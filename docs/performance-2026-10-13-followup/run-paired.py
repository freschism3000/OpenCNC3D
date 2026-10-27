import os,sys,json,subprocess,time,argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--name',required=True);p.add_argument('--runtime',type=Path);p.add_argument('--output',type=Path);p.add_argument('--glcount-library',type=Path);p.add_argument('--exe',required=True);p.add_argument('--switch',required=True);p.add_argument('--cases',nargs='+',default=['forest','base','blast','bodies']);p.add_argument('--samples',type=int,default=100);p.add_argument('--count',action='store_true');a=p.parse_args()
w=Path(__file__).resolve().parent;records=[]
for case in a.cases:
 for leg,value in enumerate([0,1,1,0]):
  label=a.name+('-counts' if a.count else '')+'-'+case+'-'+str(leg)+'-'+str(value)
  cmd=[sys.executable,str(w/'benchmark.py'),'--exe',a.exe,'--label',label,'--samples',str(a.samples),'--res','1920x1080','--cases',case]
  if a.runtime:cmd+=['--runtime',str(a.runtime)]
  if a.output:cmd+=['--output',str(a.output)]
  if a.count:cmd+=['--count','--glcount-library',str(a.glcount_library)]
  q=subprocess.run(cmd,env={**os.environ,a.switch:str(value)},check=True)
  rows=json.loads(((a.output or w/'benchmarks')/label/'metrics.json').read_text())
  for r in rows:r.update(prototype=a.name,switch=a.switch,value=value,leg=leg,label=label)
  records+=rows;(w/(a.name+('-counts' if a.count else '')+'-paired.json')).write_text(json.dumps(records,indent=2)+'\n')
