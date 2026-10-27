#!/usr/bin/env python3
"""Run production service header checks with GL stubs and actual EBLD model bytes.
No game starts and neither the repository nor installed game is modified.
"""
from pathlib import Path
import argparse,subprocess,shutil,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=Path);p.add_argument('--work',required=True,type=Path);p.add_argument('--models',type=Path);a=p.parse_args();a.repo=a.repo.resolve();a.work=a.work.resolve()
if a.work==a.repo or a.repo in a.work.parents:raise SystemExit('Use a private output directory outside the source checkout.')
a.work.mkdir(parents=True,exist_ok=True);(a.work/'tests').mkdir(exist_ok=True);g=a.work/'game'
if not g.exists():g.symlink_to(a.repo/'game',target_is_directory=True)
assert g.resolve()==(a.repo/'game').resolve()
h=Path(__file__).with_name('service-regression.cpp');c=a.work/'tests/service-regression.cpp';shutil.copyfile(h,c);exe=a.work/'service-regression'
subprocess.run(['clang++','-std=c++14','-O2',str(c),'-o',str(exe)],check=True)
models=a.models or a.repo/'playable/ebld';paths=[models/(t+'.ebld') for t in ['PROC','HPAD','FIX']]
before={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths};r=subprocess.run([str(exe),*(str(x) for x in paths)],capture_output=True,text=True);(a.work/'test.log').write_text(r.stdout+r.stderr);print(r.stdout.strip());assert r.returncode==0
assert before=={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}
(a.work/'result.json').write_text(json.dumps({'passed':True,'sourceHeaders':{n:hashlib.sha256((a.repo/'game'/n).read_bytes()).hexdigest() for n in ['building_service.h','building_service_types.h','building_service_bindings.h']},'modelHashes':before,'assetsUnchanged':True,'nativeRendering':False},indent=2))
