#!/usr/bin/env python3
"""Test the real gateway with PHP and a disposable loopback HTTP upstream."""
import fcntl
import http.server
import pathlib
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request

source = pathlib.Path(__file__).with_name('http-gateway.php').read_text()
seen = []
upstream_status = 200
class Backend(http.server.BaseHTTPRequestHandler):
    def do_GET(self): self.answer()
    def do_POST(self): self.answer()
    def answer(self):
        global upstream_status
        seen.append((self.path, self.command, dict(self.headers), self.rfile.read(int(self.headers.get('Content-Length','0')))))
        body = b'upstream-ok'
        self.send_response(upstream_status)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Content-Type','application/octet-stream')
        self.end_headers(); self.wfile.write(body)
    def log_message(self,*args): pass
backend=http.server.ThreadingHTTPServer(('127.0.0.1',0),Backend)
threading.Thread(target=backend.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory(prefix='dune-gateway-') as tmp:
    root=pathlib.Path(tmp); private=root/'private';private.mkdir();(private/'slots').mkdir()
    key='a'*64;(private/'gateway.key').write_text(key+'\n')
    for i in range(16):(private/'slots'/str(i)).touch()
    assert source.count("'/var/www/data/dunecity-relay'")==1
    assert source.count('http://127.0.0.1:18787')==1
    gateway=source.replace("'/var/www/data/dunecity-relay'",repr(str(private))).replace('http://127.0.0.1:18787',f'http://127.0.0.1:{backend.server_port}')
    (root/'gateway.php').write_text(gateway)
    (root/'router.php').write_text("<?php $_SERVER['HTTPS']='on'; require __DIR__.'/gateway.php';")
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    log=(root/'php.log').open('w')
    process=subprocess.Popen([shutil.which('php'),'-S',f'127.0.0.1:{port}',str(root/'router.php')],stdout=log,stderr=log)
    def request(path, body=b'', method='POST', headers=None):
        req=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=body if method=='POST' else None,method=method,
            headers={'Content-Type':'application/octet-stream',**(headers or {})})
        try:
            with urllib.request.urlopen(req,timeout=5) as r:return r.status,r.read()
        except urllib.error.HTTPError as e:return e.code,e.read()
    try:
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1',port),.1):break
            except OSError:time.sleep(.05)
        health = request('/relay/health',method='GET')
        assert health == (200,b'upstream-ok'), health
        assert seen[-1][1]=='GET'
        assert seen[-1][0]=='/v1/health'
        assert request('/relay/v1/poll/open')==(200,b'upstream-ok')
        assert request('/relay/v1/admission/list',body=b'app=dunecity',headers={'Content-Type':'application/x-www-form-urlencoded'})[0]==200
        for path in ['/relay/evil','/relay/v1/poll/open?url=http://evil','/relay/v1/poll/%6fpen','/relay/v1/poll/open/extra']:
            assert request(path)[0]==404,path
        assert request('/relay/v1/poll/open',method='GET')[0]==405
        assert request('/relay/v1/poll/open',headers={'Origin':'https://evil.example'})[0]==403
        assert request('/relay/v1/poll/open',headers={'Origin':'null'})[0]==403
        before=len(seen)
        assert request('/relay/v1/poll/exchange',headers={'X-Dune-Session':'invalid'})[0]==400
        assert len(seen)==before
        headers={'Origin':'https://dunelegacy.com','X-Dune-Session':'b'*64,'X-Forwarded-For':'1.2.3.4',
            'Forwarded':'for=1.2.3.4','X-Dune-Gateway':'spoof','Authorization':'secret','Cookie':'secret'}
        assert request('/relay/v1/poll/exchange',b'binary\x00body',headers=headers)[0]==200
        forwarded={k.lower():v for k,v in seen[-1][2].items()}
        assert forwarded['x-forwarded-for']=='127.0.0.1'
        assert forwarded['x-dune-gateway']==key
        assert forwarded['x-dune-session']=='b'*64
        assert all(k not in forwarded for k in ['authorization','cookie','forwarded'])
        assert seen[-1][3]==b'binary\x00body'
        upstream_status=302
        assert request('/relay/v1/poll/open')[0]==502
        upstream_status=200
        locks=[]
        for i in range(12):
            f=(private/'slots'/str(i)).open('r+');fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB);locks.append(f)
        before=len(seen)
        assert request('/relay/v1/poll/exchange')[0]==503
        assert len(seen)==before
        assert request('/relay/v1/poll/open')[0]==200
        for f in locks:f.close()
        assert request('/relay/v1/poll/open')[0]==200
        assert request('/relay/v1/poll/open',b'x')[0]==413
        assert request('/relay/v1/poll/exchange',b'x'*1048577)[0]==413
        assert request('/relay/v1/poll/open',headers={'Content-Type':'text/plain'})[0]==415
        print('PASS: PHP fixed routes, methods, Origin, spoofed headers, binary forwarding, upstream redirects, size/type bounds, concurrency refusal/recovery')
    finally:
        process.terminate();process.wait(timeout=5);log.close()
backend.shutdown();backend.server_close()
