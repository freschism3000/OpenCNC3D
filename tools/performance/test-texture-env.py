#!/usr/bin/env python3
"""Actual native texture-state cache proof. macOS, hidden contexts, private output.
Windows renderer compilation and Windows play/performance acceptance are separate.
"""
from pathlib import Path
import argparse,fcntl,hashlib,json,os,shlex,subprocess,sys
ap=argparse.ArgumentParser();ap.add_argument('--repo',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.repo=a.repo.resolve();a.out=a.out.resolve()
if sys.platform!='darwin':raise SystemExit('Native context proof requires macOS.')
if Path('/private/tmp') not in a.out.parents:raise SystemExit('Use a fresh private /private/tmp output folder.')
a.out.mkdir(parents=True,exist_ok=True);sdk=Path.home()/'.opencnc3d-macsdk';cfg=Path(os.environ.get('SDL2_CONFIG',str(sdk/'bin/sdl2-config')));exe=a.out/'texture-env-native';src=Path(__file__).with_name('texture-env-native.cpp');header=a.repo/'game/gl_state_cache.h'
cmd=['clang++','-std=c++14','-O2','-DGL_SILENCE_DEPRECATION','-I'+str(a.repo/'game'),'-o',str(exe),str(src)]+shlex.split(subprocess.check_output([str(cfg),'--cflags'],text=True))+shlex.split(subprocess.check_output([str(cfg),'--libs'],text=True))+['-Wl,-rpath,'+str(sdk/'lib'),'-framework','OpenGL'];env={**os.environ,'MACOSX_DEPLOYMENT_TARGET':'14.0','OPENCNC3D_BACKGROUND':'1'}
subprocess.run(cmd,env=env,check=True,stdout=(a.out/'compile.txt').open('w'),stderr=subprocess.STDOUT)
rows=[]
with open('/private/tmp/opencnc3d-performance-gpu.lock','a') as lock:
 fcntl.flock(lock,fcntl.LOCK_EX)
 for mode in ('0','1'):
  p=subprocess.run([str(exe),mode],env=env,text=True,capture_output=True,timeout=60);(a.out/('cache-'+mode+'.txt')).write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr;rows.append({'cache':mode,'exit':p.returncode,'stdout':p.stdout.splitlines()})
result={'passed':True,'actual_header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'hidden_contexts':True,'native_windows_runtime':False,'results':rows};(a.out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
