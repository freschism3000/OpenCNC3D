#!/usr/bin/env python3
"""Run gate-module-reinstall in a new disposable fixture, no network/window."""
from pathlib import Path
import argparse,re,hashlib,stat,zipfile,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('gate',type=Path);p.add_argument('scratch',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];install=a.scratch;install.mkdir(parents=True,exist_ok=False)
for rel in ['opencnc3d-install.txt','opencnc3d','TiberianDawn.dylib','CONQUER.MIX','savegame.sav','ra3d/missions/custom-map.ini']:
 f=install/rel;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('keep exact\n')
menu_candidates=[root/'art/interface/ra3d-title-cleanup-v01/evidence/review-install/ra3d/dosmenu.pack',Path.home()/'CNC3D-latest/playable/ra3d/dosmenu.pack',Path.home()/'CNC3D/playable/ra3d/dosmenu.pack']
menu=next((p for p in menu_candidates if p.is_file()),None)
if not menu: raise SystemExit('Pass a checkout/runtime with a real RA dosmenu.pack fixture')
version=re.search(r'#define LCFG_BUILD "([^"]+)"',(root/'launcher/lcfg.h').read_text())[1];cache=install/'.opencnc3d-modules';cache.mkdir();target=cache/'ra3d.zip'
needs=re.findall(r'"(ra3d/[^\"]+)"',(root/'launcher/lmodule.c').read_text().split('lmod_ra_needs[] = {',1)[1].split('};',1)[0]);needs=[n for n in needs if not n.endswith(('.exe','.dll'))]
with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:
 for rel in needs:
  data=menu.read_bytes() if rel=='ra3d/dosmenu.pack' else ('fixture '+rel).encode()
  info=zipfile.ZipInfo(rel);info.external_attr=(stat.S_IFREG|0o755)<<16;info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,data)
Path(str(target)+'.sha256').write_text(hashlib.sha256(target.read_bytes()).hexdigest()+'\n');Path(str(target)+'.version').write_text(version+'\n')
with zipfile.ZipFile(install/'foreign.zip','w') as z:z.writestr('savegame.sav','do not overwrite')
subprocess.run([str(a.gate.resolve()),str(install.resolve())],check=True)
assert (install/'savegame.sav').read_text()=='keep exact\n';assert (install/'ra3d/missions/custom-map.ini').read_text()=='keep exact\n'
