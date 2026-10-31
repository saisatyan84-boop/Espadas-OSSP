#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "ui.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <locale.h>

#define COLOR_PAIR_HEADER 1
#define COLOR_PAIR_LOW    2
#define COLOR_PAIR_MID    3
#define COLOR_PAIR_HIGH   4
#define COLOR_PAIR_CYAN   5
#define COLOR_PAIR_MAGENTA 6
#define COLOR_PAIR_TEXT   7
#define COLOR_PAIR_BOX    8

static int cmp_proc_cpu(const void *a, const void *b) {
    const ProcessInfo *pa = (const ProcessInfo *)a;
    const ProcessInfo *pb = (const ProcessInfo *)b;
    if (pb->cpu_pct > pa->cpu_pct) return 1;
    if (pb->cpu_pct < pa->cpu_pct) return -1;
    return (pb->ram_mb > pa->ram_mb) - (pb->ram_mb < pa->ram_mb);
}

static int cmp_proc_mem(const void *a, const void *b) {
    const ProcessInfo *pa = (const ProcessInfo *)a;
    const ProcessInfo *pb = (const ProcessInfo *)b;
    if (pb->ram_mb > pa->ram_mb) return 1;
    if (pb->ram_mb < pa->ram_mb) return -1;
    return (pb->cpu_pct > pa->cpu_pct) - (pb->cpu_pct < pa->cpu_pct);
}

static int cmp_proc_pid(const void *a, const void *b) {
    const ProcessInfo *pa = (const ProcessInfo *)a;
    const ProcessInfo *pb = (const ProcessInfo *)b;
    return pa->pid - pb->pid;
}

static void format_bytes(double bytes, char *buf, size_t sz) {
    if (bytes >= 1e12) snprintf(buf, sz, "%.2f TB", bytes / 1e12);
    else if (bytes >= 1e9) snprintf(buf, sz, "%.1f GB", bytes / 1e9);
    else if (bytes >= 1e6) snprintf(buf, sz, "%.1f MB", bytes / 1e6);
    else if (bytes >= 1e3) snprintf(buf, sz, "%.1f KB", bytes / 1e3);
    else snprintf(buf, sz, "%.0f B", bytes);
}

static void format_rate(double bytes_sec, char *buf, size_t sz) {
    if (!isfinite(bytes_sec)) { snprintf(buf, sz, "N/A"); return; }
    if (bytes_sec >= 1e9) snprintf(buf, sz, "%.2f GB/s", bytes_sec / 1e9);
    else if (bytes_sec >= 1e6) snprintf(buf, sz, "%.1f MB/s", bytes_sec / 1e6);
    else if (bytes_sec >= 1e3) snprintf(buf, sz, "%.1f KB/s", bytes_sec / 1e3);
    else snprintf(buf, sz, "%.0f B/s", bytes_sec);
}

static void draw_box_with_title(int y, int x, int h, int w, const char *title) {
    attron(COLOR_PAIR(COLOR_PAIR_BOX));
    // corners and borders
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    for (int i = 1; i < w - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }
    for (int i = 1; i < h - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }
    attroff(COLOR_PAIR(COLOR_PAIR_BOX));

    if (title && title[0] && w > (int)strlen(title) + 4) {
        attron(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);
        mvprintw(y, x + 2, " %s ", title);
        attroff(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);
    }
}

static void draw_gauge_bar(int y, int x, int width, double pct) {
    if (!isfinite(pct)) { mvprintw(y, x, "N/A (waiting for sample)"); return; }
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;

    int num_chars = width - 10;
    if (num_chars < 4) num_chars = 4;

    int filled = (int)((pct / 100.0) * (double)num_chars);
    if (filled > num_chars) filled = num_chars;

    int cp = COLOR_PAIR_LOW;
    if (pct >= 85.0) cp = COLOR_PAIR_HIGH;
    else if (pct >= 60.0) cp = COLOR_PAIR_MID;

    mvaddch(y, x, '[');
    attron(COLOR_PAIR(cp) | A_BOLD);
    for (int i = 0; i < filled; i++) {
        mvaddch(y, x + 1 + i, '|');
    }
    attroff(COLOR_PAIR(cp) | A_BOLD);
    for (int i = filled; i < num_chars; i++) {
        mvaddch(y, x + 1 + i, ' ');
    }
    mvaddch(y, x + 1 + num_chars, ']');
    mvprintw(y, x + width - 7, "%5.1f%%", pct);
}

