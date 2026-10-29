#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dotnet='/mnt/c/Program Files/dotnet/dotnet.exe'
if [ ! -f "$dotnet" ]; then
    printf '%s\n' 'Install the Windows .NET 9 SDK, or build scripts/build-host.ps1 from Windows.' >&2
    exit 1
fi
"$dotnet" publish "$(wslpath -w "$root/windows-agent/SysmonHost.csproj")" -c Release -r win-x64 --self-contained false -o "$(wslpath -w "$root/windows-agent/publish")" -p:RestoreLockedMode=true
chmod +x "$root/windows-agent/publish/sysmon-host.exe"
