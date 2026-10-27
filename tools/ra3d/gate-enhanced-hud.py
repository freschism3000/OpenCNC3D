#!/usr/bin/env python3
"""Hidden installed RA HUD: all five categories, native-size matrix and mode isolation."""
from pathlib import Path
import argparse,json,os,re,subprocess,tempfile
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('install',type=Path);p.add_argument('--evidence',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];install=a.install.resolve();scratch=tempfile.TemporaryDirectory(prefix='ra-hud-');out=(a.evidence or Path(scratch.name)).resolve();out.mkdir(parents=True,exist_ok=True)
(out/'test.cfg').write_text((root/'docs/tuning/opencnc3d-fx.cfg').read_text())
kit=root/'art/interface/ra-enhanced-hud-v06-plain-tool-wing/kit';report=[]
for w,h in [(640,480),(1280,720),(2560,1440)]:
 for mission in ['SCG01EA','SCU02EA']:
  name=f'{mission}-{w}';s=max(1,h//693);x=w-216*s;rows=max(2,min(6,(h//s-309)//64));height=max(450,309+rows*64)
  lines=['tick 30','rahdump','sbwalk',f'shot {out/name}.png']
  for cat in range(5):lines += [f'rahcat {cat}','sbwalk','rahdump']
  lines += ['powerforce 200 500',f'shot {out/name}-red.png','powerdrawn','powerforce off',f'sbclick {x-24*s} {151*s}','rahdump',f'sbclick {x-24*s} {151*s}','sbslide hide','sbslide settle',f'shot {out/name}-collapsed.png','sbslide show','sbslide settle','gfxmode 0',f'shot {out/name}-classic.png','rahdump','gfxmode 2',f'shot {out/name}-returned.png','rahdump','quit']
  script=out/(name+'.script');script.write_text('\n'.join(lines)+'\n')
  cmd=[str(install/'cnc_eyes'),'--ra3d','--dylib','ra3d/RedAlert.dylib','--content','ra3d/data','--dir','ra3d/missions/','--scen',mission,'--pack',f'ra3d/3d/{mission}.pack','--dosdata','ra3d/audio','--enhanced','--nosound','--forceradar','--w',str(w),'--h',str(h),'--gfx',str(out/'test.cfg'),'--script',str(script)]
  log=subprocess.run(cmd,cwd=install,env={**os.environ,'OPENCNC3D_BACKGROUND':'1'},stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=150).stdout;(out/(name+'.log')).write_text(log)
  assert re.search(r'SCRIPT\|end .*: \d+ lines, 0 failures',log),name+' script failures'
  assert f'|rows={rows}|scale={s}|' in log,name+' geometry'
  assert '|enabled=0|pack=1|' in log,name+' Classic isolation'
  assert '|powerMode=1' in log,name+' power input'
  assert re.search(r'POWERDRAWN\|.*\|hud=2\|',log),name+' drawn overdraw warning'
  for cat in range(5):assert f'SIDEBAR-INPUT|category|{cat}' in log,name+' missing category'
  shots=np.array(Image.open(out/(name+'.png')).convert('RGB'));wing=np.array(Image.open(kit/'tools.png').convert('RGBA'))[:,:63];mask=wing[:,:,3]==255
  for k in range(3):mask[23+42*k:58+42*k,21:56]=False
  sample=shots[27*s:192*s:s,x-63*s:x:s];assert np.array_equal(sample[mask],wing[:,:,:3][mask]),name+' wing source pixels'
  art=np.array(Image.open(kit/'minimap.png').convert('RGBA'));mask=art[:,:,3]==255;mask[16:167,30:186]=False;sample=shots[18*s:205*s:s,x:w:s]
  assert np.array_equal(sample[mask],art[:,:,:3][mask]),name+' minimap source pixels'
  returned=np.array(Image.open(out/(name+'-returned.png')).convert('RGB'));assert returned.shape==shots.shape
  # No old TD bar leaks through after Classic->Enhanced; approved neutral header is back.
  # The six transparent bottom-edge pixels show the world behind the HUD. Restored
  # tree clumps change that world across the mode walk; compare native opaque art.
  header=np.array(Image.open(kit/'sidebar-normal.png').convert('RGBA'))[:,:6]
  opaque=header[:,:,3]==255;before=shots[0:17*s:s,w-216*s:w:s][:,:6];after=returned[0:17*s:s,w-216*s:w:s][:,:6]
  assert np.array_equal(after[opaque],before[opaque]),name+' return header'
  # The header also carries its native interaction intensity; the same-state
  # before/after pixels above must match exactly. Minimap and wing source RGB are
  # already checked independently in their untinted wells above.
  report.append({'mission':mission,'size':[w,h],'scale':s,'rows':rows,'height':height,'categories':5,'sourcePixels':'exact','classicIsolation':True,'powerWarning':'red','failures':0})
  print('RAHUD|PASS|'+name+'|rows='+str(rows)+'|scale='+str(s),flush=True)
(out/'matrix.json').write_text(json.dumps(report,indent=2)+'\n');print('RAHUD|PASS|6 installed cases; 5 categories each; exact source pixels; mode/power/slide controls')
