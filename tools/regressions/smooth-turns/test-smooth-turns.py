#!/usr/bin/env python3
from pathlib import Path
import subprocess,os,re,json,shutil,hashlib,sys,time
import argparse
p=argparse.ArgumentParser(description='Hidden native vehicle turn regression. Does not change installed preferences or saves.')
p.add_argument('--runtime',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--baseline',type=Path);p.add_argument('--cfg',type=Path)
ARGS=p.parse_args();P=ARGS.runtime.resolve();OUT=ARGS.out.resolve();OUT.mkdir(parents=True,exist_ok=False)
EXE=P/'cnc_eyes';CFG=OUT/'input.cfg'
CFG.write_bytes(ARGS.cfg.read_bytes() if ARGS.cfg else (P/'opencnc3d-fx.cfg').read_bytes())
def mission(kind):
 d=OUT/('mission-'+kind);d.mkdir()
 original=P/'missions/SCG90EA.INI';parts=re.split(r'(?m)^\[([^\]]+)\]\s*\n',original.read_text());out=[]
 for i in range(1,len(parts),2):
  k,v=parts[i:i+2]
  if k.upper()=='BASIC':v='Player=GoodGuy\nBuildLevel=98\nTheme=No theme\nIntro=x\nAction=x\nBrief=x\nWin=x\nLose=x\n'
  if k.upper()=='MAP':v='X=3\nY=3\nWidth=58\nHeight=58\nTheater=TEMPERATE\n'
  if k.upper()=='UNITS':v=('000=GoodGuy,HARV,256,2932,248,Harvest,None\n' if kind=='harv' else '000=GoodGuy,MTNK,256,2870,0,Guard,None\n001=BadGuy,HARV,256,2873,0,Sleep,None\n')
  if k.upper()=='STRUCTURES':v='000=GoodGuy,PROC,256,2994,0,None\n001=GoodGuy,NUKE,256,3063,0,None\n'
  if k.upper() in ('INFANTRY','TRIGGERS','CELLTRIGGERS','TEAMTYPES','TEAMS'):v=''
  out.append('['+k+']\n'+v)
 (d/'SCG94EA.INI').write_text('\n'.join(out));shutil.copy2(original.with_suffix('.BIN'),d/'SCG94EA.BIN')
 return d

def run(name,lines,fixture,exe=EXE,extra=()):
 d=OUT/name;d.mkdir(exist_ok=True);(d/'saves').mkdir(exist_ok=True)
 sf=d/'script.txt';sf.write_text('\n'.join(x.replace('@OUT@',str(d)) for x in lines+['quit'])+'\n')
 a=[str(exe),'--scen','SCG94EA','--pack','SCG01EA.pack','--dir',str(fixture)+'/', '--reskin','--content','content/','--cameos','cameos.pack','--dospack','dossidebar.pack','--dosinf','dosinfantry.pack','--dylib','TiberianDawn.dylib','--seed','1','--noshroud','--noteamcolours','--w','1280','--h','720','--gfx',str(CFG),'--visualscfg',str(d/'visuals.cfg'),'--savedir',str(d/'saves'),'--script',str(sf),'--enhanced','--nosound',*extra]
 (d/'invocation.json').write_text(json.dumps(a,indent=2))
 with (d/'run.log').open('w')as log:
  proc=subprocess.Popen(a,cwd=P,env=dict(os.environ,OPENCNC3D_BACKGROUND='1'),stdout=log,stderr=subprocess.STDOUT)
  pid=proc.pid;sample=subprocess.run(['/usr/bin/lsappinfo','info','-only','ApplicationType','#'+str(pid)],capture_output=True,text=True)
  (d/'background.txt').write_text(sample.stdout+sample.stderr)
  rc=proc.wait(timeout=180)
 text=(d/'run.log').read_text();assert rc==0 and re.search(r'SCRIPT\|end .*: \d+ lines, 0 failures',text),(name,rc,text[-1000:])
 return text,d

def records(text):
 out=[]
 for l in text.splitlines():
  if l.startswith('DRAWHEADING|'):
   v=dict(re.findall(r'(\w+)=([^|]+)',l));out.append({k:float(vv)for k,vv in v.items()})
 return out
setup=['gfxmode 2','gfx show_stats 0','cursor -400 -400','reveal 1','cam 54 44','dist 1920','tickalpha 0','tick 2']
reports={}
for kind,typ,n in [('harv','HARV',100),('tank','MTNK',60)]:
 m=mission(kind);l=setup[:]
 for t in range(n):
  l+=['tick 1','smoothmove 1']
  for alpha in [0,.25,.5,.75,.999]:l += [f'tickalpha {alpha}',f'drawheading {typ} 0']
  l+=['smoothmove 0','tickalpha .5',f'drawheading {typ} 0']
 text,d=run(kind+'-scan',l,m);rec=records(text);assert len(rec)==n*6
 hull=turret=0;chosen=None
 for i in range(0,len(rec),6):
  group=rec[i:i+6];on=group[:5];off=group[5]
  assert len({(x['face'],x['tface'])for x in group})==1,'Readback changed engine orientation'
  assert off['primary']==off['face'] and off['secondary']==off['tface']
  def diff(a,b):return (b-a+128)%256-128
  if abs(diff(on[0]['primary'],on[-1]['primary']))>.5:
   hull+=1
   if chosen is None:chosen=i//6+3
   assert abs(diff(on[0]['hull'],on[2]['hull']))>0
  if abs(diff(on[0]['secondary'],on[-1]['secondary']))>.5:turret+=1
 assert hull>0 if kind=='harv' else turret>0,(kind,hull,turret)
 reports[kind]={'turning_hull_ticks':hull,'turning_secondary_ticks':turret,'engine_orientation_unchanged_across_subframes':True,'off_equals_engine':True,'sample_tick':chosen,'samples':len(rec)}
 # A fixed-tick A/B: baseline and new binary must agree with smoothing off.
 at=chosen or 8
 base=setup[:-1]+[f'tick {at}','smoothmove 0','tickalpha .5','shot @OUT@/off.png']
 if ARGS.baseline:
  _,old=run(kind+'-old-off',base,m,ARGS.baseline.resolve())
  _,new=run(kind+'-new-off',base,m)
  assert (old/'off.png').read_bytes()==(new/'off.png').read_bytes(),kind+' unsmoothed pixels changed'
 shot=setup[:-1]+[f'tick {at}','smoothmove 1']
 for a in [0,.25,.5,.75,.999]:shot += [f'tickalpha {a}',f'drawheading {typ} 0',f'shot @OUT@/on-{a}.png']
 _,drawn=run(kind+'-subframes',shot,m)
 assert len({hashlib.sha256(x.read_bytes()).hexdigest()for x in drawn.glob('on-*.png')})==5,kind+' subframes not visually distinct'
 reports[kind]['unsmoothed_pixels_equal_previous_build']=True if ARGS.baseline else None
 reports[kind]['five_distinct_rendered_subframes']=True
# Cartridge fallback must use the same fractional path; Classic retains its old pixels.
for name,mode,extra in [('cartridge',1,()),('enhanced-fallback',2,('--noevhc',)),('classic',0,())]:
 m=mission('harv') if not (OUT/'mission-harv').exists() else OUT/'mission-harv'
 body=[x if x!='gfxmode 2' else f'gfxmode {mode}' for x in setup[:-1]]+['tick 3','smoothmove 0','tickalpha .5','shot @OUT@/off.png']
 if ARGS.baseline:
  _,old=run(name+'-old-off',body,m,ARGS.baseline.resolve(),extra)
  _,new=run(name+'-new-off',body,m,extra=extra)
  assert (old/'off.png').read_bytes()==(new/'off.png').read_bytes(),name+' unsmoothed pixels changed'
 if mode:
  body=[x if x!='gfxmode 2' else f'gfxmode {mode}' for x in setup[:-1]]+['tick 3','smoothmove 1']
  for phase in [0,.25,.5,.75,.999]:body += [f'tickalpha {phase}','drawheading HARV 0',f'shot @OUT@/on-{phase}.png']
  text,_=run(name+'-subframes',body,m,extra=extra);rec=records(text)
  assert len({x['hull'] for x in rec})==5,name+' heading was quantized'
 reports[name]={'unsmoothed_pixels_equal_previous_build':True if ARGS.baseline else None,'fractional_heading':bool(mode)}
(OUT/'results.json').write_text(json.dumps(reports,indent=2));print(json.dumps(reports,indent=2),flush=True)
