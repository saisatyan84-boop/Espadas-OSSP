#!/bin/sh
# Build the native C Windows agent from Ubuntu/WSL.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec make -C "$root/windows-native" "$@"
