# Wintop: whole-PC monitoring in the btop terminal

This modifies `reference_clone/btop` (upstream base 612e18f), retaining btop's
native drawing, graphs, themes, configuration and Linux process controls.
The separate `linux-sysmon/monitor` project is not used by this version.

## Run

```bash
cd /home/satyan/Espadas_OSSP/Project/reference_clone/btop
./wintop
```

Quit an older running copy and launch again to load the new binary. Running
the system `btop` command still launches the separately installed version.

Rebuild both components after source changes:

```bash
sh scripts/build-host.sh
make -j2
./wintop
```

The host build uses the Windows .NET 9 SDK already installed on this machine.
The framework-dependent agent requires Windows .NET 9 runtime. The script
publishes into `windows-agent/publish` and sets the WSL executable permission.
The btop client retains upstream's C++23 requirement (tested with GCC 15.2).
No administrator access or system-wide installation is required.

## Dashboard and controls

The default CPU, memory, drive and network panels now use **Windows host data**.
Host physical RAM usage already includes RAM backing WSL. WSL used memory and
its allocation limit appear separately; they are never added to Windows usage.
The CPU aggregate is 0–100% across all logical processors. Windows process CPU
remains 100% per logical processor, as labeled in its separate table.

| Key | Action |
| --- | --- |
| `v` | Toggle main panels between whole PC and WSL/Linux |
| `d` | Open Windows connected-device inventory |
| `S` | Open hardware sensors (capital S) |
| `P` | Return to separate process lists (capital P) |
| `b` in devices | Include/hide paired but disconnected Bluetooth devices |
| `[` / `]` | Select Windows network adapter |
| Tab | Switch Windows/WSL process focus; leave a device/sensor page |
| arrows, j/k, Page Up/Down, Home/End | Scroll the focused host list |
| c/m/p with Windows focus | Sort Windows processes by CPU/RAM/PID |
| r with Windows focus | Retry an unavailable agent |
| q | Quit |

The device page shows connected Bluetooth endpoints by default, plus separate
audio, radio, display, input peripheral, network hardware, storage and USB
groups. Paired endpoints are counted but hidden until `b`; paired does not mean
connected. Classic/LE endpoints sharing an address are deduplicated. Battery
percentages are displayed only for connected devices with a valid Windows
battery property; 0% is a valid measurement, while unknown is N/A.

Windows process actions remain read-only. Host PIDs never enter Linux signal,
kill, nice, tree or process-detail code. On a narrow terminal, Tab selects one
process list. Larger layouts show both. Original btop controls remain available
in the WSL process panel except the explicitly assigned new global shortcuts.
Additional native btop GPU boxes are still the upstream WSL-visible collector;
the new Windows GPU summary is in the whole-PC CPU/hardware panel and S page.

## Measurements and limitations

- CPU: GetSystemTimes deltas. Machines with more than 64 logical CPUs currently
  show aggregate CPU as unavailable rather than incorrectly reporting one group.
- RAM: GlobalMemoryStatusEx total/available physical memory, not a sum of processes.
- Network: per-adapter traffic deltas and IP address. Adapters are selectable,
  not summed, avoiding double-counting traffic through virtual interfaces.
  Addressless NDIS filter pseudo-interfaces are excluded. Wi-Fi SSID is not yet shown.
- Storage: Windows fixed/removable volume capacity and aggregate physical-disk
  read/write counters. Activity is across all disks, not attributed to C: alone.
- Sensors/GPU: LibreHardwareMonitor, including temperature, power, clocks, load,
  voltage, fans and VRAM when the hardware exposes them. Every returned sensor
  is browsable; the main panel summarizes the CPU and detected GPUs.
- In this machine's normal-user session, CPU low-level register readings are
  unavailable. The collector suppresses misleading zero/default CPU temperature,
  power, clock and voltage values without elevation. No automatic elevation is
  attempted; elevation is not a guarantee of hardware support. SSD temperature
  was unavailable. GPU temperature, hotspot, VRAM, clocks and power were verified.
- Bluetooth: a WinRT association-endpoint watcher observes connection/property
  changes. Uses System.Devices.Aep.IsConnected and System.Devices.BatteryLife.
  Vendor-specific left/right/case batteries and direct BLE GATT reads are not
  implemented. No connected earbuds were available for a live battery check.
- Audio: current present endpoints, default output and default microphone via
  Core Audio. 'Present' alone does not claim an audio stream is playing.
- USB/input: present PnP devices, including internal USB devices and hubs. A
  composite physical device may have several functional rows. Grouping those
  functions into physical containers remains future work.
