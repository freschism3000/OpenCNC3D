#!/usr/bin/env python3
"""A calibration-less pack must block staging; a fresh bake reaches TD and RA."""
from pathlib import Path
import hashlib,shutil,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cnc3d-bib-stage-test-')as temp:
 r=Path(temp);tool=r/'tools/mac/stage-runtime.py';tool.parent.mkdir(parents=True);shutil.copy2(ROOT/'tools/mac/stage-runtime.py',tool);game=r/'game';game.mkdir();runtime=r/'playable';runtime.mkdir();(runtime/'ra3d').mkdir();names=['opencnc3d','cnc3d','ra3d/opencnc3d','ra3d/cnc3d']
 for name in names:(runtime/name).write_bytes(b'old executable sentinel')
 binary=r/'built-app';binary.write_bytes(b'OPENCNC3D|build|vfixture|\n');pack=game/'bibenh.pack';pack.write_bytes(b'BIBENH3\0'+bytes([3,0,0,0]))
 a=[sys.executable,str(tool),str(runtime),str(binary)];p=subprocess.run(a,capture_output=True,text=True);assert p.returncode!=0 and 'calibrated' in p.stderr
 assert all((runtime/name).read_bytes()==b'old executable sentinel'for name in names),'stale asset modified installed binaries'
 subprocess.run([sys.executable,str(ROOT/'tools/art/remaster/bib/bake_bibenh.py'),'--pack',str(pack)],check=True,stdout=subprocess.DEVNULL);p=subprocess.run(a,capture_output=True,text=True,check=True)
 assert all((runtime/name).read_bytes()==binary.read_bytes()for name in names)
 assert (runtime/'bibenh.pack').read_bytes()==pack.read_bytes();assert pack.read_bytes()[:12]==b'BIBENH4\0\x04\0\0\0'
 print('PASS stale bib blocks delivery before any executable changes; fresh approved bake reaches all TD/RA aliases')