void ui_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100); // 100ms non-blocking getch

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(COLOR_PAIR_HEADER, COLOR_WHITE, -1);
        init_pair(COLOR_PAIR_LOW,    COLOR_GREEN, -1);
        init_pair(COLOR_PAIR_MID,    COLOR_YELLOW, -1);
        init_pair(COLOR_PAIR_HIGH,   COLOR_RED, -1);
        init_pair(COLOR_PAIR_CYAN,   COLOR_YELLOW, -1);
        init_pair(COLOR_PAIR_MAGENTA,COLOR_MAGENTA, -1);
        init_pair(COLOR_PAIR_TEXT,   COLOR_WHITE, -1);
        init_pair(COLOR_PAIR_BOX,    COLOR_WHITE, -1);
    }

    getmaxyx(stdscr, g_state.term_height, g_state.term_width);
}

void ui_shutdown(void) {
    endwin();
}

void ui_handle_resize(void) {
    struct winsize size;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&size)==0 && size.ws_row && size.ws_col &&
       (size.ws_row!=LINES || size.ws_col!=COLS))
        resizeterm(size.ws_row,size.ws_col);
    getmaxyx(stdscr, g_state.term_height, g_state.term_width);
    clear();
}

static void draw_header(void) {
    int w = g_state.term_width;
    attron(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    for (int i = 0; i < w; i++) mvaddch(0, i, ' ');

    char mode_str[64];
    if (g_state.whole_pc_mode) {
        snprintf(mode_str, sizeof(mode_str), "[ WHOLE PC (WINDOWS HOST) ]");
    } else {
        snprintf(mode_str, sizeof(mode_str), "[ WSL / LINUX ]");
    }

    char status_str[64];
    if (g_state.whole_pc_mode) {
        snprintf(status_str, sizeof(status_str), "%s",
                 g_state.agent_connected ? "Agent: Connected" : "Host: Unavailable");
    } else {
        snprintf(status_str, sizeof(status_str), "Mode: Native Linux");
    }

    double uptime = g_state.whole_pc_mode ? g_host.uptime_seconds : g_linux.uptime_seconds;
    int up_h = (int)(uptime / 3600.0);
    int up_m = (int)((uptime - up_h * 3600.0) / 60.0);

    mvprintw(0, 2, "WINTOP [C11]");
    mvprintw(0, 18, "%s", mode_str);
    mvprintw(0, w - 42, "%s", status_str);
    mvprintw(0, w - 18, "Uptime: %dh %02dm", up_h, up_m);
    attroff(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
}

static void draw_footer(void) {
    int h = g_state.term_height;
    int w = g_state.term_width;
    attron(COLOR_PAIR(COLOR_PAIR_HEADER));
    for (int i = 0; i < w; i++) mvaddch(h - 1, i, ' ');

    mvprintw(h - 1, 1, " [v] Mode  [P] Processes  [S] Sensors  [d] Devices  [c/m/p] Sort  [q] Quit");
    const char *sort_lbl = g_state.sort_mode == SORT_CPU ? "Sort: CPU" :
                          (g_state.sort_mode == SORT_MEM ? "Sort: MEM" : "Sort: PID");
    mvprintw(h - 1, w - (int)strlen(sort_lbl) - 3, "[%s]", sort_lbl);
    attroff(COLOR_PAIR(COLOR_PAIR_HEADER));
}

static void draw_overview_cpu(int y, int x, int h, int w) {
    draw_box_with_title(y, x, h, w, "CPU");

    double cpu_usage = g_state.whole_pc_mode ? g_host.cpu_usage : g_linux.cpu_usage;
    const char *name = g_state.whole_pc_mode ? g_host.cpu_name : g_linux.cpu_name;
    int procs = g_state.whole_pc_mode ? g_host.logical_processors : g_linux.core_count;

    mvprintw(y + 1, x + 2, "Processor: %.32s", name && name[0] ? name : "CPU");
    mvprintw(y + 2, x + 2, "Cores/Logical CPUs: %d", procs);
    if(g_state.whole_pc_mode) for(int i=0;i<g_host.sensor_count;i++) {
        SensorInfo *s=&g_host.sensors[i];
        if(s->has_value && !strcmp(s->name,"Windows Reported Frequency")) {
            mvprintw(y+3,x+2,"Windows reported clock: %.0f MHz",s->value);break;
        }
    }

    mvprintw(y + 4, x + 2, "Utilization:");
    draw_gauge_bar(y + 4, x + 16, w - 18, cpu_usage);

    if (g_state.whole_pc_mode) {
        mvprintw(y + 6, x + 2, "CPU temp N/A | native C agent | %.1fs samples",g_state.refresh_ms/1000.0);
    } else {
        mvprintw(y + 6, x + 2, "Load Average: %.2f  %.2f  %.2f",
                 g_linux.load_1, g_linux.load_5, g_linux.load_15);
        mvprintw(y + 7, x + 2, "Recent History: ");
        int hist_w = w - 20;
        if (hist_w > 0) {
            int count = g_linux.cpu_hist_count;
            int start = count > hist_w ? (count - hist_w) : 0;
            for (int i = start; i < count; i++) {
                double val = g_linux.cpu_history[i];
                int cp = COLOR_PAIR_LOW;
                char ch = '.';
                if (val >= 85.0) { cp = COLOR_PAIR_HIGH; ch = '#'; }
                else if (val >= 60.0) { cp = COLOR_PAIR_MID; ch = '+'; }
                else if (val >= 25.0) { ch = ':'; }
                attron(COLOR_PAIR(cp));
                mvaddch(y + 7, x + 18 + (i - start), ch);
                attroff(COLOR_PAIR(cp));
            }
        }
    }
    if (g_state.whole_pc_mode) {
        int count=g_host.cpu_hist_count, width=w-22, start=count>width?count-width:0;
        mvprintw(y+7,x+2,"CPU history 0-100%% ");
        for(int i=start;i<count;i++) {
            double value=g_host.cpu_history[i];
            mvaddch(y+7,x+20+i-start,value>75?'#':value>40?'+':value>10?':':'.');
        }
        if(h>14) {
            int count=g_host.cpu_hist_count, width=w-5, start=count>width?count-width:0;
            for(int row=0;row<3;row++) {
                for(int i=start;i<count;i++) {
                    double value=g_host.cpu_history[i];
                    if(value>=(3-row)*100.0/3) {
                        attron(COLOR_PAIR(value>80?COLOR_PAIR_HIGH:COLOR_PAIR_MID));
                        mvaddch(y+9+row,x+w-3-(count-1-i),'|');
                        attroff(COLOR_PAIR(value>80?COLOR_PAIR_HIGH:COLOR_PAIR_MID));
                    }
                }
            }
            for(int i=0;i<g_host.sensor_count;i++) {
                SensorInfo *s=&g_host.sensors[i];
                if(s->has_value && !strcmp(s->name,"GPU Temperature")) {
                    mvprintw(y+12,x+2,"GPU: %.28s | %.1f C",s->hardware,s->value); break;
                }
            }
        }
    }
}

static void draw_overview_memory(int y, int x, int h, int w) {
    draw_box_with_title(y, x, h, w, "MEMORY");

    double total_bytes = g_state.whole_pc_mode ? g_host.mem_total_bytes : g_linux.mem_total_bytes;
    double avail_bytes = g_state.whole_pc_mode ? g_host.mem_avail_bytes : g_linux.mem_avail_bytes;
    double used_bytes = total_bytes > avail_bytes ? (total_bytes - avail_bytes) : 0.0;
    double pct = total_bytes > 0.0 ? (used_bytes * 100.0 / total_bytes) : 0.0;

    char used_str[32], tot_str[32], avail_str[32];
    format_bytes(used_bytes, used_str, sizeof(used_str));
    format_bytes(total_bytes, tot_str, sizeof(tot_str));
    format_bytes(avail_bytes, avail_str, sizeof(avail_str));

    mvprintw(y + 1, x + 2, "Physical RAM: %s / %s", used_str, tot_str);
    draw_gauge_bar(y + 2, x + 2, w - 4, pct);
    mvprintw(y + 4, x + 2, "Available RAM: %s", avail_str);

    if (g_state.whole_pc_mode) {
        mvprintw(y + 6, x + 2, "Physical RAM includes WSL memory allocation.");
        mvprintw(y + 7, x + 2, "WSL limit tracked independently.");
    } else {
        char swap_u[32], swap_t[32];
        format_bytes(g_linux.swap_used_bytes, swap_u, sizeof(swap_u));
        format_bytes(g_linux.swap_total_bytes, swap_t, sizeof(swap_t));
        mvprintw(y + 6, x + 2, "Swap Usage: %s / %s", swap_u, swap_t);
        double swap_pct = g_linux.swap_total_bytes > 0.0 ?
            (g_linux.swap_used_bytes * 100.0 / g_linux.swap_total_bytes) : 0.0;
        draw_gauge_bar(y + 7, x + 2, w - 4, swap_pct);
    }
}

static void draw_overview_disk_net(int y, int x, int h, int w) {
    draw_box_with_title(y, x, h, w, "STORAGE & NETWORK");

    int line = y + 1;
    if (g_state.whole_pc_mode && g_host.drive_count > 0) {
        mvprintw(line++, x + 2, "Windows Drives (%d present):", g_host.drive_count);
        for (int i = 0; i < g_host.drive_count && line < y + 4; i++) {
            DriveInfo *di = &g_host.drives[i];
            double used = di->total_bytes - di->free_bytes;
            char ustr[32], tstr[32];
            format_bytes(used, ustr, sizeof(ustr));
            format_bytes(di->total_bytes, tstr, sizeof(tstr));
            double dpct = di->total_bytes > 0 ? (used * 100.0 / di->total_bytes) : 0;
            mvprintw(line++, x + 4, "%s: %s / %s (%.0f%%)", di->name, ustr, tstr, dpct);
        }
        char dr_str[32], dw_str[32];
        format_rate(g_host.disk_read_rate, dr_str, sizeof(dr_str));
        format_rate(g_host.disk_write_rate, dw_str, sizeof(dw_str));
        mvprintw(line++, x + 2, "Disk I/O: Read %s | Write %s", dr_str, dw_str);
    } else if (!g_state.whole_pc_mode) {
        double total = g_linux.disk_total_bytes;
        double used = g_linux.disk_used_bytes;
        char ustr[32], tstr[32];
        format_bytes(used, ustr, sizeof(ustr));
        format_bytes(total, tstr, sizeof(tstr));
        double dpct = total > 0 ? (used * 100.0 / total) : 0;
        mvprintw(line++, x + 2, "Root Filesystem (/): %s / %s", ustr, tstr);
        draw_gauge_bar(line++, x + 2, w - 4, dpct);
    }

    line = y + 5;
    mvprintw(line++, x + 2, "---------------------------------------------");
    if (g_state.whole_pc_mode && g_host.network_count > 0) {
        int idx = g_host.selected_network;
        if (idx >= g_host.network_count) idx = 0;
        NetworkInfo *net = &g_host.networks[idx];
        char rx_str[32], tx_str[32];
        format_rate(net->rx_rate, rx_str, sizeof(rx_str));
        format_rate(net->tx_rate, tx_str, sizeof(tx_str));
        mvprintw(line++, x + 2, "Adapter: %.28s [%d/%d]", net->name, idx + 1, g_host.network_count);
        mvprintw(line++, x + 2, "IP: %s", net->address[0] ? net->address : "N/A");
        mvprintw(line++, x + 2, "Down: %s | Up: %s", rx_str, tx_str);
    } else if (!g_state.whole_pc_mode) {
        char rx_str[32], tx_str[32];
        format_rate(g_linux.rx_rate, rx_str, sizeof(rx_str));
        format_rate(g_linux.tx_rate, tx_str, sizeof(tx_str));
        mvprintw(line++, x + 2, "WSL Network (eth0 / virtual):");
        mvprintw(line++, x + 2, "Download: %s", rx_str);
        mvprintw(line++, x + 2, "Upload:   %s", tx_str);
    }
}

static void draw_overview_processes(int y, int x, int h, int w) {
    const char *title = g_state.whole_pc_mode ? "TOP PROCESSES (WINDOWS)" : "TOP PROCESSES (WSL)";
    draw_box_with_title(y, x, h, w, title);

    ProcessInfo proc_copy[MAX_PROCESSES];
    int count = g_state.whole_pc_mode ? g_host.process_count : g_linux.process_count;
    if (count > MAX_PROCESSES) count = MAX_PROCESSES;
    memcpy(proc_copy, g_state.whole_pc_mode ? g_host.processes : g_linux.processes, sizeof(ProcessInfo) * count);

    if (g_state.sort_mode == SORT_CPU) qsort(proc_copy, count, sizeof(ProcessInfo), cmp_proc_cpu);
    else if (g_state.sort_mode == SORT_MEM) qsort(proc_copy, count, sizeof(ProcessInfo), cmp_proc_mem);
    else qsort(proc_copy, count, sizeof(ProcessInfo), cmp_proc_pid);

    attron(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);
    mvprintw(y + 1, x + 2, "%-7s %-10s %-8s %-10s %s", "PID", "USER", "CPU%", "RAM (MB)", "COMMAND");
    attroff(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);

    int rows = h - 3;
    for (int i = 0; i < rows && i < count; i++) {
        ProcessInfo *p = &proc_copy[i];
        int ly = y + 2 + i;
        mvprintw(ly, x + 2, "%-7d %-10.10s %6.1f%% %9.1f  %.30s",
                 p->pid, p->user, p->cpu_pct, p->ram_mb, p->name);
    }
}

static void process_panel(int x, int w, bool windows) {
    int h = g_state.term_height;
    const char *title = windows ? "WINDOWS PROCESSES" : "WSL / LINUX PROCESSES";
    draw_box_with_title(1, x, h - 2, w, title);

    ProcessInfo proc_copy[MAX_PROCESSES];
    int count = windows ? (g_host.available ? g_host.process_count : 0) : g_linux.process_count;
    if (count > MAX_PROCESSES) count = MAX_PROCESSES;
    memcpy(proc_copy, windows ? g_host.processes : g_linux.processes, sizeof(ProcessInfo) * count);

    if (g_state.sort_mode == SORT_CPU) qsort(proc_copy, count, sizeof(ProcessInfo), cmp_proc_cpu);
    else if (g_state.sort_mode == SORT_MEM) qsort(proc_copy, count, sizeof(ProcessInfo), cmp_proc_mem);
    else qsort(proc_copy, count, sizeof(ProcessInfo), cmp_proc_pid);

    attron(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);
    mvprintw(2, x+2, "%-7s %-7s %-10s %s", "PID", "CPU%", "RAM MiB", "PROCESS");
    attroff(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);

    int rows = h - 5;
    if (g_state.scroll_offset < 0) g_state.scroll_offset = 0;
    int offset=g_state.scroll_offset;
    if(offset > count-rows) offset=count>rows?count-rows:0;

    for (int i = 0; i < rows && (i + offset) < count; i++) {
        ProcessInfo *p = &proc_copy[i + offset];
        int ly = 3 + i;
        char cpu[24]="N/A",ram[24]="N/A";
        if(p->cpu_available) snprintf(cpu,sizeof(cpu),"%.1f",p->cpu_pct);
        if(p->mem_available) snprintf(ram,sizeof(ram),"%.1f",p->ram_mb);
        mvprintw(ly, x+2, "%-7d %-7s %-10s %.*s",p->pid,cpu,ram,w-31,p->name);
    }

    attron(COLOR_PAIR(COLOR_PAIR_MAGENTA));
    mvprintw(h - 2, x+2, "%d processes | CPU normalized to whole system",count);
    attroff(COLOR_PAIR(COLOR_PAIR_MAGENTA));
}

static void draw_full_processes(void) {
    int half=g_state.term_width/2;
    process_panel(0,half,true);
    process_panel(half,g_state.term_width-half,false);
}

static void draw_full_sensors(void) {
    int h = g_state.term_height;
    int w = g_state.term_width;

    draw_box_with_title(1, 0, h - 2, w, "HARDWARE SENSORS (WINDOWS HOST)");

    if (!g_host.available || g_host.sensor_count == 0) {
        mvprintw(4, 4, "No sensor data available. Windows telemetry agent must be running.");
        mvprintw(5, 4, "Press [P] or [S] to return to dashboard.");
        return;
    }

    attron(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);
    mvprintw(2, 2, "%-32s %-32s %-12s %s", "HARDWARE", "SENSOR NAME", "VALUE", "UNIT");
    attroff(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);

    int rows = h - 5;
    SensorInfo filtered[MAX_SENSORS];
    int count = 0;
    for (int i = 0; i < g_host.sensor_count; i++) {
        if (!g_state.show_unavailable_sensors && !g_host.sensors[i].has_value) continue;
        filtered[count++] = g_host.sensors[i];
    }

    if (g_state.scroll_offset < 0) g_state.scroll_offset = 0;
    if (g_state.scroll_offset > count - rows && count > rows) g_state.scroll_offset = count - rows;

    for (int i = 0; i < rows && (i + g_state.scroll_offset) < count; i++) {
        SensorInfo *s = &filtered[i + g_state.scroll_offset];
        int ly = 3 + i;
        char val_buf[32];
        if (s->has_value) snprintf(val_buf, sizeof(val_buf), "%.2f", s->value);
        else strcpy(val_buf, "N/A");

        mvprintw(ly, 2, "%-32.32s %-32.32s %-12s %s",
                 s->hardware, s->name, val_buf, s->unit);
    }

    attron(COLOR_PAIR(COLOR_PAIR_MAGENTA));
    mvprintw(h - 2, 2, "Sensors: %d shown | [u] Toggle N/A sensors | Scroll: %d/%d | Status: %.40s",
             count, g_state.scroll_offset + 1, count > 0 ? count : 1, g_host.sensor_status);
    attroff(COLOR_PAIR(COLOR_PAIR_MAGENTA));
}

static int category_order(const char *category) {
    const char *order[]={"Display","Audio","Bluetooth","Input","USB","Storage","Network","Audio hardware","Bluetooth hardware"};
    for(int i=0;i<9;i++) if(!strcmp(category,order[i])) return i;
    return 9;
}
static int cmp_device(const void *a,const void *b) {
    const DeviceInfo *left=a,*right=b;
    int delta=category_order(left->category)-category_order(right->category);
    return delta?delta:strcmp(left->name,right->name);
}

static void draw_full_devices(void) {
    int h = g_state.term_height;
    int w = g_state.term_width;

    draw_box_with_title(1, 0, h - 2, w, "CONNECTED DEVICES (WINDOWS HOST)");

    if (!g_host.available || g_host.device_count == 0) {
        mvprintw(4, 4, "No device data available. Windows telemetry agent must be running.");
        mvprintw(5, 4, "Press [P] or [d] to return to dashboard.");
        return;
    }

    attron(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);
    mvprintw(2, 2, "%-16s %-36s %-12s %-10s %s", "CATEGORY", "DEVICE NAME", "STATE", "BATTERY", "DETAIL");
    attroff(COLOR_PAIR(COLOR_PAIR_CYAN) | A_BOLD);

    int rows = h - 5;
    DeviceInfo filtered[MAX_DEVICES];
    int count = 0;
    for (int i = 0; i < g_host.device_count; i++) {
        DeviceInfo *d = &g_host.devices[i];
        if (!g_state.show_paired_devices && strcmp(d->category, "Bluetooth") == 0) {
            if (strstr(d->state, "Disconnected") || strstr(d->state, "Paired")) continue;
        }
        filtered[count++] = *d;
    }
    qsort(filtered,(size_t)count,sizeof(*filtered),cmp_device);

    if (g_state.scroll_offset < 0) g_state.scroll_offset = 0;
    if (g_state.scroll_offset > count - rows && count > rows) g_state.scroll_offset = count - rows;

    for (int i = 0; i < rows && (i + g_state.scroll_offset) < count; i++) {
        DeviceInfo *d = &filtered[i + g_state.scroll_offset];
        int ly = 3 + i;
        char bat_buf[32];
        if (d->battery >= 0.0) snprintf(bat_buf, sizeof(bat_buf), "%.0f%%", d->battery);
        else strcpy(bat_buf, "N/A");

        mvprintw(ly, 2, "%-16.16s %-36.36s %-12.12s %-10s %.30s",
                 d->category, d->name, d->state, bat_buf, d->detail);
    }

    attron(COLOR_PAIR(COLOR_PAIR_MAGENTA));
    mvprintw(h - 2, 2, "Devices: %d | [b] Toggle paired BT devices | Scroll: %d/%d | Status: %.40s",
             count, g_state.scroll_offset + 1, count > 0 ? count : 1, g_host.inventory_status);
    attroff(COLOR_PAIR(COLOR_PAIR_MAGENTA));
}

void ui_render(void) {
    erase();
    if (g_state.term_width < 120 || g_state.term_height < 32) {
        mvaddnstr(0,0,"Wintop C: enlarge terminal to 120 columns x 32 rows. q quits.",g_state.term_width-1);
        refresh(); return;
    }
    draw_header();

    int h = g_state.term_height;
    int w = g_state.term_width;

    if (g_state.whole_pc_mode && !g_host.available) {
        mvprintw(4,4,"Windows host unavailable. Retrying native C agent; press v for WSL data.");
        draw_footer(); refresh(); return;
    }
    if (g_state.view_mode == VIEW_OVERVIEW) {
        int half_w = w / 2;
        int top_h = (h - 2) / 2;
        int bot_h = (h - 2) - top_h;

        draw_overview_cpu(1, 0, top_h, half_w);
        draw_overview_memory(1, half_w, top_h, w - half_w);
        draw_overview_disk_net(1 + top_h, 0, bot_h, half_w);
        draw_overview_processes(1 + top_h, half_w, bot_h, w - half_w);
    } else if (g_state.view_mode == VIEW_PROCESSES) {
        draw_full_processes();
    } else if (g_state.view_mode == VIEW_SENSORS) {
        draw_full_sensors();
    } else if (g_state.view_mode == VIEW_DEVICES) {
        draw_full_devices();
    }

    draw_footer();
    refresh();
}

void ui_render_once(void) {
    printf("=================================================================\n");
    printf(" WINTOP C11 SYSTEM MONITOR (One-shot Snapshot)\n");
    printf("=================================================================\n");
    printf("Host Telemetry: %s\n", g_host.available ? "Available" : "Not connected (WSL fallback)");
    if (g_host.available) {
        printf("Windows CPU:      %.1f%% (%s, %d logical processors)\n",
               g_host.cpu_usage, g_host.cpu_name, g_host.logical_processors);
        printf("Windows RAM:      %.2f / %.2f GB\n",
               (g_host.mem_total_bytes - g_host.mem_avail_bytes) / 1e9, g_host.mem_total_bytes / 1e9);
        printf("Windows Processes: %d running\n", g_host.process_count);
        printf("Windows Drives:    %d drives\n", g_host.drive_count);
        printf("Windows Sensors:   %d sensors\n", g_host.sensor_count);
        printf("Windows Devices:   %d devices\n", g_host.device_count);
    }
    printf("-----------------------------------------------------------------\n");
    printf("WSL/Linux CPU:    %.1f%% (Load: %.2f %.2f %.2f)\n",
           g_linux.cpu_usage, g_linux.load_1, g_linux.load_5, g_linux.load_15);
    printf("WSL/Linux RAM:    %.2f / %.2f GB (Swap: %.2f / %.2f GB)\n",
           g_linux.mem_used_bytes / 1e9, g_linux.mem_total_bytes / 1e9,
           g_linux.swap_used_bytes / 1e9, g_linux.swap_total_bytes / 1e9);
    printf("WSL/Linux Disk:   %.2f / %.2f GB on /\n",
           g_linux.disk_used_bytes / 1e9, g_linux.disk_total_bytes / 1e9);
    printf("WSL Processes:    %d running\n", g_linux.process_count);
    printf("=================================================================\n");
}
