#!/usr/bin/env python3
"""Exercise gate-game-content against a loopback range server, outside any player install.
Usage: gate-game-content.py COMPILED_GATE NEW_SCRATCH_DIRECTORY
"""
from pathlib import Path
import hashlib,http.server,threading,subprocess,sys,time
assert len(sys.argv)==3
root=Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=False)
body=bytes(range(256))*32
class Server(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def do_GET(self):
  if self.path=='/slow':time.sleep(2)
  a,b=map(int,self.headers['Range'].removeprefix('bytes=').split('-'));data=body[a:b+1];self.send_response(206);self.send_header('Content-Range',f'bytes {a}-{b}/{len(body)}');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
s=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);threading.Thread(target=s.serve_forever,daemon=True).start();url=f'http://127.0.0.1:{s.server_port}/disc';sha=hashlib.sha256(body[:1024]).hexdigest()
media='OCMEDIA 1\nmodule ra3d\nmirror GDI '+url+' 0\n'
for comp,rel in [('movies','dosdata/movies/fixture.VQA'),('music','dosdata/SCORES.MIX'),('movies','ra3d/movies/fixture.VQA'),('music','ra3d/audio/SCORES.MIX')]:media+=f'file {comp} {rel} 1024 {sha} GDI 0\n'
lang='OCLANG 1\nmirror DE_GDI '+url+' 0\n'
for game,code,rel in [('td','de','dosdata/lang/de/CONQUER.ENG'),('ra','de','ra3d/lang/de/CONQUER.ENG'),('ra','fr','ra3d/lang/fr/CONQUER.ENG')]:lang+=f'pack {code} {game} {code}\nfile {code} {game} {rel} 1024 {sha} DE_GDI 0\n'
(root/'media.txt').write_text(media);(root/'languages.txt').write_text(lang);install=root/'install';install.mkdir(exist_ok=True)
(root/'bad-media.txt').write_text(media.replace(sha,'0'*64))
(root/'slow-media.txt').write_text(media.replace('/disc','/slow'))
try: subprocess.run([str(Path(sys.argv[1]).resolve()),str(install),str(root/'media.txt'),str(root/'languages.txt'),str(root/'bad-media.txt'),str(root/'slow-media.txt')],check=True)
finally:s.shutdown()