- Displays: active DisplayConfig paths, resolution, refresh rate, connector
  technology (including eDP, HDMI, DisplayPort, Miracast where reported). A generic
  DisplayPort path cannot reliably identify a physical USB-C connector; this is
  stated explicitly. Inactive display paths are not yet enumerated. Names may
  fall back to 'Monitor' when the driver provides no friendly name.

Missing measurements show N/A. Agent failures are labeled Unavailable/Stale and
retry with bounded backoff. The WSL view remains accessible with `v`; Linux-only
mode bypasses the agent entirely:

```bash
WINTOP_WINDOWS=0 ./wintop
# Optional explicit executable override (WSL path):
WINTOP_AGENT=/mnt/c/path/sysmon-host.exe ./wintop
```

The previous persistent PowerShell **process-only** prototype remains available
only via explicit `WINTOP_POWERSHELL=...`; it cannot fill whole-PC panels.

## Architecture

`windows-agent/Program.cs` owns one persistent JSON producer, with independent
system/process/network, device, and sensor work. Fast telemetry follows the
configured refresh interval with a 1-second minimum; devices update about every
5 seconds and sensors every 3 seconds. Slow calls do not block fast frames.
Bluetooth notifications are event-driven and appear on the next inventory
sample. Slow sections carry their own timestamps and are marked stale when old.

Collectors: SystemCollector, NetworkCollector, DeviceCollector, BluetoothCollector,
AudioCollector, DisplayCollector, StorageCollector, and SensorCollector. DeviceCollector
coordinates specialized APIs with a single shared PnP enumeration.

Client modules:
- `host/model.hpp`: distinct host telemetry and process models.
- `btop_host_bridge.cpp`: JSON validation, process deltas, reconnect/backoff.
- `btop_host_ipc.cpp`: argument-vector launch, bounded nonblocking pipes, cleanup.
- `btop_host.cpp`: history, input focus, layout and lifecycle integration.
- `btop_host_draw.cpp`: host resource, device and sensor rendering using btop.

The transport is UTF-8 JSON Lines, schema_version 1, source windows, increasing
sample_seconds, a processes array and optional host extension. Frames are bounded
to 4 MiB, depth 12, bounded lists/strings/numbers. Host/process data are committed
together only after validation. CPU baselines reset on producer restart or PID
reuse. Unavailable measurements are null. Telemetry never supplies commands.

Dependencies: existing btop/fmt, vendored MIT nlohmann/json 3.12.0, pinned
LibreHardwareMonitorLib 0.9.6 (MPL-2.0), System.Management 10.0.2, and the Windows
SDK .NET projections for WinRT. `packages.lock.json` pins NuGet resolution.
Upstream btop's Apache-2.0 license and credits remain intact. See the dependency
package metadata for transitive notices; the sensor library is used without
modifying its source.

## Validation

Built the C++ application and Windows agent, launched the actual Windows .exe
directly from WSL, and validated 35 live frames through the C++ decoder.
Native PTY/terminal-state tests cover full-PC/WSL switching, device/sensor pages,
paired-device filtering, sorting, Linux filtering, hidden process panel,
compact layouts, resize, missing agent, Linux-only mode and shutdown.

```bash
g++ -std=c++23 -O1 -Wall -Wextra -Isrc -Iinclude \
  tests/host_bridge_test.cpp src/btop_host_bridge.cpp src/btop_host_ipc.cpp \
  -o obj/host_bridge_test
./obj/host_bridge_test
python3 tests/host_tui_smoke.py --live
```

Decoder tests include missing sensors, 0% battery, invalid battery/CPU/memory,
timestamp bounds, atomic rejection, CPU deltas, PID reuse, frame fragmentation,
size/depth limits and shutdown. Make was tested; CMake's source list was updated
but CMake was not run here. Battery handling has fixture coverage, not a live
earbud battery measurement. Other connector types and hotplug hardware still
need physical-device testing.

During a short 1-second-refresh sample after startup, the agent used about 5%
of one logical CPU (~0.31% across this 16-thread machine), with ~101 MiB working
set. This is a local sample, not a guarantee for every hardware configuration.

## API references

- [Windows physical memory](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex)
- [Bluetooth connected property](https://learn.microsoft.com/en-us/windows/win32/properties/props-system-devices-aep-isconnected)
- [Windows device battery percentage](https://learn.microsoft.com/en-us/windows/win32/properties/props-system-devices-batterylife)
- [Display connection technology](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ne-wingdi-displayconfig_video_output_technology)
- [Core Audio endpoint enumeration](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nn-mmdeviceapi-immdeviceenumerator)
- [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)
