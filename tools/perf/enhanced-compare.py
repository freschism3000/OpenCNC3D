#!/usr/bin/env python3
"""Strict same-backend Enhanced regression: decoded pixels, auxiliary targets and physics.
Requires baseline/candidate builds with the script-only render_capture.h diagnostic.
Never changes shipped quality settings. Every run explicitly hides the window.
"""
import argparse,gzip,json,os,re,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
p=argparse.ArgumentParser(description=__doc__)
for key in ('runtime','baseline','candidate','gfx','output'):p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--cases',nargs='*')
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
CASES={
 'forest':('SCG05EA','SCG05EA.pack',[],['tick 60','cam 11 29','dist 3000','capture','tick 1','capture','tick 4','capture','reveal 1','tick 30','capture','cam 32 32','dist 4200','capture','cam 2 2','dist 1200','capture','shrouddarktest','capture','reveal 1','tick 1','capture']),
 'base':('SCG08EA','SCG08EA.pack',[],['tick 60','cam 47 48','dist 3000','capture','tick 1','capture','tick 4','capture','tick 30','capture','dist 4200','capture','shrouddarktest','capture','reveal 1','capture','gfx sun_az 180','capture','gfx sun_el 25','capture','gfx terrain_shadow_strength 1','capture','gfx terrain_shadow_strength 0.69643','capture','gfx experimental_relief_mode 1','capture','gfx experimental_relief_mode 0','capture']),
 'flag-coast':('SCG90EA','SCG01EA.pack',[],['tick 60','cam 58 54.6','dist 1500','capture','tick 1','capture','tick 4','capture','tick 30','capture','cam 54 50','capture','dist 4200','capture','shrouddarktest','capture','reveal 1','capture']),
 'explosion':('SCB20EA','SCB20EA.pack',['--noshroud'],['tick 30','cam 50 12','dist 3000','capture','hurtobj NUK2','tick 1','capture','tick 15','capture','tick 25','capture','tick 20','capture','tick 60','capture','cam 2 2','capture','cam 50 12','tick 500','capture']),
 'bodies':('USER93','SCG01EA.pack',['--reskin','--noshroud','--dir','missions/user_maps/'],['cam 31 30','tick 120','capture','tick 120','capture','tick 60','cam 31 29','capture','tick 30','capture','cam 2 2','capture','tick 60','cam 31 29','capture']),
 'vehicle-wreck':('SCG90EA','SCG01EA.pack',['--reskin','--noshroud'],['tick 30','cam 54 50','capture','hurtobj MTNK 0','tick 1','capture','hurtobj MTNK 0','tick 1','capture','tick 15','capture','tick 60','capture','tick 300','capture']),
 'modes':('SCG08EA','SCG08EA.pack',[],['tick 60','cam 47 48','gfxmode 0','capture','tick 1','capture','gfxmode 1','capture','tick 4','capture','gfxmode 2','capture']),
}
results={}
for name,(scen,pack,flags,steps) in CASES.items():
 if a.cases and name not in a.cases:continue
 for leg,exe in [('baseline',a.baseline),('candidate',a.candidate)]:
  out=a.output/name/leg;out.mkdir(parents=True,exist_ok=True)
  lines=['gfx show_stats 0'];index=0
  for step in steps:
   if step!='capture':lines.append(step);continue
   lines.extend([f'shot {out / (str(index)+".png")}','physicsdump'])
   if index in (1,3):lines.append(f'renderdump {out / str(index)}')
   index+=1
  lines.extend(['meshperfdump','m3dump','quit']);script=out/'script.txt';script.write_text('\n'.join(lines)+'\n')
  cmd=[str(exe.resolve()),'--scen',scen,'--pack',pack,'--dylib','TiberianDawn.dylib','--dir','missions/','--content','content/','--cameos','cameos.pack','--dospack','dossidebar.pack','--dosinf','dosinfantry.pack','--nosound','--w','1280','--h','800','--enhanced','--gfx',str(a.gfx.resolve()),'--savedir',str(out/'saves'),*flags,'--script',str(script)]
  with (out/'run.log').open('w') as log:
   proc=subprocess.run(cmd,cwd=a.runtime.resolve(),env={**os.environ,'OPENCNC3D_BACKGROUND':'1'},stdout=log,stderr=subprocess.STDOUT,timeout=600)
  text=(out/'run.log').read_text(errors='replace')
  if proc.returncode or not re.search(r'SCRIPT\|end .*: \d+ lines, 0 failures',text):raise RuntimeError(name+' '+leg+': failed '+str(proc.returncode))
 old=a.output/name/'baseline';new=a.output/name/'candidate';pixels=[];targets=[]
 for f in sorted(old.glob('*.png')):
  x=np.asarray(Image.open(f).convert('RGBA'),dtype=np.int16);y=np.asarray(Image.open(new/f.name).convert('RGBA'),dtype=np.int16)
  d=np.abs(x-y);r={'frame':f.name,'pixels':int(np.count_nonzero(d.max(2))),'max_delta':int(d.max())};pixels.append(r)
 for f in sorted(old.glob('*.bin*')):
  def read(path):return gzip.decompress(path.read_bytes()) if path.suffix=='.gz' else path.read_bytes()
  b=read(f);n=read(new/f.name);targets.append({'frame':f.name,'bytes':len(b),'equal':b==n})
 olog=(old/'run.log').read_text();nlog=(new/'run.log').read_text();phys=re.findall(r'^PHYSICSCAPTURE.*$',olog,re.M)==re.findall(r'^PHYSICSCAPTURE.*$',nlog,re.M)
 results[name]={'pixels':pixels,'targets':targets,'physics_equal':phys}
 (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 print(name,'pixels',sum(r['pixels'] for r in pixels),'target_mismatches',sum(not r['equal'] for r in targets),'physics',phys,flush=True)
 if name=='bodies':assert re.search(r'PHYSICSCAPTURE\|[^\n]*bodies=([1-9][0-9]*)',nlog),'body fixture was not exercised'
 assert all(r['pixels']==0 for r in pixels),name+': decoded pixel parity'
 assert all(r['equal'] for r in targets),name+': intermediate target parity'
 assert phys,name+': physical state changed'
print('PASS strict pixels, targets and physical state',flush=True)
