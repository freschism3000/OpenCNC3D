#!/usr/bin/env python3
"""Hidden per-biome colour isolation, F5, neutral and saved-control regression."""
from pathlib import Path
import argparse,gzip,hashlib,json,os,re,struct,subprocess
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--renderer',type=Path);p.add_argument('--playable',type=Path,required=True);p.add_argument('--cfg',type=Path,required=True);p.add_argument('--ra-cfg',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
D=a.playable.resolve();renderer=(a.renderer or D/'cnc_eyes').resolve();O=a.output.resolve();O.mkdir(parents=True,exist_ok=True)
protected={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in D.glob('*.cfg')};groups=['grass','desert','sand','snow'];params={'warmth':0,'tint':0,'red':1,'green':1,'blue':1};cases=[('TEMPERAT','grass','SCG01EA',False,'cam 48 52'),('DESERT','desert','SCB01EA',False,'cam 46 20'),('SNOW','snow','SCS01EA',False,'cam 32 32'),('WINTER','snow','SCS01EA',False,'cam 32 32'),('SAND','sand','SCA01EA',False,'cam 32 32'),('RA_TEMPERAT','grass','SCU02EA',True,''),('RA_SNOW','snow','SCG03EA',True,'cam 78 64'),('RA_INTERIOR',None,'SCG10EB',True,'')];results=[]
winter=bytearray((D/'SCS01EA.pack').read_bytes());winter[36:52]=b'WINTER'.ljust(16,b'\0');(O/'winter.pack').write_bytes(winter)
for th,group,scene,ra,cam in cases:
 o=O/th;o.mkdir(exist_ok=True);(o/'saves').mkdir(exist_ok=True)
 neutral=[f'gfx {b}_terrain_{k} {v}'for b in groups for k,v in params.items()]
 l=['gfxmode 2','reveal 1','gfx show_stats 0','gfx cloud_on 0','gfx bloom_on 0','gfx grass 0','dist 2200','tick 2']+neutral
 if th in ['SNOW','SAND','WINTER']:l+=['terrpaint S25 30 29','terrpaint S26 32 29','terrpaint S27 34 29','elevpaint 32 30 0 1 5']
 if cam:l.append(cam)
 l += [f'shot {o}/warm.png',f'renderdump {o}/neutral',f'shot {o}/neutral.png',f'shot {o}/neutral-repeat.png']
 for k in (params if group else []):
  value=.8 if k in ['warmth','tint']else .45;l += [f'gfx {group}_terrain_{k} {value}',f'shot {o}/{k}.png',f'gfx {group}_terrain_{k} {params[k]}']
 for b in groups:
  if b!=group:l += [f'gfx {b}_terrain_red 0.25',f'gfx {b}_terrain_warmth 0.9']
 l += [f'shot {o}/inactive.png']+neutral+[f'shot {o}/neutral-return.png','gfxpanel 1 '+(group or 'grass')+'_terrain_warmth',f'shot {o}/f5.png','gfxpanel 0']
 for b in groups:
  for k in params:l.append(f'gfx {b}_terrain_{k} '+('0.321'if k in ['warmth','tint']else'1.234'))
 l += [f'gfxsave {o}/roundtrip.cfg','quit'];(o/'script.txt').write_text('\n'.join(l)+'\n')
 cfg=(a.ra_cfg or D/'ra3d-fx.cfg').resolve()if ra else a.cfg.resolve();args=[str(renderer),'--nosound','--enhanced','--texset','3','--gfx',str(cfg),'--noshroud','--nosidebar','--nominimap','--w','1200','--h','800','--scen',scene,'--pack',str(O/'winter.pack')if th=='WINTER'else('ra3d/3d/'if ra else'')+scene+'.pack','--dir','ra3d/missions/'if ra else'missions/','--dylib','ra3d/RedAlert.dylib'if ra else'TiberianDawn.dylib','--content','ra3d/data'if ra else'content/','--savedir',str(o/'saves'),'--gfxsave',str(o/'discard.cfg'),'--script',str(o/'script.txt')]
 if ra:args+=['--ra3d','--models3d',str(D)+'/', '--dosdata','ra3d/audio']
 if th in ['SNOW','SAND','WINTER']:args+=['--edit']
 with(o/'run.log').open('w')as f:r=subprocess.run(args,cwd=D,env=dict(os.environ,OPENCNC3D_BACKGROUND='1'),stdout=f,stderr=subprocess.STDOUT,timeout=240)
 s=(o/'run.log').read_text(errors='replace');assert r.returncode==0 and re.search(r'SCRIPT\|end .*0 failures',s),(th,s[-1500:]);assert not re.search(r'ERROR: 0:|FAILED TO LINK',s)
 raw=gzip.decompress((o/'neutral-normals.bin.gz').read_bytes());w,h,_=struct.unpack('<3i',raw[:12]);n=np.frombuffer(raw[12:],'<f4').reshape(h,w,4)[::-1];ground=n[...,3]>.75;objects=(n[...,3]>.01)&(n[...,3]<.7)
 for _ in range(2):
  pad=np.pad(objects,1);objects=pad[1:-1,1:-1]&pad[:-2,1:-1]&pad[2:,1:-1]&pad[1:-1,:-2]&pad[1:-1,2:]
 if th in ['SNOW','SAND','WINTER']:
  yy,xx=np.indices((h,w));ground&=(xx>w*.15)&(xx<w*.7)&(yy>h*.10)&(yy<h*.82);objects[:]=False
 base=np.asarray(Image.open(o/'neutral.png').convert('RGB'),dtype=int);inactive=np.asarray(Image.open(o/'inactive.png').convert('RGB'),dtype=int);compare=np.ones((h,w),dtype=bool)
 if th in ['SNOW','SAND','WINTER']:compare=(xx>w*.15)&(xx<w*.7)&(yy>h*.10)&(yy<h*.82)
 assert np.max(abs(base-inactive)[compare])==0,(th,'inactive biome changed world')
 stats={}
 for k in (params if group else []):
  im=np.asarray(Image.open(o/(k+'.png')).convert('RGB'),dtype=int);delta=np.max(abs(im-base),axis=2);changed=int((ground&(delta>2)).sum());assert changed>100,(th,k,'ground did not respond',changed);objmax=int(delta[objects].max())if objects.any()else 0;assert objmax<=1,(th,k,'objects recoloured',objmax);stats[k]={'ground_pixels_changed':changed,'object_max_delta':objmax}
 saved=dict(l.split('#')[0].split()for l in(o/'roundtrip.cfg').read_text().splitlines()if len(l.split('#')[0].split())==2)
 for b in groups:
  for k in params:assert abs(float(saved[b+'_terrain_'+k])-(.321 if k in ['warmth','tint']else 1.234))<.00001
 results.append({'theater':th,'group':group,'passed':True,'inactive_biomes_exact':True,'controls':stats});(O/'RESULTS.json').write_text(json.dumps({'passed':False,'fixtures':results},indent=2)+'\n');print(th,'PASS',stats,flush=True)
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in protected.items())
(O/'RESULTS.json').write_text(json.dumps({'passed':True,'fixtures':results,'settings_preserved':protected},indent=2)+'\n')
