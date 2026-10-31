# Wintop C — Windows + WSL Terminal Monitor

This version implements the terminal client **and the Windows companion agent in C11**.
It is independent of the original C++/C# version in `reference_clone`.
The active build uses GCC, MinGW-w64, ncurses, cJSON and native Windows APIs.
It does not build or launch C++ code, .NET, or PowerShell.

## Build and run

In Ubuntu WSL:

```sh
cd "Project folder/reference_clone_C"
make
./wintop
```

Prerequisites, if building on another Ubuntu installation:

```sh
sudo apt-get update
sudo apt-get install build-essential libncurses-dev gcc-mingw-w64-x86-64-posix
```

The Linux client is compiled with `gcc -std=c11`; the Windows executable is
cross-compiled with `x86_64-w64-mingw32-gcc -std=c11`. MinGW-w64 is a compiler
toolchain, not a runtime agent. No .NET SDK/runtime is needed.

```sh
./wintop --once                       # Text snapshot
./wintop --interval-ms 1500           # Configurable 1–60 second interval
WINTOP_WINDOWS=0 ./wintop             # Linux-only mode
./wintop --agent /mnt/c/path/sysmon-host.exe
```

Launchers resolve their own location; running from another working directory
also works. Use a terminal at least 120 columns by 32 rows for the dashboard.
A smaller terminal displays a resize message and still accepts q.

## Features and boundaries

| Area | C implementation |
| --- | --- |
| Windows CPU | Aggregate usage from GetSystemTimes deltas; PDH per-core load and Windows-reported frequency |
| Windows RAM | GlobalMemoryStatusEx physical total and available RAM; already includes memory backing WSL |
| Windows processes | Toolhelp enumeration, GetProcessTimes CPU deltas, working-set RAM where accessible |
| WSL/Linux | /proc CPU, memory, swap, load, process statistics; root filesystem capacity; aggregate non-loopback traffic |
| Process dashboard | Separate Windows and WSL tables shown side by side with P; CPU/RAM/PID sorting and scrolling |
| GPU | NVIDIA NVML through its C ABI: load, temperature, clocks, VRAM, power when supported |
| Storage | Fixed/removable volume capacity; PDH aggregate physical-disk read/write rates |
| Network | Physical adapters, state, IPv4 and per-adapter traffic deltas; select with [ and ] |
| USB/input/storage hardware | Present-device enumeration through SetupAPI |
| Bluetooth | Passive classic Bluetooth cache enumeration with actual connected/paired flags; no active inquiry |
| Audio | Core Audio COM interfaces called from C; active endpoints and default console output/microphone |
| Displays | Active DisplayConfig paths with resolution, refresh rate and reported connector technology |
| Reliability | Bounded JSON frames/lists, schema validation, null readings, stale detection and agent retries |

**This is not feature-equivalent to the old btop-based version.** CPU/package,
SSD and motherboard temperatures, CPU power, fan RPM, voltages, Bluetooth battery
and full BLE connection tracking are not implemented in this C version. CPU
temperature and package power are exposed as null readings; use u in sensors
to reveal unavailable entries. NVIDIA metrics require a compatible installed
NVIDIA driver exposing NVML; AMD/Intel GPU sensor backends are not implemented.
No raw hardware driver or administrator access is required by this version.

Windows-reported CPU frequency is an operating-system counter, not a measurement
of effective per-core clocks. Present PnP devices may include internal components
and multiple functions of one physical peripheral. Bluetooth hardware rows marked
Present are not claims of an active Bluetooth connection. BLE devices may appear
as PnP hardware without a reliable connection state. Battery is N/A.
DisplayPort alone cannot identify a physical USB-C connector.
Network filters/virtual interfaces are excluded on the Windows side; WSL traffic
sums non-loopback interfaces and may double-count traffic on routed configurations.

Windows process CPU is normalized to 0–100% of the whole logical CPU capacity.
The first process CPU sample is unavailable until a second sample arrives.
Protected processes may have unavailable CPU/RAM. Working-set memory across
processes should not be summed to estimate physical RAM use.
Windows host PIDs are never passed to Linux kill, nice or process controls.
Process mutation and the old btop process tree are not implemented.

## Controls

