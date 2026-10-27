#!/usr/bin/env python3
"""Exercise the generated comparison scripts with native binaries and private writes."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

HERE=Path(__file__).resolve().parent
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
maker=load('maker',HERE/'make-comparison.py');analyzer=load('analyzer',HERE/'analyze-comparison.py')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',required=True,type=Path);p.add_argument('--binary',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--gfx',type=Path);p.add_argument('--legacy-smoke',action='store_true');a=p.parse_args()
    repo=a.repo.resolve();binary=a.binary.resolve();out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
    assets=repo/'playable';before={p.name:sha(p) for p in assets.glob('*.cfg')};binary_hash=sha(binary)
    maker.build(repo,out/'packet');fixture=out/'packet/Performance-Comparison'
    source_cfg=a.gfx.resolve() if a.gfx else assets/'opencnc3d-fx.cfg'
    if source_cfg.is_file():shutil.copy2(source_cfg,out/'input.cfg')
    samples=maker.scripts()
    if a.legacy_smoke:
        samples={k:'\n'.join(x for x in text.splitlines() if x not in ('sunstaticdump','resourceprepdump'))+'\n' for k,text in samples.items()}
    for name,text in samples.items():(fixture/name).write_text(text)
    runs=[]
    report=out/'report';report.mkdir()
    variants=[('baseline-1','0'),('optimized-1','1'),('optimized-2','1'),('baseline-2','0')]
    if a.legacy_smoke:variants=variants[:1]
    with open('/private/tmp/opencnc3d-performance-gpu.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for variant,value in variants:
            for case in ('active','siege'):
                scen,pack,directory,script=('SCG94EA','SCB01EA.pack',fixture,fixture/'active.txt') if case=='active' else ('SCB35EA','SCB35EA.pack',assets/'missions',fixture/'siege.txt')
                label=variant+'-'+case;raw=out/(label+'.raw.log')
                args=[str(binary),'--enhanced','--scen',scen,'--build','98','--pack',str(assets/pack),'--dir',str(directory)+'/',
                      '--content',str(assets/'content')+'/', '--dylib',str(assets/'TiberianDawn.dylib'),'--nosound','--ticks','0',
                      '--w','1920','--h','1080','--gfxsave',str(out/(label+'.cfg')),'--savedir',str(out/'saves'),'--script',str(script)]
                if (out/'input.cfg').is_file():args+=['--gfx',str(out/'input.cfg')]
                env=dict(os.environ,OPENCNC3D_BACKGROUND='1',OPENCNC3D_FRAMEPROF='1',OPENCNC3D_PERFDETAIL='1')
                for name in maker.CONTROLS:env[name]=value
                for name in ('OPENCNC3D_PERFTRACE','OPENCNC3D_GPUPROF'):env.pop(name,None)
                for name in ('OPENCNC3D_MESH_FRAGMENT_REUSE','OPENCNC3D_MESH_PREPARED_BUDGET_FULL','OPENCNC3D_MESH_PREPARED_ALLOC_OK','OPENCNC3D_MESH_FRAGMENT_GPU_OK','OPENCNC3D_MESH_FRAGMENT_PACK_OK','OPENCNC3D_MESH_FRAGMENT_PACK_ALLOC_OK','OPENCNC3D_MESH_FRAGMENT_BUFFER_OK','OPENCNC3D_MESH_CACHE_FULL','OPENCNC3D_MESH_CACHE_ALLOC_OK'):env[name]='1'
                start=time.perf_counter()
                with raw.open('w') as log:result=subprocess.run(args,cwd=assets,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
                text=raw.read_text(errors='replace');assert result.returncode==0,(label,result.returncode)
                assert ', 0 failures' in text,label
                selected=[x for x in text.splitlines() if x.startswith(('ECHO|BENCH|','FRAMEBENCH|','FRAMEPROF|','FRAGMENTPREP|','SUNSTATIC|','EBLDPREPARE|','MESHPERF|','MESHPOOL|','FRAGMENTREUSE|','EBLDOBJ|','GL_RENDERER','GL_VERSION'))]
                dest=report/(label+'.log');dest.write_text('\n'.join(selected)+'\n')
                phases,_,_=analyzer.read(dest)
                if case=='active':assert all(len(phases[name])==32 for name in ('moving','settling','sinking','late'));assert '|bodies=321|' in text,'Installed NUKE321 fixture was not exercised'
                else:assert len(phases['active-siege'])==80
                runs.append({'label':label,'exit':result.returncode,'process_wall_seconds':time.perf_counter()-start,'controls':{k:env[k] for k in maker.CONTROLS},'sample_counts':{k:len(v) for k,v in phases.items()}})
                print(label,'PASS',flush=True)
    assert {p.name:sha(p) for p in assets.glob('*.cfg')}==before,'Installed configuration changed'
    assert sha(binary)==binary_hash
    receipt={'passed':True,'native_scripts_tested':True,'windows_batch_executed':False,'legacy_smoke':a.legacy_smoke,'binary_sha256':binary_hash,'installed_settings_unchanged':True,'visible_window':False,'runs':runs}
    if not a.legacy_smoke:receipt['comparison']=analyzer.analyze(report)
    (out/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:v for k,v in receipt.items() if k not in ('comparison','runs')},indent=2))


if __name__=='__main__':main()
