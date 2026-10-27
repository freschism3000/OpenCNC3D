from pathlib import Path
import os,subprocess,re,json,time
import argparse,hashlib
ap=argparse.ArgumentParser(description='Hidden sun/sky, terrain and F5 checks. Never opens or focuses a visible window.')
ap.add_argument('--analyze-only',action='store_true');ap.add_argument('--playable',type=Path,required=True);ap.add_argument('--renderer',type=Path);ap.add_argument('--cfg',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--native-fixtures',action='store_true')
a=ap.parse_args();R=Path(__file__).resolve().parents[1];D=a.playable.resolve();O=a.output.resolve();O.mkdir(parents=True,exist_ok=True)
renderer=a.renderer.resolve() if a.renderer else D/'cnc_eyes';cfg=a.cfg.resolve()
protected={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in D.glob('*-fx.cfg')}
# Cold map fixtures exercise actual TD/RA runtime readers; Winter shares the native snow mission layout.
b=bytearray((D/'SCS01EA.pack').read_bytes());b[36:52]=b'WINTER'.ljust(16,b'\0');(O/'winter.pack').write_bytes(b)
fixtures=[('DESERT','SCB01EA',False,'cam 46 20'),('TEMPERAT','SCG01EA',False,''),('SNOW','SCS01EA',False,''),('WINTER','SCS01EA',False,''),('SAND','SCA01EA',False,''),('RA_SNOW','SCG03EA',True,'cam 78 64'),('RA_TEMPERAT','SCU02EA',True,''),('RA_INTERIOR','SCG10EB',True,'')]
results=json.loads((O/'RESULTS.json').read_text()) if a.analyze_only else []
if isinstance(results,dict):results=results['fixtures']
for th,sc,ra,cam in ([] if a.analyze_only else fixtures):
 o=O/th;o.mkdir(exist_ok=True);(o/'saves').mkdir(exist_ok=True)
 lines=['gfxmode 2','gfx texset 3','gfx show_stats 0','reveal 1','gfx grass 0','gfx cloud_on 0','gfx sand_fx 0','gfx fog_fx 0','gfx rain 0','gfx snow 0','gfx bloom_on 0','gfx lights_on 0','gfx ssao_on 0','gfx contact_on 0','dist 2200','tick 2']
 # rain/snow keys are supplied by accepted CFG, avoid assumptions on optional weather names.
 lines=[l for l in lines if l not in ['gfx rain 0','gfx snow 0']]
 if a.native_fixtures and th in ['SNOW','WINTER','SAND']:
  lines += ['terrpaint S25 30 29','terrpaint S26 32 29','terrpaint S27 34 29','terrpaint B1 30 33','elevpaint 32 30 0 1 5','cam 32 32']
 if cam:lines.append(cam)
 lines+=['gfx sun_az 67.5','gfx sun_el 27.28572',f'shot {o}/day.png',f'renderdump {o}/day','gfx sun_az 247.5',f'shot {o}/opposite.png','gfx sun_energy 0',f'shot {o}/sky-only.png','gfx sky_energy 0',f'shot {o}/black.png','gfx sun_energy 1',f'shot {o}/sun-only.png','gfx sky_energy 1','framebench 30','gfx terrain_relief 0','framebench 30','gfx terrain_relief 1','gfxpanel 1 unified_lighting',f'shot {o}/f5-sun.png','gfxpanel 1 desert_shadow_strength',f'shot {o}/f5-desert.png','gfxpanel 1 sand_shadow_strength',f'shot {o}/f5-sand.png','gfxpanel 1 snow_shadow_strength',f'shot {o}/f5-snow.png','gfxpanel 0',f'gfxsave {o}/roundtrip.cfg','quit']
 if th=='DESERT':
  lines[-1:-1]=['gfx unified_lighting 1','gfx terrain_relief 1','gfxmode 0',f'shot {o}/classic-on.png','gfx unified_lighting 0','gfx terrain_relief 0','gfxmode 0',f'shot {o}/classic-off.png','gfx unified_lighting 1','gfxmode 1',f'shot {o}/native-on.png','gfx unified_lighting 0','gfxmode 1',f'shot {o}/native-off.png']
 (o/'commands.txt').write_text('\n'.join(lines)+'\n')
 pack=str(O/'winter.pack') if th=='WINTER' else ('ra3d/3d/' if ra else '')+sc+'.pack'
 args=[str(renderer),'--nosound','--enhanced','--texset','3','--gfx',str(cfg),'--noshroud','--nosidebar','--nominimap','--w','1200','--h','800','--scen',sc,'--pack',pack,'--dir','ra3d/missions/' if ra else 'missions/','--dylib','ra3d/RedAlert.dylib' if ra else 'TiberianDawn.dylib','--content','ra3d/data' if ra else 'content/','--savedir',str(o/'saves'),'--gfxsave',str(o/'discard.cfg'),'--script',str(o/'commands.txt')]
 if a.native_fixtures and th in ['SNOW','WINTER','SAND']:args+=['--edit']
 if ra:args+=['--ra3d','--models3d',str(D)+'/', '--dosdata','ra3d/audio']
 env=dict(os.environ,OPENCNC3D_BACKGROUND='1')
 t=time.time()
 with (o/'run.log').open('w') as log:r=subprocess.run(args,cwd=D,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=240)
 s=(o/'run.log').read_text(errors='replace');passed=r.returncode==0 and bool(re.search(r'SCRIPT\|end .*0 failures',s)) and not re.search(r'ERROR: 0:|shader.*failed',s,re.I)
 result={'theater':th,'scene':sc,'passed':passed,'seconds':time.time()-t,'geometry':list(dict.fromkeys(re.findall(r'TERRAINSURFACE\|[^\n]+',s))),'bench':re.findall(r'FRAMEBENCH[^\n]+',s),'failures':re.findall(r'[^\n]*(?:SCRIPT\|FAIL|unknown key)[^\n]*',s)};results.append(result);(O/'RESULTS.json').write_text(json.dumps(results,indent=2)+'\n');print(th,passed,result['geometry'],result['bench'],result['failures'],flush=True)
 assert passed,s[-1500:]

for p,h in protected.items():assert hashlib.sha256(Path(p).read_bytes()).hexdigest()==h,'Saved user settings changed: '+p
# Quantitative energy check against actual material-class pixels.
import gzip,struct,numpy as np
from PIL import Image
for r in results:
 o=O/r['theater'];b=gzip.decompress((o/'day-normals.bin.gz').read_bytes());w,h,t=struct.unpack('<3i',b[:12]);nrm=np.frombuffer(b[12:],'<f4').reshape(h,w,4)[::-1]
 im=np.array(Image.open(o/'black.png').convert('RGB'));assert im.shape[:2]==(h,w)
 ground=nrm[...,3]>.75;objects=(nrm[...,3]>.01)&(nrm[...,3]<.7)
 if a.native_fixtures and r['theater'] in ['SNOW','WINTER','SAND']:
  # Editor chrome and waypoint labels are unlit UI over the world; its normal
  # target still contains the occluded world behind them. Use the named central
  # physical cliff fixture, excluding palette/chrome and flat waypoint overlays.
  yy,xx=np.indices((h,w));review=(xx>w*.12)&(xx<w*.64)&(yy>h*.15)&(yy<h*.4)
  ground=review&(nrm[...,3]>.84)&(nrm[...,3]<.96);objects=np.zeros_like(ground)
  assert ground.sum()>1000,('physical cliff fixture absent',r['theater'])
  r['energy_sample']='upper central physical-rock fixture (.12-.64 width, .15-.40 height); editor waypoint plates below it excluded'

 r['black_ground_p99']=float(np.percentile(im[ground],99)) if ground.any() else None
 r['black_objects_p90']=float(np.percentile(im[objects],90)) if objects.any() else None
 assert r['black_ground_p99'] is None or r['black_ground_p99']<=12,r
 assert r['black_objects_p90'] is None or r['black_objects_p90']<=5,r
for mode in ['classic','native']:
 x=np.array(Image.open(O/'DESERT'/(mode+'-on.png'))).astype(int);y=np.array(Image.open(O/'DESERT'/(mode+'-off.png'))).astype(int)
 delta=np.abs(x-y);assert float(np.mean(delta>1))<.001,(mode,'mode isolation',float(np.mean(delta>1)))
(O/'RESULTS.json').write_text(json.dumps({'passed':True,'fixtures':results,'settings_preserved':protected,'native_classic_isolation':True},indent=2)+'\n')
print('Unified daylight checks passed; user CFG files unchanged.')
