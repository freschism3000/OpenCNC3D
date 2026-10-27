#!/usr/bin/env python3
"""Hidden TD/RA checks for the shared display shadow toe. Never opens/focuses a window."""
from pathlib import Path
import argparse,gzip,hashlib,json,os,re,struct,subprocess
import numpy as np
from PIL import Image

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--playable',type=Path,required=True)
p.add_argument('--cfg',type=Path,required=True)
p.add_argument('--ra-cfg',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();D=a.playable.resolve();O=a.output.resolve();O.mkdir(parents=True,exist_ok=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
protected={str(f):sha(f)for f in D.glob('*.cfg')}
results=[]
for th,scene,ra,cam in [('TEMPERAT','SCG01EA',False,'cam 48 52'),('RA_TEMPERAT','SCU02EA',True,''),('RA_SNOW','SCG03EA',True,'cam 78 64')]:
 o=O/th;o.mkdir(exist_ok=True);(o/'saves').mkdir(exist_ok=True)
 lines=['gfxmode 2','reveal 1','gfx show_stats 0','gfx cloud_on 0','gfx grass 0','dist 2200','tick 2']
 if cam:lines.append(cam)
 lines+=['gfx object_shadow_fill 0','gfx shadow_black_limit 0',f'shot {o}/floor0.png',f'renderdump {o}/base','gfx shadow_black_limit 0.035',f'shot {o}/floor035.png','gfx shadow_black_limit 0.10',f'shot {o}/floor100.png','gfx object_shadow_fill 0.80',f'shot {o}/object-fill-80.png','gfxpanel 1 shadow_black_limit',f'shot {o}/f5.png','gfxpanel 1 object_shadow_fill',f'shot {o}/f5-object-fill.png','gfxpanel 0','gfx shadow_black_limit 0.123','gfx object_shadow_fill 0.123',f'gfxsave {o}/roundtrip.cfg','gfx sun_energy 0','gfx sky_energy 0','gfx lights_on 0','gfx bloom_on 0',f'shot {o}/zero.png']
 if not ra:
  for mode,name in [(0,'classic'),(1,'native')]:
   lines += [f'gfxmode {mode}','gfx shadow_black_limit 0.1',f'shot {o}/{name}-on.png','gfx shadow_black_limit 0',f'shot {o}/{name}-off.png']
 lines+=['quit'];(o/'script.txt').write_text('\n'.join(lines)+'\n')
 cfg=(a.ra_cfg or D/'ra3d-fx.cfg').resolve()if ra else a.cfg.resolve()
 args=[str(D/'cnc_eyes'),'--nosound','--enhanced','--texset','3','--gfx',str(cfg),'--noshroud','--nosidebar','--nominimap','--w','1200','--h','800','--scen',scene,'--pack',('ra3d/3d/'if ra else'')+scene+'.pack','--dir','ra3d/missions/'if ra else'missions/','--dylib','ra3d/RedAlert.dylib'if ra else'TiberianDawn.dylib','--content','ra3d/data'if ra else'content/','--savedir',str(o/'saves'),'--gfxsave',str(o/'discard.cfg'),'--script',str(o/'script.txt')]
 if ra:args+=['--ra3d','--models3d',str(D)+'/', '--dosdata','ra3d/audio']
 with(o/'run.log').open('w')as log:r=subprocess.run(args,cwd=D,env=dict(os.environ,OPENCNC3D_BACKGROUND='1'),stdout=log,stderr=subprocess.STDOUT,timeout=240)
 text=(o/'run.log').read_text(errors='replace');assert r.returncode==0 and re.search(r'SCRIPT\|end .*0 failures',text),(th,text[-1500:]);assert not re.search(r'ERROR: 0:|FAILED TO LINK',text)
 raw=gzip.decompress((o/'base-normals.bin.gz').read_bytes());w,h,_=struct.unpack('<3i',raw[:12]);n=np.frombuffer(raw[12:],'<f4').reshape(h,w,4)[::-1]
 ims={k:np.asarray(Image.open(o/(k+'.png')).convert('RGB'),dtype=float)for k in ['floor0','floor035','floor100','object-fill-80','zero']}
 canopy=(n[...,3]>.48)&(n[...,3]<.52);objects=(n[...,3]>.01)&(n[...,3]<.7);sample=canopy if canopy.sum()>512 else objects;assert sample.sum()>128,(th,'no material-class sample')
 lum=lambda c:c@np.array([.2126,.7152,.0722]);stats={k:{'mean':float(lum(v)[sample].mean()),'nearblack':float((lum(v)[sample]<6).mean())}for k,v in ims.items()}
 assert stats['floor100']['mean']>stats['floor035']['mean']>=stats['floor0']['mean'],(th,stats)
 fill_delta=lum(ims['object-fill-80'])-lum(ims['floor0'])
 fill_sample=canopy if canopy.sum()>512 else objects
 assert np.percentile(fill_delta[fill_sample],75)>0.05,(th,'object self-shadow fill did not lift tree/structure pixels')
 stats['object_fill_80']={'mean_delta':float(fill_delta[fill_sample].mean()),'p75_delta':float(np.percentile(fill_delta[fill_sample],75))}
 assert np.percentile(ims['zero'][sample],90)<=5,(th,'zero-energy surface was lifted')
 saved=dict(l.split('#')[0].split()for l in(o/'roundtrip.cfg').read_text().splitlines()if len(l.split('#')[0].split())==2);assert abs(float(saved['shadow_black_limit'])-.123)<.00001 and abs(float(saved['object_shadow_fill'])-.123)<.00001
 if not ra:
  for name in ['classic','native']:
   delta=np.abs(np.array(Image.open(o/(name+'-on.png')),dtype=float)-np.array(Image.open(o/(name+'-off.png')),dtype=float));assert np.mean(delta>1)<.001,(name,'floor leaked into native mode')
 result={'theater':th,'passed':True,'sample':'canopy'if canopy.sum()>512 else'objects','pixels':int(sample.sum()),'stats':stats,'roundtrip':.123};results.append(result);print(th,'PASS',result['sample'],result['pixels'],flush=True)
assert all(sha(Path(f))==h for f,h in protected.items()),'User CFG changed'
(O/'RESULTS.json').write_text(json.dumps({'passed':True,'fixtures':results,'settings_preserved':protected},indent=2)+'\n')
print('PASS shared shadow detail, F5, saved roundtrip, zero radiance and native modes; user settings unchanged.')
