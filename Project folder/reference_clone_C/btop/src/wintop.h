#ifndef WINTOP_H
#define WINTOP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 1024
#define MAX_NETWORKS 32
#define MAX_DRIVES 32
#define MAX_DEVICES 256
#define MAX_SENSORS 256
#define CPU_HISTORY_LEN 60

typedef struct {
    int pid;
    char name[128];
    char user[64];
    double ram_mb;
    double cpu_pct;
    bool cpu_available;
    bool mem_available;
    uint64_t last_cpu_seconds;
    double last_sample_seconds;
} ProcessInfo;

typedef struct {
    char id[128];
    char name[128];
    char kind[64];
    char state[64];
    char address[64];
    bool is_virtual;
    double rx_rate; // B/s
    double tx_rate; // B/s
} NetworkInfo;

typedef struct {
    char name[64];
    char kind[64];
    double total_bytes;
    double free_bytes;
} DriveInfo;

typedef struct {
    char id[128];
    char category[64];
    char name[128];
    char state[64];
    char detail[128];
    double battery; // -1 if unavailable
} DeviceInfo;

typedef struct {
    char id[128];
    char hardware[128];
    char kind[64];
    char name[128];
    char unit[32];
    double value;
    bool has_value;
} SensorInfo;

typedef struct {
    bool available;
    char cpu_name[128];
    double cpu_usage; // 0 - 100
    int logical_processors;
    double uptime_seconds;
    double mem_total_bytes;
    double mem_avail_bytes;
    double disk_read_rate;
    double disk_write_rate;
    char inventory_status[128];
    char sensor_status[128];

    NetworkInfo networks[MAX_NETWORKS];
    int network_count;
    int selected_network;

    DriveInfo drives[MAX_DRIVES];
    int drive_count;

    DeviceInfo devices[MAX_DEVICES];
    int device_count;

    SensorInfo sensors[MAX_SENSORS];
    int sensor_count;

    ProcessInfo processes[MAX_PROCESSES];
    int process_count;

    double sample_seconds;
    double cpu_history[CPU_HISTORY_LEN];
    int cpu_hist_count;
} HostData;

typedef struct {
    double cpu_usage;
    double load_1, load_5, load_15;
    double mem_total_bytes;
    double mem_used_bytes;
    double mem_avail_bytes;
    double swap_total_bytes;
    double swap_used_bytes;
    double disk_total_bytes;
    double disk_used_bytes;
    double rx_rate; // B/s
    double tx_rate; // B/s
    double uptime_seconds;
    char cpu_name[128];
    int core_count;

    ProcessInfo processes[MAX_PROCESSES];
    int process_count;

    double cpu_history[CPU_HISTORY_LEN];
    int cpu_hist_count;
} LinuxData;

typedef enum {
    VIEW_OVERVIEW = 0,
    VIEW_PROCESSES,
    VIEW_SENSORS,
    VIEW_DEVICES
} ViewMode;

typedef enum {
    SORT_CPU = 0,
    SORT_MEM,
    SORT_PID
} SortMode;

typedef struct {
    bool whole_pc_mode; // true: Windows host, false: WSL/Linux
    int refresh_ms;
    ViewMode view_mode;
    SortMode sort_mode;
    int scroll_offset;
    bool show_unavailable_sensors;
    bool show_paired_devices;
    char status_message[256];
    bool agent_running;
    bool agent_connected;
    int term_width;
    int term_height;
} AppState;

extern AppState g_state;
extern HostData g_host;
extern LinuxData g_linux;

#endif // WINTOP_H
