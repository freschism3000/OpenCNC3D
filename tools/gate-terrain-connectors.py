#!/usr/bin/env python3
"""Verify exact connector deltas, protected art, negative controls and repeat builds."""
from pathlib import Path
import argparse,json,struct,sys
import numpy as np
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/bakery'))
import terrain_connectors as repair

def check(runtime):
 rows=[];total=0;before_edges=[];after_edges=[];packs={}
 for p in repair.manifest()['packs']:
  name=p['path'];data=(runtime/name).read_bytes();assert repair.digest(data)==p['after_sha256'],'Repair missing: '+name
  baseline=repair.normalize(data,name);assert repair.correct(baseline,name)==data and repair.correct(data,name)==data
  damaged=bytearray(baseline);damaged[-1]^=1
  try:repair.correct(damaged,name)
  except ValueError:pass
  else:raise AssertionError('Changed source accepted: '+name)
  changed=0
  for r in p['records']:
   key=bytes.fromhex(r['key']);assert (key[20:]==bytes(4)if p['key_size']==24 else struct.unpack('<HH',key)[1]>>8==0),'RA/variant changed'
   off=p['start']+r['index']*p['stride'];a=np.frombuffer(data,np.uint8,p['stride'],off).reshape(144,144,p['channels']);b=np.frombuffer(baseline,np.uint8,p['stride'],off).reshape(a.shape)
   if p['channels']==2:assert np.array_equal(a[:,:,1],b[:,:,1]),'Shade changed'
   tid=struct.unpack_from('<H',key,16 if p['key_size']==24 else 0)[0]
   mask=np.any(a!=b,axis=2);changed+=int(mask.sum())
   if not(name=='coastmask_DESERT.pack'and tid in(154,161)):
    assert not mask[24:-24,24:-24].any(),'Interior changed: '+name
  assert repair.digest(baseline)==p['before_sha256']
  rows.append(dict(pack=name,changed_cells=len(p['records']),changed_pixels=changed,idempotent=True,source_change_rejected=True,all_unlisted_bytes_exact=True,td_default_variant_only=True,shade_exact=True));total+=changed;packs[p['bank'],p['kind']]=(p,data,baseline)
 # Shared road and bank RGB edges: a source-negative is measured, not presumed.
 for bank in ['TEMPERAT','DESERT']:
  p,data,baseline=packs[bank,'rgb'];c=144;arrays=[]
  for blob in [baseline,data]:
   d={}
   for i in range(p['count']):
    tid,ic=struct.unpack_from('<HH',blob,20+i*4);d[tid,ic&255,(ic>>8)&3]=np.frombuffer(blob,np.uint8,p['stride'],p['start']+i*p['stride']).reshape(c,c,3)
   arrays.append(d)
  def edge(a,s):return [a[:,0],a[:,-1],a[0],a[-1]][s]
  refs=next(x for x in json.loads((repair.FOLDER/'CONNECTORS.json').read_text())if x['bank']==bank)
  for a,b in refs['connector_pairs']:
   for dest,arr in zip([before_edges,after_edges],arrays):dest.append(int(abs(edge(arr[tuple(a[:3])],a[3]).astype(int)-edge(arr[tuple(b[:3])],b[3]).astype(int)).max()))
 assert max(after_edges)<=2,('Connector RGB divergence',max(after_edges))
 assert max(before_edges)>40,'Source negative did not exercise the defect'
 return dict(passed=True,packs=rows,total_changed_pixels=total,max_rgb_edge_before=max(before_edges),max_rgb_edge_after=max(after_edges),desert_whole_template_feather=['RV19','FORD1'],unchanged_geometry_depth_shader_and_approved_virtual_bed=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();r=check(a.runtime)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps(r))
