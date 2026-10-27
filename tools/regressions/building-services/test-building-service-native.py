#!/usr/bin/env python3
"""Hidden rendering fixture, requiring a private snapshot-instrumented renderer.
This checks renderer phase appearance; injected FIX/HPAD states are NOT live repair/reload.
"""
from pathlib import Path
import argparse,subprocess,os,shutil,json
p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=Path);p.add_argument('--exe',required=True,type=Path);p.add_argument('--work',required=True,type=Path);p.add_argument('--fixtures',type=Path,default=Path(__file__).parent/'fixtures');a=p.parse_args();a.repo=a.repo.resolve();a.exe=a.exe.resolve();a.work=a.work.resolve()
if Path('/private/tmp') not in a.exe.parents or Path('/private/tmp') not in a.work.parents:raise SystemExit('Require private renderer and private output directory.')
a.work.mkdir(parents=True,exist_ok=True);assets=a.work/'assets';assets.mkdir(exist_ok=True)
# Exact read-only art/data links, plus private fixture files. No logs/settings linked.
for n in ['ebld','evhc','einf','eplc','models3d','n64fix','content','dosdata','newaudio','dosclassic.pack','efx.pack','efx2.pack','efx3.pack','experimental_terrain.pack','remaster_terrain.pack','remaster_terrain_snow.pack','remaster_terrain_desert.pack','fxlight.png','radar_bg.png']:
 q=a.repo/'playable'/n;z=assets/n
 if q.exists() and not z.exists():z.symlink_to(q,target_is_directory=q.is_dir())
for q in (a.repo/'playable').glob('*.pack'):
 z=assets/q.name
 if not z.exists():z.symlink_to(q)
for n in ['SCG91EA.INI','SCG91EA.BIN']:shutil.copyfile(a.fixtures/n,assets/n)
for label,off in [('on',False),('off',True)]:
 d=a.work/label;d.mkdir(exist_ok=True);(d/'fx.cfg').write_text('');script='gfxmode 2\ngfx show_stats 0\ncheatset fog 0\ntick 3\n'
 for typ,cam in [('FIX','21.5 16'),('HPAD','36 16'),('PROC','7.5 16')]:
  script+='cam '+cam+'\ndist 1800\nbservicepreview '+typ+' 1 0 0\nshot '+str(d/(typ+'-idle.png'))+'\n'
  for stage in range(7) if typ!='PROC' else range(6,12):
   doing=3 if typ=='PROC' else 2
   script+=f'bservicepreview {typ} {doing} {stage} 0\nbservicedump\nshot '+str(d/f'{typ}-active-{stage}.png')+'\n'
  script+=f'bservicepreview {typ} 2 3 1\nshot '+str(d/(typ+'-poweroff.png'))+'\n'
 script+='quit\n';(d/'script.txt').write_text(script)
 env={**os.environ,'OPENCNC3D_BACKGROUND':'1','OPENCNC3D_SERVICE':'0' if off else '1'}
 cmd=[str(a.exe),'--build','98','--scen','SCG91EA','--pack',str(a.repo/'playable/SCB01EA.pack'),'--dir',str(assets)+'/', '--content','content/','--dylib',str(a.repo/'playable/TiberianDawn.dylib'),'--nosound','--enhanced','--texset','3','--gfx',str(d/'fx.cfg'),'--gfxsave',str(d/'private.cfg'),'--savedir',str(d/'saves'),'--ticks','0','--w','1280','--h','720','--script',str(d/'script.txt')]
 with (d/'run.log').open('w') as f:r=subprocess.run(cmd,cwd=assets,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 text=(d/'run.log').read_text();assert r.returncode==0 and '0 failures' in text and 'BSERVICE-PREVIEW|injected' in text,(r.returncode,text[-1000:])
(a.work/'result.json').write_text(json.dumps({'passed':True,'backgroundOnly':True,'testMethod':'Explicitly injected private renderer snapshot states; source-header controls validate engine BState schedules separately. These are not live gameplay service proofs.','captureDirs':['on','off']},indent=2))
print(a.work)
