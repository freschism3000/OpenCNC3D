#!/usr/bin/env python3
"""Exercise the installed TD combined app, hidden, using only private settings."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,time

p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
r=a.runtime.resolve();o=a.out.resolve();o.mkdir(parents=True,exist_ok=False);(o/'saves').mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
protected={str(p):sha(p) for p in r.rglob('*') if p.is_file() and (p.suffix=='.cfg' or 'saves' in p.parts)}
shutil.copy2(r/'opencnc3d-fx.cfg',o/'fx.cfg')
cmd=[str(r/'opencnc3d'),'--harness','1','--rounds','1','--hidden','--no-movies','--nosound','--harnessscen','SCG22EA','--w','1280','--h','720','--gfx',str(o/'fx.cfg'),'--visualscfg',str(o/'visuals.cfg'),'--savedir',str(o/'saves'),'--shotdir',str(o)]
(o/'invocation.json').write_text(json.dumps(cmd,indent=2)+'\n')
env=dict(os.environ,OPENCNC3D_BACKGROUND='1');samples=[]
with (o/'run.log').open('w') as log:
    proc=subprocess.Popen(cmd,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT)
    try:
        for i in range(12):
            if proc.poll() is not None:break
            state=subprocess.run(['/usr/bin/lsappinfo','info','-only','ApplicationType','#'+str(proc.pid)],capture_output=True,text=True,timeout=4).stdout
            samples.append(state)
            if 'BackgroundOnly' in state:break
            time.sleep(.25)
        code=proc.wait(timeout=150)
    except BaseException:
        proc.kill();proc.wait();raise
log=(o/'run.log').read_text();changed=[p for p,h in protected.items() if sha(Path(p))!=h]
result={'passed':code==0 and '0 failure(s)' in log and any('BackgroundOnly' in s for s in samples) and not changed and (o/'h_game1.png').exists(),'runtime':str(r),'binary_sha256':sha(r/'opencnc3d'),'exit':code,'background_samples':samples,'settings_saves_preserved':not changed,'changed':changed,'captures':[str(p) for p in o.glob('*.png')]}
(o/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));assert result['passed']
