# Portable replay derivative; measured local-driver hashes are in driver-provenance.json.
import os,sys,json,re,subprocess,time,argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe');p.add_argument('--runtime',type=Path);p.add_argument('--output',type=Path);p.add_argument('--label',required=True);p.add_argument('--switch');p.add_argument('--cases',nargs='+',default=['forest','base','blast','bodies']);p.add_argument('--seconds',type=int,default=10);p.add_argument('--vsync',default='0');a=p.parse_args()
w=Path(__file__).resolve().parent;r=a.runtime.resolve() if a.runtime else w.parents[1]/'playable';a.exe=str(Path(a.exe).resolve()) if a.exe else str(r/('cnc_eyes.exe' if os.name=='nt' else 'cnc_eyes'));brain='TiberianDawn.dll' if os.name=='nt' else 'TiberianDawn.dylib';records=[]
cases={'forest':('SCG05EA','SCG05EA.pack',[],['tick 60','reveal 1','cam 11 29','dist 3000']),'base':('SCG08EA','SCG08EA.pack',[],['tick 60','reveal 1','cam 47 48','dist 3000']),'blast':('SCB20EA','SCB20EA.pack',['--noshroud'],['tick 30','cam 50 12','dist 3000','hurtobj NUK2','tick 5']),'bodies':('USER93','SCG01EA.pack',['--reskin','--noshroud','--dir','missions/user_maps/'],['cam 31 30','tick 120'])}
for case in a.cases:
 for leg,value in enumerate([0,1,1,0] if a.switch else [0,0]):
  d=(a.output.resolve() if a.output else w/'livebench')/a.label/(case+'-'+str(leg)+'-'+str(value));d.mkdir(parents=True,exist_ok=True);(d/'saves').mkdir(exist_ok=True)
  scen,pack,flags,prep=cases[case];(d/'script.txt').write_text('\n'.join(['gfx show_stats 0',*prep,'framebench 20','meshperfdump',f'livebench {a.seconds}','meshperfdump','physicsdump','quit'])+'\n')
  cmd=[a.exe,'--scen',scen,'--pack',pack,'--dylib',brain,'--dir','missions/','--content','content/','--cameos','cameos.pack','--dospack','dossidebar.pack','--dosinf','dosinfantry.pack','--nosound','--w','1920','--h','1080','--vsync',a.vsync,'--enhanced','--gfx',str(w/'baseline-fx.cfg'),'--savedir',str(d/'saves'),*flags,'--script',str(d/'script.txt')]
  env={**os.environ,'OPENCNC3D_BACKGROUND':'1','OPENCNC3D_FRAMEPROF':'1','OPENCNC3D_GPUPROF':'1','OPENCNC3D_PERFDETAIL':'1'}
  if a.switch:env[a.switch]=str(value)
  with(d/'run.log').open('w')as f:q=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
  t=(d/'run.log').read_text(errors='replace');assert q.returncode==0;assert re.search(r'SCRIPT\|end .*: \d+ lines, 0 failures',t)
  m=re.findall(r'ran ([\d.]+)s: (\d+) engine ticks \(([\d.]+) Hz\), (\d+) frames \(([\d.]+) fps\)',t);assert len(m)==1,(case,m)
  elapsed,ticks,hz,frames,fps=m[0];row={'case':case,'leg':leg,'value':value,'elapsed_s':float(elapsed),'ticks':int(ticks),'hz':float(hz),'frames':int(frames),'fps':float(fps),'mean_ms':1000*float(elapsed)/int(frames),'vsync':a.vsync,'switch':a.switch,'mesh_windows':re.findall(r'^(?:MESHPERF|UNIFORMPERF|FRAGMENTPERF|RANGEMODES).*$',t,re.M),'frame_windows':re.findall(r'^FRAMEPROF.*$',t,re.M),'args':cmd}
  records.append(row);(w/(a.label+'-live.json')).write_text(json.dumps(records,indent=2)+'\n');print(json.dumps({k:row[k] for k in ['case','leg','value','fps','mean_ms','frames','ticks']}),flush=True)
