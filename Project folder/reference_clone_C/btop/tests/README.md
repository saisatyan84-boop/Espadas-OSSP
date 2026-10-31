# C verification

Run `make test` in btop/ for C parser and pseudo-terminal checks.
Run `make test-live` inside WSL for live native Windows agent checks.

The parser checks malformed/versioned messages, invalid physical memory totals,
missing CPU values, CPU normalization and PID reuse. The terminal check exercises
the process view, keyboard input, resize handling, and clean quit in Linux-only
mode. The live test requires a Windows host and WSL interoperability.

These tests do not verify unavailable hardware sensors or every Bluetooth device.
