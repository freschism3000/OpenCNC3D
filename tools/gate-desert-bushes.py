#!/usr/bin/env python3
"""Exercise source-placed bushes, density independence, the F5 switch and radiance."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--playable',type=Path,required=True);p.add_argument('--cfg',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();D=a.playable.resolve();O=a.output.resolve();O.mkdir(parents=True,exist_ok=True)
protected={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in D.glob('*.cfg')}
lines=['gfxmode 2','gfx texset 3','gfx show_stats 0','reveal 1','gfx grass 0','gfx cloud_on 0','gfx fog_fx 0','gfx sand_fx 0','gfx bloom_on 0','gfx lights_on 0','gfx fol_bush_amt 0','gfx fol_on 1','gfx fol_gain 0.10','gfx fol_shadows 1','cam 27 30','dist 1920','tick 2',f'shot {O}/on.png','foldump','gfx fol_gain 0.20',f'shot {O}/gain.png','gfx fol_gain 0.10','gfx fol_on 0',f'shot {O}/off.png','foldump','gfx fol_on 1','gfx sun_energy 0','gfx sky_energy 0',f'shot {O}/zero.png','quit']
(O/'commands.txt').write_text('\n'.join(lines)+'\n');args=[str(D/'cnc_eyes'),'--nosound','--enhanced','--texset','3','--gfx',str(a.cfg.resolve()),'--noshroud','--nosidebar','--nominimap','--w','1200','--h','800','--scen','SCB01EA','--pack','SCB01EA.pack','--dir','missions/','--dylib','TiberianDawn.dylib','--content','content/','--savedir',str(O/'saves'),'--gfxsave',str(O/'discard.cfg'),'--script',str(O/'commands.txt')]
with(O/'run.log').open('w')as log:r=subprocess.run(args,cwd=D,env=dict(os.environ,OPENCNC3D_BACKGROUND='1',SDL_MAC_BACKGROUND_APP='1'),stdout=log,stderr=subprocess.STDOUT,timeout=240)
s=(O/'run.log').read_text(errors='replace');assert r.returncode==0 and re.search(r'SCRIPT\|end .*0 failures',s),s[-1200:];assert not re.search(r'ERROR: 0:|FAILED TO LINK',s)
counts=[int(x)for x in re.findall(r'FOLDUMP\|[^\n]*?\|bushes=(\d+)',s)];assert len(counts)==2 and counts[0]>0 and counts[1]==0,counts
ims={k:np.asarray(Image.open(O/(k+'.png')).convert('RGB'),float)for k in ['on','off','gain','zero']};mask=np.max(np.abs(ims['on']-ims['off']),axis=2)>8;assert mask.sum()>512,'Switch did not remove visible mesh patches'
gain=(ims['gain']-ims['on']).mean(2);assert np.percentile(gain[mask],75)>.5,'F5 bush gain did not reach source-placed bushes'
assert np.percentile(ims['zero'][mask],90)<=5,'Source bushes emit light with sun and sky disabled'
assert all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in protected.items()),'User settings changed'
j=dict(passed=True,authored_bushes_with_scatter_density_zero=counts[0],bushes_when_disabled=counts[1],changed_pixels=int(mask.sum()),gain_p75_delta=float(np.percentile(gain[mask],75)),zero_energy_p90=float(np.percentile(ims['zero'][mask],90)),settings_preserved=True);(O/'RESULTS.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps(j,indent=2))
