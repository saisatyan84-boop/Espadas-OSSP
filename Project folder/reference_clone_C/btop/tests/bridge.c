/* SPDX-License-Identifier: MIT */
/* Test parser rejection, null values, PID reuse and CPU normalization. */
#include <assert.h>
#include "../src/host_ipc.c"
AppState g_state;HostData g_host;LinuxData g_linux;
int main(void) {
    parse("{\"source\":\"windows\",\"host\":{}}");
    assert(!g_host.available);
    parse("{\"schema_version\":1,\"source\":\"windows\",\"sample_seconds\":1,\"host\":{\"cpu\":{\"usage\":null,\"logical_processors\":4},\"memory\":{\"total\":100,\"available\":50}},\"processes\":[{\"pid\":42,\"cpu_seconds\":1,\"start_seconds\":10,\"name\":\"test\"}]}");
    assert(g_host.available && isnan(g_host.cpu_usage));
    assert(g_host.process_count==1 && !g_host.processes[0].cpu_available);
    parse("{\"schema_version\":1,\"source\":\"windows\",\"sample_seconds\":2,\"host\":{\"cpu\":{\"usage\":25,\"logical_processors\":4},\"memory\":{\"total\":100,\"available\":50}},\"processes\":[{\"pid\":42,\"cpu_seconds\":2,\"start_seconds\":10}]}");
    assert(g_host.processes[0].cpu_available && g_host.processes[0].cpu_pct==25);
    assert(g_host.cpu_hist_count==1);
    parse("{\"schema_version\":1,\"source\":\"windows\",\"sample_seconds\":3,\"host\":{\"cpu\":{\"usage\":50,\"logical_processors\":4},\"memory\":{\"total\":100,\"available\":50}},\"processes\":[{\"pid\":42,\"cpu_seconds\":3,\"start_seconds\":11}]}");
    assert(!g_host.processes[0].cpu_available);
    parse("{\"schema_version\":2,\"source\":\"windows\",\"sample_seconds\":4}");
    assert(g_host.sample_seconds==3);
    parse("{\"schema_version\":1,\"source\":\"windows\",\"sample_seconds\":4,\"host\":{\"cpu\":{},\"memory\":{\"total\":1,\"available\":100}},\"processes\":[]}");
    assert(g_host.sample_seconds==3);
    puts("C bridge validation tests passed");
    return 0;
}
