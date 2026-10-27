#!/usr/bin/env python3
"""Native macOS containers/choice matrix with inert fixtures, never installs/runs a game."""
import hashlib,importlib.util,json,os
from pathlib import Path
import plistlib,shutil,stat,subprocess,tempfile,zipfile
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('mac_packages',ROOT/'tools/mac/installer/package.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
def main():
 with tempfile.TemporaryDirectory(prefix='mac-installer-fixture-',dir=ROOT/'build')as name:
  work=Path(name);base=work/'base';base.mkdir();modules=work/'modules';modules.mkdir();out=work/'out';out.mkdir()
  source=work/'inert.c';source.write_text('#include <stdio.h>\nint main(int n,char **v){if(n==241)puts("/api/builds --module-file --media-game --media-add-only 0.9.0");return 0;}\n')
  binary=work/'inert';subprocess.run(['cc','-arch','arm64','-arch','x86_64','-mmacosx-version-min=14.0',str(source),'-o',str(binary)],check=True)
  for rel in p.BOOTSTRAP:
   q=base/rel;q.parent.mkdir(parents=True,exist_ok=True)
   if rel=='opencnc3d-launcher'or rel.endswith('.dylib'):shutil.copy2(binary,q)
   else:q.write_bytes(b'INERT PACKAGE-CONTAINER FIXTURE\n')
  app=base/'OpenCNC3D.app/Contents';(app/'MacOS').mkdir(parents=True);(app/'Resources').mkdir()
  shutil.copy2(binary,app/'MacOS/opencnc3d-launch');shutil.copy2(ROOT/'tools/launchers/OpenCNC3D.app/Contents/Resources/AppIcon.icns',app/'Resources/AppIcon.icns')
  (app/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleExecutable='opencnc3d-launch',CFBundleIdentifier='com.slipgate.fixture.opencnc3d',CFBundleName='OpenCNC3D',CFBundlePackageType='APPL',CFBundleVersion='0.9.0')))
  (base/'opencnc3d-install.txt').write_text('version 0.9.0\ndata_id fixture\n')
  catalog=['modules 1','version 0.9.0']
  for game in ['td','ra']:
   files=({'opencnc3d':binary.read_bytes(),'cnc_eyes':binary.read_bytes(),'TiberianDawn.dylib':binary.read_bytes(),'CONQUER.MIX':b'INERT','dosmenu.pack':b'INERT'}if game=='td'else{'ra3d/opencnc3d':binary.read_bytes(),'ra3d/RedAlert.dylib':binary.read_bytes(),'ra3d/dosmenu.pack':b'INERT','ra3d/SCG01EA.pack':b'INERT','efx.pack':b'INERT','groundmask_RA_SNOW.pack':b'INERT'})
   text=['module-files 1',f'module {game}','version 0.9.0','platform macos']
   q=modules/f'OpenCNC3D-module-{game}-macos-v0.9.0.zip'
   with zipfile.ZipFile(q,'w',zipfile.ZIP_DEFLATED)as z:
    for rel,b in files.items():
     info=zipfile.ZipInfo(rel);info.external_attr=(stat.S_IFREG|0o755)<<16;info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,b);text.append(f'file {hashlib.sha256(b).hexdigest()} {len(b)} {rel}')
    z.writestr(f'opencnc3d-module-{game}.txt','\n'.join(text)+'\n')
   catalog.append(f'module {game} macos {q.name} {q.stat().st_size} {p.sha(q)}')
  (modules/'OpenCNC3D-v0.9.0-modules.txt').write_text('\n'.join(catalog)+'\n')
  subprocess.run([str(ROOT/'tools/mac/make-release-packages-mac.sh'),'--version','0.9.0','--base-stage',str(base),'--module-dir',str(modules),'--out',str(out)],check=True)
  r=json.loads((out/'macos-package-receipt-v0.9.0.json').read_text())
  assert len(r['artifacts'])==5 and r['native_container_and_choice_checks_passed']and not r['gui_launched']
  for a in r['artifacts']:
   if a['kind']=='native-installer':
    names={x['path']for x in a['payload_files']}
    assert not any(x.endswith(('.cfg','.sav'))for x in names)
    if a['game']is None:assert not any('TiberianDawn'in x or'ra3d/'in x or x.startswith('.opencnc3d-modules/')for x in names)
   else:
    with zipfile.ZipFile(a['path'])as z:
     assert not any(x.endswith(('.pack','.cfg','.sav'))for x in z.namelist())
     assert ('ra3d/RedAlert.dylib'in z.namelist())==(a['game']=='ra')
     assert ('TiberianDawn.dylib'in z.namelist())==(a['game']=='td')
  print('MAC-INSTALLER-FIXTURE|PASS|three real native PKGs; all four Complete media choices; two distinct binary ZIPs; exact payload/container hashes; no GUI/system install; temporary inert fixtures retired')
if __name__=='__main__':main()
