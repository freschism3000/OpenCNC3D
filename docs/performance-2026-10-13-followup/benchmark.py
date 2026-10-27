# Portable replay derivative; measured local-driver hashes are in driver-provenance.json.
import argparse,os,re,json,statistics,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe');p.add_argument('--runtime',type=Path);p.add_argument('--output',type=Path);p.add_argument('--label',default='v084');p.add_argument('--samples',type=int,default=120);p.add_argument('--repeat',type=int,default=1);p.add_argument('--cases',nargs='*',default=['forest','base','blast','bodies']);p.add_argument('--res',nargs='*',default=['1280x720','1920x1080']);p.add_argument('--count',action='store_true');p.add_argument('--glcount-library',type=Path);a=p.parse_args()
w=Path(__file__).resolve().parent;r=a.runtime.resolve() if a.runtime else w.parents[1]/'playable';a.exe=str(Path(a.exe).resolve()) if a.exe else str(r/('cnc_eyes.exe' if os.name=='nt' else 'cnc_eyes'));brain='TiberianDawn.dll' if os.name=='nt' else 'TiberianDawn.dylib';out=(a.output.resolve() if a.output else w/'benchmarks')/a.label;out.mkdir(parents=True,exist_ok=True)
cases={'forest':('SCG05EA','SCG05EA.pack',[],['tick 60','reveal 1','cam 11 29','dist 3000']),'base':('SCG08EA','SCG08EA.pack',[],['tick 60','reveal 1','cam 47 48','dist 3000']),'blast':('SCB20EA','SCB20EA.pack',['--noshroud'],['tick 30','cam 50 12','dist 3000','hurtobj NUK2','tick 5']),'bodies':('USER93','SCG01EA.pack',['--reskin','--noshroud','--dir','missions/user_maps/'],['cam 31 30','tick 120'])}
if a.count and (not a.glcount_library or os.name=='nt'):p.error('--count needs a Mac interposer supplied with --glcount-library')
records=[]
for rep in range(a.repeat):
 for res in a.res:
  for name in a.cases:
   scen,pack,flags,prep=cases[name];rw,rh=res.split('x');d=out/(name+'-'+res+'-'+str(rep));d.mkdir(parents=True,exist_ok=True);(d/'saves').mkdir(exist_ok=True)
   steps=['gfx show_stats 0',*prep,'framebench 20','echo MEASURE|begin','meshperfdump',*(['tick 1','framebench 1']*a.samples),'meshperfdump','physicsdump','echo MEASURE|end','quit']
   (d/'script.txt').write_text('\n'.join(steps)+'\n')
   cmd=[a.exe,'--scen',scen,'--pack',pack,'--dylib',brain,'--dir','missions/','--content','content/','--cameos','cameos.pack','--dospack','dossidebar.pack','--dosinf','dosinfantry.pack','--nosound','--w',rw,'--h',rh,'--vsync','0','--enhanced','--gfx',str(w/'baseline-fx.cfg'),'--savedir',str(d/'saves'),*flags,'--script',str(d/'script.txt')]
   env={**os.environ,'OPENCNC3D_BACKGROUND':'1','OPENCNC3D_FRAMEPROF':'1','OPENCNC3D_GPUPROF':'1','OPENCNC3D_PERFDETAIL':'1'}
   if a.count:env.update(DYLD_INSERT_LIBRARIES=str(a.glcount_library.resolve()),GLC_WINDOW='20')
   start=time.monotonic()
   with (d/'run.log').open('w') as f:q=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=300)
   text=(d/'run.log').read_text(errors='replace');assert q.returncode==0,(name,q.returncode);assert re.search(r'SCRIPT\|end .*: \d+ lines, 0 failures',text),name
   vals=[float(v) for v in re.findall(r'^FRAMEBENCH\|n=1\|median=([0-9.]+)',text,re.M)];assert len(vals)==a.samples,(name,len(vals))
   record={'case':name,'resolution':res,'repetition':rep,'samples':len(vals),'median_ms':statistics.median(vals),'p95_ms':sorted(vals)[int(.95*len(vals))],'max_ms':max(vals),'first30_median':statistics.median(vals[:30]),'last30_median':statistics.median(vals[-30:]),'elapsed_s':round(time.monotonic()-start,2),'args':cmd,'gpu_profile':re.findall(r'^GPUPROF.*$',text,re.M),'profile_windows':re.findall(r'^FRAMEPROF.*$',text,re.M),'mesh_windows':re.findall(r'^(?:MESHPERF|UNIFORMPERF|FRAGMENTPERF|RANGEMODES).*$',text,re.M),'calls':re.findall(r'^GLC.*$',text,re.M)}
   records.append(record);(out/'metrics.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps({k:record[k] for k in ('case','resolution','repetition','samples','median_ms','p95_ms','max_ms','elapsed_s')}),flush=True)
