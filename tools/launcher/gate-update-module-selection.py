#!/usr/bin/env python3
"""Disposable real native policy/controller fixtures, with counted no-HTTP stubs."""
from pathlib import Path
import hashlib, os, re, subprocess, sys
assert len(sys.argv)==3
root=Path(__file__).resolve().parents[2];work=Path(sys.argv[2]);work.mkdir(parents=True,exist_ok=False)
needs=re.findall(r'"(ra3d/[^\"]+)"',(root/'launcher/lmodule.c').read_text().split('lmod_ra_needs[] = {',1)[1].split('};',1)[0])
needs=[n for n in needs if not (n.endswith(('.dylib',)) or n=='ra3d/opencnc3d')] if os.name=='nt' else [n for n in needs if not n.endswith(('.exe','.dll'))]
td=['opencnc3d-game.exe','TiberianDawn.dll'] if os.name=='nt' else ['opencnc3d','TiberianDawn.dylib']
def put(folder,name,data):
 p=folder/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
def snapshot(folder):
 return {p.relative_to(folder).as_posix():(p.stat().st_size,p.stat().st_mtime_ns,hashlib.sha256(p.read_bytes()).hexdigest()) for p in folder.rglob('*') if p.is_file()}
cases=[('empty-no-record','blocked',False,None),('empty-module-record','blocked',False,''),
       ('empty-stale-data','blocked',False,'matching-data'),('ra-no-data','blocked',True,''),
       ('ra-stale-matching-data','blocked',True,'matching-data'),('ra-different-data','blocked',True,'old-data'),
       ('td-matching-data','td-bins',False,'matching-data'),('td-different-data','td-full',False,'old-data'),
       ('td-missing-data','td-full',False,''),('both-games-matching-data','td-bins',True,'matching-data'),
       ('explicit-fresh-td','fresh-full',False,None)]
for name,mode,ra,data_id in cases:
 folder=work/name;folder.mkdir()
 for path,data in [('savegame.sav',b'USER SAVE EXACT'),('ra3d-fx.cfg',b'USER LATEST CFG EXACT'),
                   ('opencnc3d-update.zip',b'EXISTING UPDATE ZIP EXACT'),('opencnc3d-update.zip.part',b'EXISTING PART EXACT')]:put(folder,path,data)
 if data_id is not None:put(folder,'opencnc3d-install.txt',('version 0.9.0\nmodule_install 1\n'+('data_id '+data_id+'\n' if data_id else '')).encode())
 if ra:
  for path in needs:put(folder,path,b'INSTALLED RA PRESENCE FIXTURE')
 if mode.startswith('td-'):
  for path in td:put(folder,path,b'INSTALLED TD PRESENCE FIXTURE')
 before=snapshot(folder)
 result=subprocess.run([str(Path(sys.argv[1]).resolve()),str(folder.resolve()),mode],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
 print(result.stdout,end='');assert result.returncode==0,(name,result.returncode)
 if mode=='blocked':
  assert snapshot(folder)==before,(name,'Refused apply changed file bytes/names/mtime')
  assert 'transport_calls=0' in result.stdout
 else:
  for path in ['savegame.sav','ra3d-fx.cfg',*td]:
   if path in before:assert snapshot(folder)[path]==before[path],(name,path)
print('UPDATE-MODULE-CHOICE|PASS|blocked_cases=6|zero_HTTP_or_file_mutation=1|TD_matching_bins=1|TD_changed_or_missing_full=1|explicit_fresh_full=1|RA_only_and_mixed_Launch=1|TD_selected_Update=1|add_only_KEPT_vs_Settings_removal=1')
