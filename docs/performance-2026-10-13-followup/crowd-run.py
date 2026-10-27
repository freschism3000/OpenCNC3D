import os,json,re,statistics,subprocess,argparse,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path);p.add_argument('--uniform-exe',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
w=Path(__file__).resolve().parent;runtime=a.runtime.resolve() if a.runtime else w.parents[1]/'playable';output=a.output.resolve() if a.output else Path(tempfile.mkdtemp(prefix='opencnc3d-crowd-'));v=json.loads((w/'crowded-infantry-invocation.json').read_text());script=(w/'crowded-infantry-script.txt').read_text();records=[]
def paths(s):return s.replace('${REPO_ROOT}/playable',str(runtime)).replace('${REPO_ROOT}',str(w.parents[1])).replace('${EVIDENCE_ROOT}',str(w))
v['args']=[paths(s)for s in v['args']];v['cwd']=paths(v['cwd'])
if os.name=='nt':v['args'][0]=str(runtime/'cnc_eyes.exe');v['args'][v['args'].index('--dylib')+1]=str(runtime/'TiberianDawn.dll')
legs=[('baseline',None,None),('baseline',None,None)]
if a.uniform_exe:legs += [('uniform',str(a.uniform_exe.resolve()),x)for x in [0,1,1,0]]
for leg,(name,exe,value) in enumerate(legs):
 d=output/str(leg);d.mkdir(parents=True,exist_ok=True);(d/'saves').mkdir(exist_ok=True);(d/'script.txt').write_text(script.replace('${OUTPUT_ROOT}',str(d)))
 cmd=v['args'].copy()
 if exe:cmd[0]=exe
 cmd[cmd.index('--savedir')+1]=str(d/'saves');cmd[cmd.index('--script')+1]=str(d/'script.txt')
 env={**os.environ,**v['env']}
 if value is not None:env['OPENCNC3D_UNIFORM_VALUES']=str(value)
 with(d/'run.log').open('w')as f:q=subprocess.run(cmd,cwd=v['cwd'],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 t=(d/'run.log').read_text(errors='replace');assert q.returncode==0,(leg,q.returncode);assert re.search(r'SCRIPT\|end .*: \d+ lines, 0 failures',t),(leg,t[-1000:])
 vals=[float(x)for x in re.findall(r'^FRAMEBENCH\|n=1\|median=([0-9.]+)',t,re.M)];assert len(vals)==120,len(vals)
 m3=re.findall(r'^(?:M3DUMP|M3POSE|M3INFLIGHT|M3INF|M3GAIT|COUNT|OBJ|ORDER|EXPECTSEL|SELECTION|UNIFORMPERF).*$',t,re.M)
 row={'leg':leg,'name':name,'value':value,'samples':120,'median_ms':statistics.median(vals),'p95_ms':sorted(vals)[114],'max_ms':max(vals),'witnesses':m3,'profile_windows':re.findall(r'^FRAMEPROF.*$',t,re.M),'mesh_windows':re.findall(r'^MESHPERF.*$',t,re.M),'args':cmd,'log':str(d/'run.log')};records.append(row);(output/'crowd-results.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps({k:row[k]for k in ['leg','name','value','samples','median_ms','p95_ms']}),flush=True)
