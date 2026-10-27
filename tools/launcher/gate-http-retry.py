#!/usr/bin/env python3
"""Private native HTTP/DNS checks; no game/window or installed folder is used."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading

requests = []
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append(self.path)
        body = b'RECOVERED\n'
        self.send_response(503 if self.path == '/unavailable' else 200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *args):
        pass

assert len(sys.argv) == 2
binary = str(Path(sys.argv[1]).resolve())
server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
host = 'http://127.0.0.1:' + str(server.server_port)
# Avoid an environment proxy turning the deliberate .invalid DNS failure into
# a proxy HTTP response. This modifies only each fixture process's environment.
env = {key: value for key, value in os.environ.items() if not key.lower().endswith('_proxy')}
env['NO_PROXY'] = '*'
env['no_proxy'] = '*'
blocked = socket.socket()
blocked.bind(('127.0.0.1', 0))
cases = [(1, host + '/ok', 2, 1), (2, host + '/ok', 2, 1),
         (3, host + '/ok', 2, 0), (4, host + '/ok', 1, 0),
         (5, host + '/ok', 1, 0), (0, host + '/unavailable', 1, 1),
         (6, 'http://opencnc3d-unresolvable.invalid/builds', 2, 0),
         (7, 'http://127.0.0.1:' + str(blocked.getsockname()[1]) + '/builds', 2, 0)]
try:
    for mode, url, attempts, hits in cases:
        if mode == 7:
            # A bound, non-listening socket can silently hold SYNs on macOS.
            # Close the reservation immediately before the refused-port check.
            blocked.close()
        before = len(requests)
        run = subprocess.run([binary, str(mode), url, str(attempts)], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=45)
        print(run.stdout, end='')
        assert run.returncode == 0, (mode, run.returncode)
        assert len(requests) - before == hits, (mode, requests[before:])
        if mode in (1, 2, 3, 6, 7):
            assert 'network: libcurl error ' in run.stdout
            assert 'retrying once' in run.stdout
        if mode in (4, 5, 0):
            assert 'retrying once' not in run.stdout
finally:
    blocked.close()
    server.shutdown()
    server.server_close()
print('HTTP-RETRY-MATRIX|PASS|cases=8|real_loopback_recovery=1|real_invalid_DNS=1|real_refused_connect=1|only_resolve_connect_retried=1|raw_diagnostics_retained=1')
