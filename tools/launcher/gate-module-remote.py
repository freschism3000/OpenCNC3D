#!/usr/bin/env python3
"""CPU-only native module route over real loopback HTTP, no game/window."""
import hashlib,json,re,stat,subprocess,threading,zipfile,importlib.util,os
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import argparse
p=argparse.ArgumentParser(description=__doc__);p.add_argument('gate',type=Path);p.add_argument('scratch',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];work=a.scratch;work.mkdir(parents=True,exist_ok=False)
version=re.search(r'#define LCFG_BUILD "([^"]+)"',(root/'launcher/lcfg.h').read_text())[1]
release_version=(root/'VERSION').read_text().strip()
menus=([Path(os.environ['OPENCNC3D_RA_SOURCE'])/'dosmenu.pack'] if os.environ.get('OPENCNC3D_RA_SOURCE') else [])+[root/'ra3d/dosmenu.pack',root/'playable/ra3d/dosmenu.pack',Path.home()/'CNC3D-latest/playable/ra3d/dosmenu.pack',Path.home()/'CNC3D/playable/ra3d/dosmenu.pack'];menu=next(x for x in menus if x.is_file()).read_bytes()
(work/'dosmenu.pack').write_bytes(menu)
for rel in ('savegame.sav','ra3d-fx.cfg','ra3d/missions/custom-map.ini'):
 f=work/rel;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('KEEP EXACT\n')
needs=re.findall(r'"(ra3d/[^\"]+)"',(root/'launcher/lmodule.c').read_text().split('lmod_ra_needs[] = {',1)[1].split('};',1)[0]);needs=[n for n in needs if not n.endswith(('.exe','.dll'))]
ra={n:(menu if n=='ra3d/dosmenu.pack' else ('fixture '+n).encode()) for n in needs};ra.update({'ra3d/einf/E1.c3m':b'Enhanced infantry','ra3d/ebld/manifest.json':b'{}','efx.pack':b'shared effects','einf/SHARED.c3m':b'shared infantry'})
td={'opencnc3d':b'frontend','TiberianDawn.dylib':b'brain','CONQUER.MIX':b'data','einf/SHARED.c3m':b'shared infantry'}
spec=importlib.util.spec_from_file_location('module_producer',root/'tools/launcher/package-modules.py');producer=importlib.util.module_from_spec(spec);spec.loader.exec_module(producer)
def package(code,files,extra_manifest=''):
 if not extra_manifest:
  staged=work/f'{code}-stage';staged.mkdir(exist_ok=True);inputs={}
  for rel,data in files.items():
   path=staged/rel;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);path.chmod(0o755);inputs[rel]=path
  output=work/f'producer-{code}.zip';producer.write_archive(inputs,output,code,'macos',release_version)
  # The CPU gate uses the exact launcher build string, including dev suffixes.
  # Release identity is replaced below solely for an untagged development gate.
  if release_version==version:return output.read_bytes()
 path=work/f'{code}-{len(files)}-{bool(extra_manifest)}.zip'
 manifest=f'module-files 1\nmodule {code}\nversion {version}\nplatform macos\n'+''.join(f'file {hashlib.sha256(data).hexdigest()} {len(data)} {name}\n' for name,data in files.items())+extra_manifest
 with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
  for name,data in dict(files,**{f'opencnc3d-module-{code}.txt':manifest.encode()}).items():
   zi=zipfile.ZipInfo(name);zi.external_attr=(stat.S_IFREG|0o755)<<16;zi.compress_type=zipfile.ZIP_DEFLATED;z.writestr(zi,data)
 return path.read_bytes()
assets={2:package('td',td),3:package('ra',ra),8:package('ra',ra,f'file {"0"*64} 0 ra3d-fx.cfg\n')}
(work/'bad-meta.txt').write_text(f'{len(assets[8])} {hashlib.sha256(assets[8]).hexdigest()}\n')
catalog='modules 1\nversion '+version+'\n'+''.join(f'module {code} macos OpenCNC3D-module-{code}-macos-v{version}.zip {len(assets[i])} {hashlib.sha256(assets[i]).hexdigest()}\n' for code,i in [('td',2),('ra',3)])
assets[9]=package('ra',ra,f'file {"0"*64} 0 ra3d/einf/PHANTOM.c3m\n')
(work/'phantom-meta.txt').write_text(f'{len(assets[9])} {hashlib.sha256(assets[9]).hexdigest()}\n')
assets[1]=catalog.encode();builds=json.dumps({'ok':True,'latest':{'tag':'v'+version,'assets':[{'id':1,'name':f'OpenCNC3D-v{version}-modules.txt','size':len(assets[1])}]+[{'id':i,'name':f'OpenCNC3D-module-{code}-macos-v{version}.zip','size':len(assets[i])} for code,i in [('td',2),('ra',3)]]}}).encode()
(work/'builds.json').write_bytes(builds)
requests=[]
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  requests.append((self.path,self.headers.get('Range')))
  if self.path=='/disc':data=bytes(range(256))*32
  elif self.path=='/api/builds':data=builds
  elif self.path.startswith('/api/download?asset='):data=assets[int(self.path.rsplit('=',1)[1])]
  else:self.send_error(404);return
  r=self.headers.get('Range');status=200;start=0;end=len(data)-1
  if r:
   m=re.fullmatch(r'bytes=(\d+)-(\d+)',r);assert m;start,end=map(int,m.groups());status=206
  self.send_response(status)
  if r:self.send_header('Content-Range',f'bytes {start}-{end}/{len(data)}')
  body=data[start:end+1];self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
media=f'OCMEDIA 1\nmodule ra3d\nmirror GDI http://127.0.0.1:{server.server_port}/disc 0\n'
body=bytes(range(256))*4;sha=hashlib.sha256(body).hexdigest()
for component,rel in [('movies','dosdata/movies/fixture.VQA'),('music','dosdata/SCORES.MIX'),('movies','ra3d/movies/fixture.VQA'),('music','ra3d/audio/SCORES.MIX')]:
 media+=f'file {component} {rel} 1024 {sha} GDI 0\n'
(work/'media.txt').write_text(media)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
 result=subprocess.run([str(a.gate.resolve()),str(work.resolve()),f'http://127.0.0.1:{server.server_port}',str(work/'media.txt')],text=True,capture_output=True);print(result.stdout,end='');print(result.stderr,end='');result.check_returncode()
 assert 'LBROWSER|i=1|name=RA3D|installed=0|status=Install|grey=0|enabled=1' in result.stdout
 assert 'LBROWSER|i=1|name=RA3D|installed=1|status=Update|grey=0|enabled=1' in result.stdout
 assert result.stdout.count('LCHK|label=DOWNLOAD MOVIES|checked=1')>=2
 assert result.stdout.count('LCHK|label=DOWNLOAD MUSIC|checked=1')>=2
 assert result.stdout.count('LAPPLY|movies=1|music=1|asks=0|game=1|enabled=1')>=2
finally:server.shutdown()
for rel in ('savegame.sav','ra3d-fx.cfg','ra3d/missions/custom-map.ini'):assert (work/rel).read_text()=='KEEP EXACT\n'
assert any(r for _,r in requests);print('MODULE-REMOTE-HTTP|PASS|requests',len(requests))
