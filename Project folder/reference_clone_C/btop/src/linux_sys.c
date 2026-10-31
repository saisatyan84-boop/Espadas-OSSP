#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "linux_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <pwd.h>
#include <time.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

static unsigned long long s_prev_idle = 0;
static unsigned long long s_prev_total = 0;

static unsigned long long s_prev_rx_bytes = 0;
static unsigned long long s_prev_tx_bytes = 0;
static double s_prev_net_time = 0.0;

#define LINUX_PID_TRACK_MAX 4096
typedef struct {
    int pid;
    unsigned long long utime;
    unsigned long long stime;
    double timestamp;
} LinuxPidTrack;

static LinuxPidTrack s_lpid_track[LINUX_PID_TRACK_MAX];
static int s_lpid_track_count = 0;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void read_cpuinfo(void) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;
    char line[256];
    int cores = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) == 0 && g_linux.cpu_name[0] == '\0') {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (isspace((unsigned char)*colon)) colon++;
                size_t len = strlen(colon);
                if (len > 0 && colon[len - 1] == '\n') colon[len - 1] = '\0';
                strncpy(g_linux.cpu_name, colon, sizeof(g_linux.cpu_name) - 1);
            }
        }
        if (strncmp(line, "processor", 9) == 0) {
            cores++;
        }
    }
    fclose(f);
    g_linux.core_count = cores > 0 ? cores : 1;
    if (g_linux.cpu_name[0] == '\0') {
        strcpy(g_linux.cpu_name, "WSL/Linux Virtual Processor");
    }
}

static void update_cpu(void) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;
    char line[512];
    if (fgets(line, sizeof(line), f)) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        int matched = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                             &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        if (matched >= 4) {
            unsigned long long idle_all = idle + (matched >= 5 ? iowait : 0);
            unsigned long long non_idle = user + nice + system +
                (matched >= 6 ? irq : 0) + (matched >= 7 ? softirq : 0) + (matched >= 8 ? steal : 0);
            unsigned long long total = idle_all + non_idle;

            if (s_prev_total > 0 && total > s_prev_total) {
                unsigned long long d_total = total - s_prev_total;
                unsigned long long d_idle = (idle_all >= s_prev_idle) ? (idle_all - s_prev_idle) : 0;
                double usage = (double)(d_total - d_idle) * 100.0 / (double)d_total;
                if (usage < 0.0) usage = 0.0;
                if (usage > 100.0) usage = 100.0;
                g_linux.cpu_usage = usage;

                if (g_linux.cpu_hist_count < CPU_HISTORY_LEN) {
                    g_linux.cpu_history[g_linux.cpu_hist_count++] = usage;
                } else {
                    memmove(&g_linux.cpu_history[0], &g_linux.cpu_history[1], sizeof(double) * (CPU_HISTORY_LEN - 1));
                    g_linux.cpu_history[CPU_HISTORY_LEN - 1] = usage;
                }
            }
            s_prev_total = total;
            s_prev_idle = idle_all;
        }
    }
    fclose(f);

    FILE *fl = fopen("/proc/loadavg", "r");
    if (fl) {
        if (fscanf(fl, "%lf %lf %lf", &g_linux.load_1, &g_linux.load_5, &g_linux.load_15) != 3) {
            g_linux.load_1 = g_linux.load_5 = g_linux.load_15 = 0;
        }
        fclose(fl);
    }
}

static void update_memory(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char line[256];
    unsigned long long total = 0, free = 0, avail = 0, swap_tot = 0, swap_free = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %llu kB", &total) == 1) continue;
        if (sscanf(line, "MemFree: %llu kB", &free) == 1) continue;
        if (sscanf(line, "MemAvailable: %llu kB", &avail) == 1) continue;
        if (sscanf(line, "SwapTotal: %llu kB", &swap_tot) == 1) continue;
        if (sscanf(line, "SwapFree: %llu kB", &swap_free) == 1) continue;
    }
    fclose(f);

    g_linux.mem_total_bytes = (double)total * 1024.0;
    g_linux.mem_avail_bytes = (double)avail * 1024.0;
    g_linux.mem_used_bytes = (double)(total - avail) * 1024.0;
    g_linux.swap_total_bytes = (double)swap_tot * 1024.0;
    g_linux.swap_used_bytes = (double)(swap_tot - swap_free) * 1024.0;
}

static void update_disk(void) {
    struct statvfs v;
    if (statvfs("/", &v) == 0) {
        double total = (double)v.f_blocks * (double)v.f_frsize;
        double free_b = (double)v.f_bavail * (double)v.f_frsize;
        g_linux.disk_total_bytes = total;
        g_linux.disk_used_bytes = total - free_b;
    }
}

static void update_network(void) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;
    char line[512];
    unsigned long long total_rx = 0, total_tx = 0;
    // skip 2 header lines
    if (fgets(line, sizeof(line), f) && fgets(line, sizeof(line), f)) {
        while (fgets(line, sizeof(line), f)) {
            char *colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            char *iface = line;
            while (isspace((unsigned char)*iface)) iface++;
            if (strcmp(iface, "lo") == 0) continue;

            unsigned long long r, t, dummy;
            if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &r, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &t) >= 9) {
                total_rx += r;
                total_tx += t;
            }
        }
    }
    fclose(f);

    double now = get_time_sec();
    if (s_prev_net_time > 0.0 && now > s_prev_net_time) {
        double dt = now - s_prev_net_time;
        if (total_rx >= s_prev_rx_bytes) g_linux.rx_rate = (double)(total_rx - s_prev_rx_bytes) / dt;
        if (total_tx >= s_prev_tx_bytes) g_linux.tx_rate = (double)(total_tx - s_prev_tx_bytes) / dt;
    }
    s_prev_rx_bytes = total_rx;
    s_prev_tx_bytes = total_tx;
    s_prev_net_time = now;
}

