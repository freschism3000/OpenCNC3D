#!/usr/bin/env python3
"""Actual headless native media CLI, tiny pinned existing files, no HTTP/game/UI.
Usage: test_additive_media.py PATH_TO_NEW_NATIVE_LAUNCHER
"""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3]
assert len(sys.argv)==2
launcher=Path(sys.argv[1]).resolve();assert launcher.is_file()and b'--media-add-only'in launcher.read_bytes()
body=bytes(range(256))*4;digest=hashlib.sha256(body).hexdigest()
needs=re.findall(r'"(ra3d/[^\"]+)"',(ROOT/'launcher/lmodule.c').read_text().split('lmod_ra_needs[] = {',1)[1].split('};',1)[0]);needs=[n for n in needs if not n.endswith(('.exe','.dll'))]
paths=[('movies','dosdata/movies/existing.VQA'),('music','dosdata/SCORES.MIX'),('movies','ra3d/movies/existing.VQA'),('music','ra3d/audio/SCORES.MIX')]
def preferences(p):
 return dict(line.split()for line in p.read_text().splitlines()if line and not line.startswith('#'))
with tempfile.TemporaryDirectory(prefix='mac-additive-media-cli-',dir=ROOT/'build')as name:
 work=Path(name);manifest=work/'media.txt'
 manifest.write_text('OCMEDIA 1\nmodule ra3d\nmirror GDI http://127.0.0.1:1/no-network-required 0\n'+''.join(f'file {comp} {rel} {len(body)} {digest} GDI 0\n'for comp,rel in paths))
 for game in ['td','ra']:
  for selected in ['none','music','movies','movies,music']:
   install=work/(game+'-'+selected.replace(',','-'));install.mkdir()
   for rel in ['opencnc3d','TiberianDawn.dylib',*needs]:
    q=install/rel;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(b'INERT INSTALLED-MODULE PRESENCE FIXTURE')
   (install/'opencnc3d-install.txt').write_text('version 0.9.0\ndata_id additive-media-fixture\n')
   protected={}
   for _,rel in paths:
    q=install/rel;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(body);protected[rel]=body
   for code in ['td','ra']:
    q=install/f'opencnc3d-media-{code}.txt';q.write_text('movies no\nmusic yes\nmovies_state pending\nmusic_state complete\n')
   cfg=install/'ra3d-fx.cfg';cfg.write_text('USER CURRENT CFG\n');protected['ra3d-fx.cfg']=cfg.read_bytes()
   target=install/f'opencnc3d-media-{game}.txt';other=install/f'opencnc3d-media-{"ra"if game=="td"else"td"}.txt';other_before=other.read_bytes();before=preferences(target)
   command=[str(launcher),'--dir',str(install),'--media-install',selected,'--media-game',game,'--media-add-only','--media-manifest',str(manifest),'--stall-seconds','1']
   r=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
   if r.returncode:raise AssertionError(f'{game}/{selected}: '+r.stdout.decode(errors='replace'))
   after=preferences(target)
   for component in ['movies','music']:
    if component not in selected.split(','):
     assert after[component]==before[component]and after[component+'_state']==before[component+'_state'],(game,selected,component,before,after)
    else:assert after[component]=='yes'and after[component+'_state']=='complete',(game,selected,before,after)
   assert other.read_bytes()==other_before
   for rel,data in protected.items():assert(install/rel).read_bytes()==data,(game,selected,rel)
 print('MAC-ADDITIVE-MEDIA-CLI|PASS|actual native CLI; all8 TD/RA choices preserve existing files/unchecked preferences/other game/CFG; selected local pinned content verified; no HTTP/game/GUI; launcher_sha256='+hashlib.sha256(launcher.read_bytes()).hexdigest())