| Key | Action |
| --- | --- |
| v | Switch overview between Windows host and WSL |
| P | Open/close the side-by-side process tables |
| S | Open/close sensors |
| d | Open/close devices |
| c / m / p | Sort processes by CPU / RAM / PID |
| arrows / j / k / Page Up / Page Down / Home | Scroll lists |
| [ / ] | Select Windows network adapter |
| u | Show/hide null sensor values |
| b | Show/hide paired but disconnected classic Bluetooth devices |
| Tab | Return to overview or switch overview source |
| r | Restart/retry the host agent |
| q | Quit |

## Architecture and OS concepts

```text
WSL: C ncurses client                         Windows: native C agent
/proc and filesystem collectors              Win32 / PDH / SetupAPI / NVML
           |                                              |
           +---- client data models <--- JSON stdout -----+
                          |
                  terminal dashboard
```

The client launches an independent Windows process with fork/exec and reads a
nonblocking anonymous pipe. There is no shell command construction.
Each line is one UTF-8 JSON snapshot with schema_version 1, source windows,
sample_seconds, processes and host (CPU, memory, networks, inventory, hardware).

The client uses cJSON to decode and validate frames. It rejects malformed
messages, unsupported schemas and impossible physical memory totals. Process
creation time distinguishes Windows PID reuse. Lists and frame sizes are bounded.
Incoming control characters are removed from display strings.

System/GPU samples default to two seconds. Device inventory and disk-I/O counters
refresh every ten seconds; the inventory is cached between refreshes. Native
inventory collection currently runs synchronously and may delay a stream frame
on a slow device/driver. Independent inventory workers/events remain future work.
The client treats telemetry older than ten seconds as unavailable and retries
the agent after failures. Refreshes above ten seconds need a correspondingly
larger stale threshold; the implementation accounts for the selected interval.

The UI redraws on new samples, keys and resize events. It avoids unnecessary
full redraws while idle. SIGINT/SIGTERM trigger terminal and child-process cleanup.
Agent failure never prevents use of Linux-only mode. Missing data is not invented.

## Source map

```text
reference_clone_C/
  Makefile, wintop              Build/launch entry points
  btop/
    Makefile, CMakeLists.txt    C client builds
    src/
      main.c                   Event loop, CLI, signals and input
      wintop.h                 Shared C structures
      host_ipc.c               Agent launch, framing, JSON validation and retry
      linux_sys.c              Linux collectors
      ui.c                     ncurses panels and formatting
      cJSON.c                  Vendored C JSON library
    include/cJSON.h
    windows-native/
      main.c                   JSON stream and sampling loop
      system.c                 CPU, RAM and process collectors
      cpu_sensors.c            PDH per-core load and reported frequency
      network.c                Windows adapter traffic
      devices.c                PnP and passive classic Bluetooth
      displays.c               DisplayConfig collector
      audio.c                  Core Audio collector
      storage.c                Capacity and disk counters
      sensors.c                Optional NVIDIA NVML backend
      host.h, Makefile
      publish/sysmon-host.exe  Built native Windows agent
    tests/                     C parser, PTY and live stream checks
```

The directory name btop is retained from the copied checkout, but its active
sources and build are C. The original application's C++/C# files and obsolete
tests/build files were moved out to the sibling
`reference_clone_C_legacy_backup_20261031` directory for recovery. The original
`reference_clone/btop` project was not edited during this conversion.
The inherited Git history still records upstream btop; history is not compiled.

## Verification

```sh
make -C btop test
make -C btop test-live
```

The first command checks JSON validation, null readings, CPU normalization,
PID reuse, process views, keyboard handling, terminal resizing and clean quit.
The second validates several real native Windows frames inside WSL.
These checks do not establish support for hardware that was not available to test.

To confirm the implementation language, inspect the Makefiles and source files.
`ldd btop/bin/wintop` lists the Linux runtime libraries; it does not require
libstdc++. The Windows executable imports Windows native libraries, not the CLR.

## Attribution

Original C port additions use MIT; see btop/LICENSE-C. cJSON retains its MIT
notice in its source/header. The retained btop Apache-2.0 license and upstream
notices still apply to their respective copied materials. The current monitor
is a C rewrite inspired by btop's concepts, rather than a claim that the original
btop code was written in C.

Technical API references:
- [Windows GetSystemTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemtimes)
- [BluetoothFindFirstDevice](https://learn.microsoft.com/en-au/windows/win32/api/bluetoothapis/nf-bluetoothapis-bluetoothfindfirstdevice)
- [NVIDIA NVML](https://developer.nvidia.com/management-library-nvml)