static double track_linux_proc_cpu(int pid, unsigned long long ut, unsigned long long st, double now, int cores) {
    if (cores < 1) cores = 1;
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck < 1) clk_tck = 100;
    double total_sec = (double)(ut + st) / (double)clk_tck;

    for (int i = 0; i < s_lpid_track_count; i++) {
        if (s_lpid_track[i].pid == pid) {
            double dt = now - s_lpid_track[i].timestamp;
            double prev_sec = (double)(s_lpid_track[i].utime + s_lpid_track[i].stime) / (double)clk_tck;
            s_lpid_track[i].utime = ut;
            s_lpid_track[i].stime = st;
            s_lpid_track[i].timestamp = now;
            if (dt > 0.05 && total_sec >= prev_sec) {
                double pct = ((total_sec - prev_sec) / dt) * 100.0 / (double)cores;
                if (pct < 0.0) pct = 0.0;
                if (pct > 100.0) pct = 100.0;
                return pct;
            }
            return 0.0;
        }
    }
    if (s_lpid_track_count < LINUX_PID_TRACK_MAX) {
        s_lpid_track[s_lpid_track_count].pid = pid;
        s_lpid_track[s_lpid_track_count].utime = ut;
        s_lpid_track[s_lpid_track_count].stime = st;
        s_lpid_track[s_lpid_track_count].timestamp = now;
        s_lpid_track_count++;
    } else {
        s_lpid_track_count = 0;
        s_lpid_track[0].pid = pid;
        s_lpid_track[0].utime = ut;
        s_lpid_track[0].stime = st;
        s_lpid_track[0].timestamp = now;
        s_lpid_track_count = 1;
    }
    return 0.0;
}

static void update_processes(void) {
    DIR *d = opendir("/proc");
    if (!d) return;

    g_linux.process_count = 0;
    double now = get_time_sec();
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 1) page_size = 4096;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && g_linux.process_count < MAX_PROCESSES) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;
        int pid = atoi(ent->d_name);
        if (pid <= 0) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;

        char buf[2048];
        if (!fgets(buf, sizeof(buf), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        char *lparen = strchr(buf, '(');
        char *rparen = strrchr(buf, ')');
        if (!lparen || !rparen || rparen < lparen) continue;

        ProcessInfo *p = &g_linux.processes[g_linux.process_count];
        memset(p, 0, sizeof(*p));
        p->pid = pid;

        size_t name_len = (size_t)(rparen - lparen - 1);
        if (name_len > sizeof(p->name) - 1) name_len = sizeof(p->name) - 1;
        memcpy(p->name, lparen + 1, name_len);
        p->name[name_len] = '\0';

        char state;
        int ppid, pgrp, session, tty_nr, tpgid;
        unsigned flags;
        unsigned long minflt, cminflt, majflt, cmajflt;
        unsigned long long utime = 0, stime = 0;
        long cutime, cstime, priority, nice_val, num_threads, itrealvalue;
        unsigned long long starttime;
        unsigned long vsize;
        long rss = 0;

        if (sscanf(rparen + 2,
                   "%c %d %d %d %d %d %u %lu %lu %lu %lu %llu %llu %ld %ld %ld %ld %ld %ld %llu %lu %ld",
                   &state, &ppid, &pgrp, &session, &tty_nr, &tpgid, &flags,
                   &minflt, &cminflt, &majflt, &cmajflt,
                   &utime, &stime, &cutime, &cstime, &priority, &nice_val,
                   &num_threads, &itrealvalue, &starttime, &vsize, &rss) == 22) {
            p->ram_mb = (double)rss * (double)page_size / (1024.0 * 1024.0);
            p->mem_available = true;
            p->cpu_pct = track_linux_proc_cpu(pid, utime, stime, now, g_linux.core_count);
            p->cpu_available = true;
        }

        // Get user from /proc/[pid]/status
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        FILE *fs = fopen(path, "r");
        if (fs) {
            char sline[256];
            unsigned uid = 0;
            while (fgets(sline, sizeof(sline), fs)) {
                if (sscanf(sline, "Uid:\t%u", &uid) == 1) {
                    struct passwd *pw = getpwuid(uid);
                    if (pw) {
                        strncpy(p->user, pw->pw_name, sizeof(p->user) - 1);
                    } else {
                        snprintf(p->user, sizeof(p->user), "%u", uid);
                    }
                    break;
                }
            }
            fclose(fs);
        }
        if (p->user[0] == '\0') strcpy(p->user, "N/A");

        g_linux.process_count++;
    }
    closedir(d);
}

void linux_sys_init(void) {
    memset(&g_linux, 0, sizeof(g_linux));
    read_cpuinfo();
    update_cpu();
    update_memory();
    update_disk();
    update_network();
}

void linux_sys_update(void) {
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        g_linux.uptime_seconds = (double)si.uptime;
    }
    update_cpu();
    update_memory();
    update_disk();
    update_network();
    update_processes();
}
