#!/usr/bin/env python3
"""Native, hidden GL state proof using the supplied repository's actual cache header.
macOS host; no installed binaries/settings/assets are modified. Windows renderer
cross-compilation and native Windows acceptance are separate checks.
"""
from pathlib import Path
import argparse,json,os,shlex,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.repo=a.repo.resolve();a.out=a.out.resolve()
if sys.platform!='darwin':raise SystemExit('This native context runner requires macOS; use the Windows renderer compiler separately.')
if a.out!=Path('/private/tmp') and Path('/private/tmp') not in a.out.parents:raise SystemExit('Use a private /private/tmp output directory; this runner never stages runtime files.')
a.out.mkdir(parents=True,exist_ok=True);cfg=Path(os.environ.get('SDL2_CONFIG',str(Path.home()/'.opencnc3d-macsdk/bin/sdl2-config')));exe=a.out/'exact-state-reuse-native';src=Path(__file__).with_name('exact-state-reuse-native.cpp');cmd=['clang++','-std=c++14','-O2','-DGL_SILENCE_DEPRECATION','-I'+str(a.repo/'game'),'-o',str(exe),str(src)]+shlex.split(subprocess.check_output([str(cfg),'--cflags'],text=True))+shlex.split(subprocess.check_output([str(cfg),'--libs'],text=True))+['-Wl,-rpath,'+str(Path.home()/'.opencnc3d-macsdk/lib'),'-framework','OpenGL'];env={**os.environ,'MACOSX_DEPLOYMENT_TARGET':'14.0','OPENCNC3D_BACKGROUND':'1'}
subprocess.run(cmd,env=env,check=True,stdout=(a.out/'compile.txt').open('w'),stderr=subprocess.STDOUT)
rows=[]
for mode in ('0','1'):
 q=subprocess.run([str(exe),mode],env=env,text=True,capture_output=True,timeout=30);(a.out/('cache-'+mode+'.txt')).write_text(q.stdout+q.stderr);assert q.returncode==0,q.stderr;rows.append({'cache':mode,'exit_code':q.returncode,'lines':q.stdout.splitlines()})
result={'passed':True,'hidden_only':True,'actual_header':True,'native_windows_runtime':False,'results':rows};(a.out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
