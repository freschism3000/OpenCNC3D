#!/usr/bin/env python3
"""Native private/public runtime refusal and optional-media preservation fixtures."""
from pathlib import Path
import hashlib,os,re,subprocess,sys
assert len(sys.argv)==3
binary=Path(sys.argv[1]).resolve();work=Path(sys.argv[2]);work.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[2]
def put(d,n,b):
 p=d/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
def snapshot(d):
 return {p.relative_to(d).as_posix():(p.lstat().st_mode,p.lstat().st_size,p.lstat().st_mtime_ns,
         os.readlink(p)if p.is_symlink()else hashlib.sha256(p.read_bytes()).hexdigest())
         for p in d.rglob('*')if p.is_file()or p.is_symlink()}
def run(d,*args):
 r=subprocess.run([str(binary),str(d.resolve()),*args],capture_output=True,text=True,timeout=30)
 print(r.stdout,end='');print(r.stderr,end='');assert r.returncode==0,(args,r.returncode)
cases=[(b'private_developer 1\n',1),(b'private_developer\t1\r\n',1),
       (b'version 0.9.0-alpha+700c523\nprivate_developer 1',1),
       (b'version 0.9.0-alpha+700c523\n',0),(b'private_developer 0\n',0),
       (b'private_developer 10\n',0),(b'private_developer 1 extra\n',0),
       (b'# private_developer 1\n',0),(b'not_private_developer 1\n',0),
       (b'x'*255+b'private_developer 1\n',0),(b'x'*600+b'\nprivate_developer 1\n',1)]
for i,(record,want)in enumerate(cases):
 d=work/f'predicate-{i}';d.mkdir();put(d,'opencnc3d-install.txt',record);before=snapshot(d)
 run(d,'predicate',str(want));assert snapshot(d)==before
d=work/'linked-record';d.mkdir();(d/'opencnc3d-install.txt').symlink_to(work/'predicate-0/opencnc3d-install.txt');before=snapshot(d);run(d,'predicate','0');assert snapshot(d)==before
needs=re.findall(r'"(ra3d/[^\"]+)"',(root/'launcher/lmodule.c').read_text().split('lmod_ra_needs[] = {',1)[1].split('};',1)[0]);needs=[n for n in needs if not n.endswith(('.exe','.dll'))]
for name,private in [('private',True),('public',False)]:
 d=work/name;d.mkdir()
 for n in [*needs,'opencnc3d','TiberianDawn.dylib','CONQUER.MIX','ra3d-fx.cfg','savegame.sav','opencnc3d-update.zip','opencnc3d-update.zip.part','cancel-sentinel']:
  put(d,n,b'KEEP ORIGINAL PRIVATE/PLAYER BYTES\n')
 put(d,'opencnc3d-install.txt',b'version 0.9.0-alpha+700c523\n'+(b'private_developer 1\n'if private else b''))
 before=snapshot(d);run(d,name)
 if private:assert snapshot(d)==before,'Private refusal mutated install/cache/cancel/settings/saves'
 else:
  after=snapshot(d);assert all(after[p]==r for p,r in before.items()),'Public control touched original files'
 # Media gets an independent fixture; approved runtime/player inputs remain exact.
 if private:
  put(d,'ra3d/movies/fixture.VQA',b'PINNED MOVIE');put(d,'ra3d/audio/SCORES.MIX',b'PINNED MUSIC')
  manifest=work/'media.txt';text='OCMEDIA 1\nmodule ra3d\nmirror GDI http://127.0.0.1:9/disc 0\n'
  for component,n in [('movies','ra3d/movies/fixture.VQA'),('music','ra3d/audio/SCORES.MIX')]:
   b=(d/n).read_bytes();text+=f'file {component} {n} {len(b)} {hashlib.sha256(b).hexdigest()} GDI 0\n'
  manifest.write_text(text);before=snapshot(d);run(d,'media',str(manifest.resolve()));after=snapshot(d)
  assert all(after[p]==r for p,r in before.items()),'Media control changed original runtime/player bytes'
  assert set(after)-set(before)=={'opencnc3d-media-ra.txt'}
  before=snapshot(d);original=d/'ra3d/opencnc3d';original.rename(original.with_name(original.name+'.module-removing'))
  put(d,'opencnc3d-module-removal.txt',b'1 0\n');run(d,'recovery')
  assert snapshot(d)==before,'Private recovery failed to restore exact interrupted module staging'
print('PRIVATE-SOURCE-GATE|PASS|12_record_cases|private_zero_HTTP_and_full_snapshot_exact|public_transport_positive|optional_media_and_recovery_allowed')
