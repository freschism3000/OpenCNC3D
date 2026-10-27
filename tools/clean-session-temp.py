#!/usr/bin/env python3
"""Remove completed OpenCNC3D session/staging folders after 72 idle hours.

Only direct children of system temp roots with a completion receipt are eligible.
To mark a disposable session, write .cnc3d-cleanup.json containing
{"owner":"OpenCNC3D","completed":true,"disposable":true,"kind":"session",
 "unique_authoring_files":false}.
Use kind "release-staging" for published staging and include published:true.
Never mark a folder that holds the only copy of authored work.
The three October 15 staging paths were explicitly cleared by the user.
"""
import argparse,json,os,shutil,subprocess,time
from pathlib import Path
from storage_cleanup import cleanup_lock
# Historical staging names are no longer authority: a reused name may hold new work.
KNOWN=set()
def activity_paths():
 p=subprocess.run(['lsof','-nP','-Fpn'],capture_output=True,text=True)
 # lsof returns 1 for some inaccessible processes; require a useful own-user snapshot.
 if not p.stdout or 'n/'not in p.stdout:raise RuntimeError('cannot verify process open files; keeping all temp folders')
 paths=[str(Path(x[1:]).resolve())for x in p.stdout.splitlines()if x.startswith('n/')]
 commands=subprocess.run(['ps','-axo','command='],capture_output=True,text=True,check=True).stdout
 return paths,commands
def inspect(p,now,paths,commands):
 if p.is_symlink()or not p.is_dir():return None
 receipt=p/'.cnc3d-cleanup.json';marked=False
 if receipt.is_file()and not receipt.is_symlink():
  try:
   m=json.loads(receipt.read_text());marked=m.get('owner')=='OpenCNC3D'and m.get('completed')is True and m.get('disposable')is True and(m.get('kind')=='session'or(m.get('kind')=='release-staging'and m.get('published')is True))
  except(ValueError,OSError):return None
 if not marked and p.name not in KNOWN:return None
 if marked and m.get('unique_authoring_files')is not False:return {'path':str(p),'keep':'receipt does not verify absence of unique authoring work'}
 text=str(p.resolve());active=any(x==text or x.startswith(text+'/')for x in paths)or text in commands or text.replace('/private/tmp/','/tmp/')in commands
 if active:return {'path':text,'keep':'active process reference'}
 newest=max(p.stat().st_mtime,p.stat().st_ctime);size=0
 for d,ds,fs in os.walk(p,followlinks=False):
  q=Path(d);newest=max(newest,q.stat().st_mtime,q.stat().st_ctime)
  if '.git'in ds or '.git'in fs:return {'path':text,'keep':'Git checkout; requires separate worktree cleanup'}
  ds[:]=[n for n in ds if not(q/n).is_symlink()]
  for n in fs:
   f=q/n;s=f.lstat();newest=max(newest,s.st_mtime,s.st_ctime);size+=s.st_size
   if f.suffix.lower()in{'.blend','.blend1','.drp','.dra','.psd','.kra','.aep'}:return {'path':text,'keep':'authoring/project file; retain for manual dependency review'}
 age=(now-newest)/3600
 if age<72:return {'path':text,'keep':'modified within 72 hours','idle_hours':round(age,2)}
 return {'path':text,'bytes':size,'idle_hours':round(age,2),'eligible':True}
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--apply',action='store_true');ap.add_argument('--report',type=Path);a=ap.parse_args();paths,commands=activity_paths();roots={Path('/private/tmp'),Path(os.environ.get('TMPDIR','/private/tmp')).resolve()};rows=[]
 for root in roots:
  if root.is_dir():
   for p in root.iterdir():
    row=inspect(p,time.time(),paths,commands)
    if row:rows.append(row)
 result={'created':time.time(),'retention_hours':72,'mode':'APPLY'if a.apply else'DRY_RUN','entries':rows,'freed_bytes':0}
 for row in rows:
  if row.get('eligible'):
   print(('REMOVE'if a.apply else'WOULD REMOVE'),row['path'],row['bytes'])
   if a.apply:
    p=Path(row['path'])
    if p.parent not in roots or p.is_symlink():raise RuntimeError('temp path changed; retained')
    # Refresh process references and idle/authoring checks immediately before removal.
    fresh_paths,fresh_commands=activity_paths();fresh=inspect(p,time.time(),fresh_paths,fresh_commands)
    if not fresh or not fresh.get('eligible'):row['keep']='safety conditions changed';row['eligible']=False;continue
    shutil.rmtree(p);result['freed_bytes']+=fresh['bytes']
  else:print('KEEP',row['path'],row['keep'])
 if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n')
 print('Completed-temp cleanup:',round(result['freed_bytes']/2**30,3),'GiB freed')
if __name__=='__main__':
 try:
  with cleanup_lock():main()
 except BlockingIOError:print('KEEP temp folders: another cleanup is running')
