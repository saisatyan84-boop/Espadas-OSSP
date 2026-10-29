#!/usr/bin/env python3
"""Regression coverage for first-run paths and 16/256/true-color host rendering."""
import fcntl
import os
import pathlib
import pty
import select
import struct
import subprocess
import tempfile
import termios
import time
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(case):
    with tempfile.TemporaryDirectory(prefix='wintop-startup-') as tmp:
        base = pathlib.Path(tmp)
        home = base / 'home'
        home.mkdir()
        config = home / '.config/wintop'
        settings = {
            'first_run': None,
            'tty_theme': 'color_theme = "TTY"\n',
            'forced_tty': 'force_tty = True\n',
            'lowcolor': 'truecolor = False\n',
            'default_theme': 'color_theme = "Default"\n',
        }[case]
        if settings is not None:
            (config / 'btop').mkdir(parents=True)
            (config / 'btop/btop.conf').write_text(settings + 'update_ms = 1000\n')

        # A stable local telemetry fixture makes theme coverage independent of Windows.
        agent = base / 'agent'
        agent.write_text('''#!/usr/bin/python3
import json,time
t=0
while True:
    t+=1
    rows=[dict(pid=p,name="fixture"+str(p),ram_bytes=1048576,
               cpu_seconds=t*.05,start_id=str(p),sample_seconds=t) for p in range(100,110)]
    print(json.dumps(dict(schema_version=1,source="windows",sample_seconds=t,processes=rows)),flush=True)
    time.sleep(.25)
''')
        agent.chmod(0o755)
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('WINTOP_', 'XDG_CONFIG_HOME', 'XDG_STATE_HOME'))}
        env.update(HOME=str(home), TERM='xterm-256color', WINTOP_WINDOWS='1', WINTOP_AGENT=str(agent))
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH',45,150,0,0))
        proc = subprocess.Popen([str(ROOT / 'wintop')], stdin=slave, stdout=slave,
                                stderr=slave, env=env, start_new_session=True)
        output = bytearray()

        def drain(seconds):
            end = time.monotonic() + seconds
            last = time.monotonic()
            while time.monotonic() < end or time.monotonic() - last < .1:
                if select.select([master], [], [], .05)[0]:
                    try:
                        data = os.read(master, 65536)
                    except OSError:
                        break
                    if not data:
                        break
                    last = time.monotonic()
                    output.extend(data)

        try:
            drain(5)
            assert proc.poll() is None, output[-2500:].decode(errors='replace')
            assert b'fixture100' in output and b'fixture101' in output, 'Host process rows were not rendered'
            assert b'Could not determine config path' not in output, 'Fresh config directory was not created'
            assert b'Logging is disabled' not in output
            os.write(master, b'\tmcp')
            drain(.4)
            os.write(master, b'q')
            drain(1)
            proc.wait(timeout=4)
            assert proc.returncode == 0, output[-2500:].decode(errors='replace')
            assert (config / 'btop/btop.conf').is_file(), 'Config did not persist on clean exit'
            assert (home / '.local/state/wintop').is_dir()
            print('PASS:', case, '- process rows, controls, persistent config, clean exit')
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            os.close(master)
            os.close(slave)


for case in sys.argv[1:] or ['first_run', 'tty_theme', 'forced_tty', 'lowcolor', 'default_theme']:
    run(case)
