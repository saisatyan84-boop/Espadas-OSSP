#!/usr/bin/env python3
"""WSL host panel integration tests using only Python's standard library."""
import os, pathlib, pty, fcntl, termios, struct, subprocess, tempfile, time, select, signal
root=pathlib.Path(__file__).resolve().parents[1]
def run(extra, live=False):
    with tempfile.TemporaryDirectory() as tmp:
        config=pathlib.Path(tmp)/'btop'; config.mkdir()
        (config/'btop.conf').write_text('update_ms = 1000\nshow_gpu_info = "Off"\n')
        master,slave=pty.openpty()
        def size(rows,cols):
            fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',rows,cols,0,0))
        size(45,150)
        env={**os.environ,'TERM':'xterm-256color','WINTOP_WINDOWS':'1',
             'WINTOP_CONFIG_HOME':tmp,'WINTOP_STATE_HOME':tmp,**extra}
        p=subprocess.Popen([str(root/'wintop')],stdin=slave,stdout=slave,stderr=slave,
                           env=env,start_new_session=True)
        output=bytearray()
        def drain(seconds):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                if select.select([master],[],[],.05)[0]:
                    try: chunk=os.read(master,65536)
                    except OSError: break
                    if not chunk: break
                    output.extend(chunk)
        try:
            drain(1.5)
            if live:
                # A cold Windows/.NET launch can take more than four seconds.
                # Wait for the state we need, with a bounded startup deadline.
                deadline=time.monotonic()+15
                while b'Host: Connected' not in output and p.poll() is None and time.monotonic()<deadline:
                    drain(.25)
            assert p.poll() is None,output.decode(errors='replace')
            if extra.get('WINTOP_WINDOWS')!='0':
                assert b'whole PC' in output
                assert b'windows processes' in output
                assert (b'Host: Connected' if live else b'Host: Unavailable') in output,output[-3000:].decode(errors='replace')
                for key in [b'\t',b'm',b'c',b'p',b'\x1b[B',b'\x1b[A',b't',b's',b'N']:
                    os.write(master,key); drain(.1)
            else:
                assert b'windows processes' not in output and b'Program:' in output
            for rows,cols in [(24,90),(16,60),(45,150)]:
                size(rows,cols); os.kill(p.pid,signal.SIGWINCH); drain(.3)
            os.write(master,b'q'); drain(1); p.wait(timeout=6)
            assert p.returncode==0
        finally:
            if p.poll() is None: p.kill(); p.wait()
            os.close(master); os.close(slave)
run({'WINTOP_AGENT':'/missing/windows-producer'})
print('PASS: unavailable host, native panel controls and resize')
run({'WINTOP_WINDOWS':'0'})
print('PASS: disabled host, native Linux monitor')
if '--live' in __import__('sys').argv:
    run({},True)
    print('PASS: actual Windows bridge in the native btop UI')
