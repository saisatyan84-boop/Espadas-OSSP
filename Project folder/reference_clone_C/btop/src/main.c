#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "wintop.h"
#include "host_ipc.h"
#include "linux_sys.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <ncurses.h>

AppState g_state;
HostData g_host;
LinuxData g_linux;

static volatile sig_atomic_t s_running = 1;
static volatile sig_atomic_t s_resized = 0;

static void handle_sigint(int sig) {
    (void)sig;
    s_running = 0;
}

static void handle_sigwinch(int sig) {
    (void)sig;
    s_resized = 1;
}

static void print_usage(const char *prog) {
    printf("Wintop: Whole-PC & WSL Linux System Monitor (Pure C11)\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  --once             Print one snapshot to stdout and exit\n");
    printf("  --agent <path>     Path to Windows companion agent executable\n");
    printf("  --interval-ms <n>  Refresh interval, 1000..60000 (default 2000)\n");
    printf("  -h, --help         Show this help message\n\n");
    printf("Interactive Controls:\n");
    printf("  v                  Toggle Whole-PC (Windows) vs WSL/Linux\n");
    printf("  P                  Toggle Full Process table\n");
    printf("  S                  Toggle Hardware Sensors page\n");
    printf("  d                  Toggle Connected Devices page\n");
    printf("  c / m / p          Sort processes by CPU / Memory / PID\n");
    printf("  u                  Toggle display of unavailable sensors\n");
    printf("  b                  Toggle display of paired Bluetooth devices\n");
    printf("  [ / ]              Select Windows network adapter\n");
    printf("  Up / Down / j / k  Scroll process, sensor, or device lists\n");
    printf("  q                  Quit\n");
}

