#!/usr/bin/env python3
"""Fault-injection test: stream loss, stale state and automatic producer restart."""
import os, pathlib, pty, fcntl, termios, struct, subprocess, tempfile, time, select

root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    folder=pathlib.Path(tmp)
    (folder/'btop').mkdir()
    (folder/'btop/btop.conf').write_text('update_ms = 1000\nshow_gpu_info = "Off"\n')
    producer=folder/'producer'
    producer.write_text('''#!/usr/bin/python3
import json,time,pathlib,sys
counter=pathlib.Path(__file__).with_suffix('.count')
run=int(counter.read_text())+1 if counter.exists() else 1
counter.write_text(str(run))
started=time.monotonic()
for n in range(2 if run==1 else 100):
    t=time.monotonic()-started+1
    h={"cpu":{"usage":25,"name":"Fixture CPU","logical_processors":2,"uptime_seconds":100},
       "memory":{"total":8589934592,"available":4294967296},"networks":[],
       "inventory":{"sample_seconds":t,"status":"fixture","devices":[],"drives":[]},
       "hardware":{"sample_seconds":t,"status":"fixture","sensors":[]}}
    print(json.dumps({"schema_version":1,"source":"windows","sample_seconds":t,"processes":[],"host":h}),flush=True)
    time.sleep(1)
''')
    producer.chmod(0o755)
    master,slave=pty.openpty()
    fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',40,140,0,0))
    p=subprocess.Popen([str(root/'wintop')],stdin=slave,stdout=slave,stderr=slave,start_new_session=True,
        env={**os.environ,'TERM':'xterm-256color','WINTOP_WINDOWS':'1','WINTOP_AGENT':str(producer),
             'WINTOP_CONFIG_HOME':tmp,'WINTOP_STATE_HOME':tmp})
    output=bytearray()
    def drain(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([master],[],[],.05)[0]:
                try: data=os.read(master,65536)
                except OSError: break
                if not data: break
                output.extend(data)
    try:
        drain(8)
        assert p.poll() is None
        stale=output.find(b'Host: Stale')
        assert stale>=0,'Stream termination was not shown as stale'
        assert b'Host: Connected' in output[stale:],'Producer did not reconnect'
        assert int(producer.with_suffix('.count').read_text())>=2
        os.write(master,b'v');drain(.4)
        assert b'WSL memory / disks' in output or b'Linux memory / disks' in output
        os.write(master,b'q');drain(2);p.wait(timeout=4)
        assert p.returncode==0
        print('PASS: stream loss, stale status, automatic restart, WSL fallback and clean exit')
    finally:
        if p.poll() is None: p.kill();p.wait()
        os.close(master);os.close(slave)
