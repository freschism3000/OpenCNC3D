#!/usr/bin/env python3
"""Hidden installed RA Settings clicks, real feature switches and CFG reload."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('install',type=Path);p.add_argument('--evidence',required=True,type=Path);a=p.parse_args()
install=a.install.resolve();out=a.evidence.resolve();out.mkdir(parents=True,exist_ok=True)
def state(path):
 return {m[1]:float(m[2]) for line in path.read_text().splitlines() if (m:=re.match(r'^([^#\s]+)\s+([-+.0-9]+)',line))}
reports=[]
for mission in ['SCU02EA','SCG01EA','SCG05EA']:
 d=out/mission;d.mkdir(exist_ok=True)
 # The user's old preset intentionally has no new marker: test the same upgrade path.
 source=install/'ra3d-fx.cfg';cfg=d/'legacy.cfg'
 legacy='\n'.join(l for l in source.read_text().splitlines() if not l.startswith(('ra_enhanced_features','enh_world_detail')))+'\n'
 for key in ['new_hud','tree3d','grass','tib3d','fol_on','rock_on','mottle_on']+[f'mode.enhanced.{k}' for k in ['new_hud','tree3d','grass','tib3d','fol_on','rock_on','mottle_on']]:
  legacy=re.sub(r'(?m)^('+re.escape(key)+r'\s+)\S+[^\n]*',r'\g<1>0',legacy)
 cfg.write_text(legacy)
 script=d/'walk.script';script.write_text('\n'.join([
  'tick 3','reveal 1','dist 2200','gfx fol_bush_amt 0.6',('cam 56.5 68.5' if mission=='SCG05EA' else 'cam 37.5 66' if mission=='SCU02EA' else 'cam 52 60'),'options','emclick Visuals','emclick World','optvis',f'shot {d}/world-on.png',
  'emclick 3D Trees','emclick 3D Grass','emclick 3D Ore','optvis',f'gfxsave {d}/world-off.cfg','emclick More Settings','emclick Bushes','emclick Rocks','emclick Ground patches',f'shot {d}/detail-off.png','emclick Interface','emclick New HUD','optvis',
  f'gfxsave {d}/all-off.cfg',f'shot {d}/interface-off.png','emclick New HUD','emclick World','emclick 3D Trees','emclick 3D Grass','emclick 3D Ore','optvis','emclick More Settings','emclick Bushes','emclick Rocks','emclick Ground patches',f'shot {d}/detail-on.png',
  f'gfxsave {d}/all-on.cfg','emclick Interface',f'shot {d}/interface-on.png','emclick OK','emclick Resume Mission',
  f'shot {d}/world-native-on.png','ra3dprobe','rahdump','foldump','mottledump','raore',
  f'gfxload {d}/all-off.cfg',f'shot {d}/world-native-off.png','ra3dprobe','rahdump','foldump','mottledump','raore',
  'options','emclick Visuals','emclick World','optvis','emclick Interface','optvis','emclick OK','emclick Resume Mission',
  f'gfxload {d}/all-on.cfg',f'shot {d}/world-native-restored.png','ra3dprobe','rahdump','foldump','mottledump','raore','quit'])+'\n')
 cmd=[str(install/'cnc_eyes'),'--ra3d','--dylib','ra3d/RedAlert.dylib','--content','ra3d/data','--dir','ra3d/missions/','--scen',mission,'--pack',f'ra3d/3d/{mission}.pack','--dosdata','ra3d/audio','--enhanced','--nosound','--w','1280','--h','720','--gfx',str(cfg),'--script',str(script),'--gfxsave',str(d/'scratch.cfg')]
 result=subprocess.run(cmd,cwd=install,env={**os.environ,'OPENCNC3D_BACKGROUND':'1'},stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180);log=result.stdout;(d/'run.log').write_text(log)
 assert result.returncode==0 and re.search(r'SCRIPT\|end .*: \d+ lines, 0 failures',log),log[-1800:]
 assert 'defaults adopted once' in log
 for file,want in [('all-off.cfg',0),('all-on.cfg',1)]:
  values=state(d/file)
  for key in ['new_hud','tree3d','grass','tib3d','fol_on','rock_on','mottle_on']+[f'mode.enhanced.{k}' for k in ['new_hud','tree3d','grass','tib3d','fol_on','rock_on','mottle_on']]:assert values[key]==want,(file,key,values[key])
  assert values['enh_world_detail']==1
  assert values['ra_enhanced_features']==1
  old=state(cfg)
  for key in ['display_mode','res_w','res_h','vsync','ui_scale']:assert values[key]==old[key],(file,key)
 assert '|hud=0|fx_hud=0|sb_hud=0' in log
 assert '|hud=1|fx_hud=1|sb_hud=1' in log
 assert '|fx_trees=0|fx_grass=0|' in log and '|fx_trees=1|fx_grass=1|' in log
 assert '|enabled=0|pack=1|' in log and '|enabled=1|pack=1|' in log
 treeCounts=[int(v) for v in re.findall(r'\|treemodels=(\d+)',log)]
 assert len(treeCounts)==3 and treeCounts[1]==0 and treeCounts[0]==treeCounts[2],treeCounts
 if mission=='SCU02EA':
  assert treeCounts[0]>0,treeCounts
  assert re.search(r'GRASS\|draw\|.*\|blades=[1-9][0-9]*',log),'No live grass blades drawn'

 if mission=='SCG05EA':
  ore=[dict(re.findall(r'([^|=]+)=([^|]+)',l)) for l in log.splitlines() if l.startswith('RA3DORE|end|')]
  assert len(ore)==3 and int(ore[0]['drawn'])>0 and int(ore[1]['drawn'])==0 and int(ore[2]['drawn'])>0,ore
  assert int(ore[0]['oredecals'])==0 and int(ore[1]['oredecals'])>0,ore
 if mission=='SCU02EA':
  groups=log.split('RA3DPROBE|frame|')[1:]
  for part in [groups[0],groups[2]]:
   clumps=re.findall(r'RA3DPROBE\|terrain\|(TC0[1-5])\|[^\n]+\|model=(\d)',part)
   assert clumps and all(int(v)==1 for _,v in clumps),clumps
  fol=[dict(re.findall(r'([^|=]+)=([^|]+)',l)) for l in log.splitlines() if l.startswith('FOLDUMP|')]
  assert len(fol)==3 and int(fol[0]['drawn'])>0 and int(fol[1]['drawn'])==0 and int(fol[2]['drawn'])>0,fol
  mot=[dict(re.findall(r'([^|=]+)=([^|]+)',l)) for l in log.splitlines() if l.startswith('MOTTLE|')]
  assert len(mot)==3 and int(mot[0]['drawn'])>0 and int(mot[1]['drawn'])==0 and int(mot[2]['drawn'])>0,mot
 report={'mission':mission,'clicks':'HUD, Trees, Grass, Ore, Bushes, Rocks, Ground patches off and on','cfgReload':'off choices persist; on choices restore','displayPreferences':'unchanged','scriptFailures':0,'treeModels':treeCounts}
 reports.append(report);print('RAOPTIONS|PASS|'+mission+'|real mouse seats, live switches, save/reload, display preferences',flush=True)
(out/'results.json').write_text(json.dumps(reports,indent=2)+'\n')
