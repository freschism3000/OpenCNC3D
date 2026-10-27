#!/usr/bin/env python3
"""Build private local reinstall packages from exact ownership, never player files.
The source installation must contain current-main assets. --frontend and --ra-menu
must come from the matching clean main build. This tool is not a public catalog.
"""
import argparse,hashlib,json,os,re,stat,zipfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('installation',type=Path);p.add_argument('output',type=Path);p.add_argument('--frontend',type=Path,required=True);p.add_argument('--ra-menu',type=Path,required=True);p.add_argument('--version',required=True);a=p.parse_args();root=Path(__file__).resolve().parents[2]
assert '-dirty' not in a.version
ownership=(root/'launcher/lmodule_files.h').read_text();a.output.mkdir(parents=True,exist_ok=True);report=[]
for game in ['td','ra']:
 names=re.findall(r'"([^"]+)"',ownership.split('lmod_'+game+'_files[] = {',1)[1].split('};',1)[0]);name='ra3d' if game=='ra' else 'td';target=a.output/(name+'.zip');part=target.with_suffix('.zip.ocpart');files=[]
 with zipfile.ZipFile(part,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=False) as z:
  for rel in names:
   src=a.frontend if rel=='ra3d/opencnc3d' else a.ra_menu if rel=='ra3d/enhanced-menu.pack' else a.installation/rel
   if not src.exists():continue # alternate-platform files are absent
   assert src.is_file() and not src.is_symlink(),src
   data=src.read_bytes();info=zipfile.ZipInfo(rel,(2026,10,24,0,0,0));info.create_system=3;info.external_attr=(stat.S_IFREG|stat.S_IMODE(src.stat().st_mode))<<16;info.compress_type=zipfile.ZIP_DEFLATED
   z.writestr(info,data);files.append({'path':rel,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
 required=[n for n in re.findall(r'"(ra3d/[^"]+)"',(root/'launcher/lmodule.c').read_text().split('lmod_ra_needs[] = {',1)[1].split('};',1)[0]) if not n.endswith(('.exe','.dll'))] if game=='ra' else ['TiberianDawn.dylib','CONQUER.MIX'];assert set(required)<={f['path']for f in files}
 os.replace(part,target);digest=hashlib.sha256(target.read_bytes()).hexdigest();Path(str(target)+'.sha256').write_text(digest+'\n');Path(str(target)+'.version').write_text(a.version.removeprefix('v')+'\n');report.append({'game':name,'version':a.version,'sha256':digest,'compressed_bytes':target.stat().st_size,'files':files});print('MODULE-CACHE|'+name+'|files='+str(len(files))+'|bytes='+str(target.stat().st_size),flush=True)
(a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
