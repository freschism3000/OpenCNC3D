from pathlib import Path
import re,json,statistics
import argparse
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();R=a.output.resolve();root=R/'runs';trials=[]
for i,arm in enumerate(['off','on','on','off'],1):
 p=root/arm/('activebench-'+str(i));lines=(p/'run.log').read_text().splitlines();rows=[];idx=-1
 for l in lines:
  if l.startswith('ECHO|ACTIVEFRAME|'):idx=int(l.rsplit('=',1)[1])
  if l.startswith('FRAMEPROF|') and idx>=0:
   d={k:float(v) for k,v in re.findall(r'\|([^|=]+)=([-+\d.]+)',l)};d['index']=idx;rows.append(d)
  if l.startswith(('FRAGMENTPREP|','MESHPERF|')) and rows:
   prefix='prep:' if l.startswith('FRAGMENTPREP|') else 'mesh:'
   rows[-1].update({prefix+k:int(v) for k,v in re.findall(r'\|([^|=]+)=(\d+)',l)})
  if l.startswith('FRAMEBENCH|') and rows:rows[-1]['allframe']=float(re.search(r'median=([\d.]+)',l)[1])
 for j,r in enumerate(rows):
  r['draw_stages']=sum(r.get(k,0) for k in ['sun shadow map','objects (opaque)','cutout + translucent','post: ground normals'])
  r['submit_all']=sum(v for k,v in r.items() if k in ['sun shadow map','seabed','water','terrain','shore','mottle','tiberium','tiberium ground','tread marks','smudges','tiberium crystals','objects (opaque)','trees + foliage','cutout + translucent','infantry','effects','rain + shroud draw','markers + bars','post: ground normals','post: light','post: atmosphere','post chain','hud + present','panels'])
  if j:
   for k in ['prep:upload_bytes','prep:full_bytes','prep:draws_saved','mesh:draws','mesh:uploads','prep:hits','prep:vertices','prep:builds']:
    r['delta:'+k]=r.get(k,0)-rows[j-1].get(k,0)
 trials.append({'arm':arm,'order':i,'path':str(p),'rows':rows})
fields=['allframe','draw_stages','submit_all','sun shadow map','objects (opaque)','cutout + translucent','post: ground normals','gpu finish','delta:prep:upload_bytes','delta:prep:full_bytes','delta:prep:draws_saved','delta:mesh:draws','delta:mesh:uploads','delta:prep:vertices','delta:prep:builds']
ranges={'moving':(5,30),'settling':(31,50),'sinking':(53,74),'fading':(78,110),'all_warm':(5,130)}
def stats(rs):return {'n':len(rs),'median':{k:statistics.median(r.get(k,0) for r in rs) for k in fields},'p95':{k:sorted(r.get(k,0) for r in rs)[min(len(rs)-1,int(len(rs)*.95))] for k in fields}}
summary={'method':'Serialized fcntl LOCK_EX whole OFF/ON/ON/OFF sequence; every framebench1 preceded by tick1 and changed tickAlpha. Independent process per arm; original321-body installed NUKE. glFinish total includes GPU completion; FRAMEPROF stage CPU includes possible driver stalls. First samples reported separately.','trials':[dict(arm=t['arm'],order=t['order'],path=t['path'],first=t['rows'][0],phases={tag:stats([r for r in t['rows'] if lo<=r['index']<=hi]) for tag,(lo,hi) in ranges.items()}) for t in trials],'pooled':{arm:{tag:stats([r for t in trials if t['arm']==arm for r in t['rows'] if lo<=r['index']<=hi]) for tag,(lo,hi) in ranges.items()} for arm in ['off','on']}}
(R/'activebench-abba.json').write_text(json.dumps(summary,indent=2)+'\n');(R/'activebench-abba-rows.json').write_text(json.dumps(trials,indent=2)+'\n')
for tag in ranges:
 print(tag)
 for arm in ['off','on']:
  s=summary['pooled'][arm][tag];print(arm,s['n'],{k:s['median'][k] for k in ['allframe','draw_stages','submit_all','delta:prep:upload_bytes','delta:mesh:draws','delta:prep:vertices']},'p95all',s['p95']['allframe'])
print('First:',[(t['arm'],t['order'],t['rows'][0]['allframe'],t['rows'][0]['draw_stages']) for t in trials])
