# Espadas OSSP

**Operating systems coursework and a Windows + WSL system monitor.**

This repository brings together operating systems practicals, skill exercises, assignments and **Wintop**, a terminal monitor built from btop. Wintop connects a Linux/WSL dashboard to a persistent Windows companion agent so you can inspect your host PC and WSL environment in one place.

## Wintop: Windows and WSL in one terminal

The dashboard keeps Windows and WSL process lists separate and can show:

- Windows host and WSL CPU, memory, storage and network information.
- Hardware sensors, including temperatures, clocks, power and fan speeds when available.
- Windows GPU information and connected Bluetooth, USB, audio, display, network and storage devices.
- btop's graphs, themes and interactive terminal interface.

Sensor readings and device details depend on Windows permissions, hardware and driver support. Unsupported values appear as unavailable. Bluetooth battery information is shown only when Windows reports it; it has not been verified with connected earbuds.

### Get started

Use Ubuntu on WSL with a C++23-capable compiler, the btop build dependencies and the Windows .NET 9 SDK and runtime. From a clone of this repository:

```bash
cd "Project folder/reference_clone/btop"
sh scripts/build-host.sh
make -j2
./wintop
```

The Windows agent build may need internet access to restore its .NET packages. For dependencies, detailed setup, controls, and hardware limitations, see [WINTOP.md](Project%20folder/reference_clone/btop/WINTOP.md) and the [upstream btop README](Project%20folder/reference_clone/btop/README.md).

| Key | Action |
| --- | --- |
| `v` | Switch between Windows host and WSL views |
| `d` | Open connected devices |
| `S` | Open hardware sensors |
| `P` | Show process lists |
| `Tab` | Switch process focus |
| `q` | Quit |

## Repository contents

| Folder | Contents |
| --- | --- |
| [Project folder](Project%20folder/) | Wintop source, project abstract and presentation |
| [Espadas_practical](Espadas_practical/) | Operating systems practical work |
| [Espadas_skill](Espadas_skill/) | Skill exercises |
| [ForgeOS](ForgeOS/) | ForgeOS material |
| [Nptel_Assignments](Nptel_Assignments/) | NPTEL assignments |

## License and acknowledgements

Original work by the repository owner is offered under the [MIT License](LICENSE). Third-party material retains its own license and copyright notices. In particular, Wintop is based on [btop](https://github.com/aristocratos/btop), distributed under [Apache License 2.0](Project%20folder/reference_clone/btop/LICENSE); its bundled dependencies have their own terms. The MIT license does not replace those licenses.