int main(int argc, char **argv) {
    bool once_mode = false;
    const char *agent_override = NULL;
    int refresh_ms=2000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {
            once_mode = true;
        } else if (strcmp(argv[i], "--agent") == 0 && i + 1 < argc) {
            agent_override = argv[++i];
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i+1<argc) {
            char *end;
            long value=strtol(argv[++i],&end,10);
            if(*end || value<1000 || value>60000) {
                fprintf(stderr,"Refresh interval must be 1000..60000 milliseconds.\n");
                return 2;
            }
            refresh_ms=(int)value;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 2;
        }
    }

    // Initialize application state
    memset(&g_state, 0, sizeof(g_state));
    memset(&g_host, 0, sizeof(g_host));
    memset(&g_linux, 0, sizeof(g_linux));

    g_state.whole_pc_mode = true;
    g_state.refresh_ms=refresh_ms;
    g_state.view_mode = VIEW_OVERVIEW;
    g_state.sort_mode = SORT_CPU;
    g_state.show_unavailable_sensors = false;
    g_state.show_paired_devices = false;

    linux_sys_init();
    host_ipc_start(agent_override);

    if (once_mode) {
        // Allow native Windows inventory startup and two CPU samples.
        for (int i = 0; i < (refresh_ms+12000)/100; i++) {
            host_ipc_poll();
            if (i>=10 && !g_state.agent_running && !g_state.whole_pc_mode) break;
            if (g_host.available && g_host.cpu_hist_count > 0) break;
            usleep(100000); // 100ms
        }
        linux_sys_update();
        ui_render_once();
        host_ipc_stop();
        return 0;
    }

    // Setup signal handlers
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "Interactive mode requires a terminal; use --once.\n");
        host_ipc_stop();
        return 2;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct sigaction sw;
    memset(&sw, 0, sizeof(sw));
    sw.sa_handler = handle_sigwinch;
    sigaction(SIGWINCH, &sw, NULL);

    ui_init();

    struct timespec last_linux_update;
    clock_gettime(CLOCK_MONOTONIC, &last_linux_update);

    bool redraw=true;
    while (s_running) {
        if (s_resized) {
            s_resized = 0;
            ui_handle_resize();
            redraw=true;
        }

        // Poll host IPC for telemetry updates
        double previous_sample=g_host.sample_seconds;
        bool connected=g_state.agent_connected;
        host_ipc_poll();
        if(previous_sample!=g_host.sample_seconds || connected!=g_state.agent_connected) redraw=true;

        // Check if 1 second passed for Linux sys update
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - last_linux_update.tv_sec) +
                         (double)(now.tv_nsec - last_linux_update.tv_nsec) * 1e-9;
        if (elapsed >= refresh_ms/1000.0) {
            linux_sys_update();
            last_linux_update = now;
            redraw=true;
        }

        // Handle user input
        int ch = getch();
        if (ch != ERR) {
            redraw=true;
            switch (ch) {
                case 'q':
                case 'Q':
                    s_running = 0;
                    break;
                case 'v':
                case 'V':
                    g_state.whole_pc_mode = !g_state.whole_pc_mode;
                    g_state.scroll_offset = 0;
                    break;
                case 'P':
                    if (g_state.view_mode == VIEW_PROCESSES) {
                        g_state.view_mode = VIEW_OVERVIEW;
                    } else {
                        g_state.view_mode = VIEW_PROCESSES;
                    }
                    g_state.scroll_offset = 0;
                    break;
                case 'S':
                    if (g_state.view_mode == VIEW_SENSORS) {
                        g_state.view_mode = VIEW_OVERVIEW;
                    } else {
                        g_state.view_mode = VIEW_SENSORS;
                    }
                    g_state.scroll_offset = 0;
                    break;
                case 'd':
                    if (g_state.view_mode == VIEW_DEVICES) {
                        g_state.view_mode = VIEW_OVERVIEW;
                    } else {
                        g_state.view_mode = VIEW_DEVICES;
                    }
                    g_state.scroll_offset = 0;
                    break;
                case 'c':
                case 'C':
                    g_state.sort_mode = SORT_CPU;
                    break;
                case 'm':
                case 'M':
                    g_state.sort_mode = SORT_MEM;
                    break;
                case 'p':
                    g_state.sort_mode = SORT_PID;
                    break;
                case 'r':
                    host_ipc_stop();
                    host_ipc_start(agent_override);
                    break;
                case 'u':
                case 'U':
                    g_state.show_unavailable_sensors = !g_state.show_unavailable_sensors;
                    break;
                case 'b':
                case 'B':
                    g_state.show_paired_devices = !g_state.show_paired_devices;
                    break;
                case '[':
                    if (g_host.network_count > 0) {
                        g_host.selected_network = (g_host.selected_network - 1 + g_host.network_count) % g_host.network_count;
                    }
                    break;
                case ']':
                    if (g_host.network_count > 0) {
                        g_host.selected_network = (g_host.selected_network + 1) % g_host.network_count;
                    }
                    break;
                case KEY_UP:
                case 'k':
                    if (g_state.scroll_offset > 0) g_state.scroll_offset--;
                    break;
                case KEY_DOWN:
                case 'j':
                    if(g_state.scroll_offset < MAX_DEVICES+MAX_PROCESSES) g_state.scroll_offset++;
                    break;
                case KEY_PPAGE:
                    g_state.scroll_offset -= 10;
                    if (g_state.scroll_offset < 0) g_state.scroll_offset = 0;
                    break;
                case KEY_NPAGE:
                    if(g_state.scroll_offset < MAX_DEVICES+MAX_PROCESSES) g_state.scroll_offset += 10;
                    break;
                case KEY_HOME:
                    g_state.scroll_offset = 0;
                    break;
                case '\t':
                    // Tab switches whole-pc / WSL or leaves special view
                    if (g_state.view_mode != VIEW_OVERVIEW) {
                        g_state.view_mode = VIEW_OVERVIEW;
                    } else {
                        g_state.whole_pc_mode = !g_state.whole_pc_mode;
                    }
                    g_state.scroll_offset = 0;
                    break;
                case KEY_RESIZE:
                    ui_handle_resize();
                    break;
                default:
                    break;
            }
        }

        if(redraw) {ui_render();redraw=false;}
    }

    ui_shutdown();
    host_ipc_stop();
    return 0;
}
