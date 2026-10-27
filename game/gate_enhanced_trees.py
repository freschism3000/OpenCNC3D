#!/usr/bin/env python3
"""Hidden installed-model regression. Uses G307's source-authored terrain fixture.
Checks actual remaster draw counts, a visible on/off difference and exact restoration.
Never opens/focuses a visible window, and writes only to the provided proof directory.
"""
import os, re, subprocess, sys
from pathlib import Path
from PIL import Image, ImageChops
import gate_eterr

def run(out):
    out=Path(out).resolve(); out.mkdir(parents=True,exist_ok=True)
    gate_eterr.maps(str(out))
    script=out/'tree-toggle.txt'
    script.write_text('\n'.join([
        'tick 3','cam 33 48.5','zoom max','tick 1',
        'gfx tree3d 1',f'shot {out}/on.png','ebldump',
        'gfx tree3d 0',f'shot {out}/off.png','ebldump',
        'gfx tree3d 1',f'shot {out}/restored.png','ebldump','quit'])+'\n')
    env=dict(os.environ,OPENCNC3D_BACKGROUND='1',SDL_AUDIODRIVER='dummy')
    cmd=['./cnc_eyes','--nosound','--noshroud','--scen','SCG85EA',
         '--pack','SCG10EA.pack','--dir',str(out)+'/', '--dylib','TiberianDawn.dylib',
         '--content','content','--dosdata','dosdata','--gfx','--w','1280','--h','720',
         '--script',str(script)]
    result=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log=result.stdout.decode('latin-1'); (out/'trees.log').write_text(log)
    assert result.returncode==0, ('runtime failed', result.returncode, log[-1000:])
    counts=[[]]
    for line in log.splitlines():
        if line.startswith('EBLD|loaded='):
            counts.append([])
        if line.startswith('EBLDART|T') and re.match(r'EBLDART\|T\d\d_',line):
            counts[-1].append(int(re.search(r'\|drawn=(\d+)',line).group(1)))
    totals=[sum(x) for x in counts if x]
    assert len(totals)==3 and totals[0]>0 and totals[1]==0 and totals[2]>0,totals
    a,b,c=[Image.open(out/(n+'.png')).convert('RGB') for n in ('on','off','restored')]
    assert ImageChops.difference(a,c).getbbox() is None,'restored tree scene changed'
    diff=ImageChops.difference(a,b); pixels=sum(1 for p in diff.getdata() if max(p)>12)
    assert pixels>=1000,('no meaningful tree difference',pixels)
    assert not Path('tree3d.pack').exists(),'legacy installed tree asset'
    assert 'trees: tree3d.pack' not in log,'legacy tree loader ran'
    print('ENHTREES|PASS|draw counts=%s|changed pixels=%d|exact restoration|no imported tree pack'%(totals,pixels))
if __name__=='__main__': run(sys.argv[1])
