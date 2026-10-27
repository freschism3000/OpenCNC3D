#!/usr/bin/env python3
"""Verify approved grass-only shoreline coverage in TD and native RA packs."""
from pathlib import Path
import argparse,hashlib,json,struct,sys
import numpy as np
from PIL import Image

def check(source,runtime):
 sys.path.insert(0,str(source/'tools/bakery'))
 import native_coast_edges as edges
 repairs={r['key']:r for r in edges.recipe('grassmask_TEMPERAT.pack')['records']}
 d=source/'art/remaster/textures/unified_daylight_v01';refs=json.loads((d/'shore_grass_v01/MANIFEST.json').read_text())['records'];rows=json.loads((d/'MANIFEST.json').read_text())['assets']
 from terrain_connectors import normalize
 b=normalize((runtime/'grassmask_TEMPERAT.pack').read_bytes(),'grassmask_TEMPERAT.pack');assert b[:8]==b'C3DGRAS\0';_,c,n=struct.unpack_from('<III',b,8);assert c==144;start=20+n*24;stride=c*c*2
 data={struct.unpack_from('<HHBBH',b,20+i*24+16):np.frombuffer(b,'u1',stride,start+i*stride).reshape(c,c,2)[...,0]for i in range(n)}
 count=templates=0
 for r in rows:
  if r['theater']not in ['TEMPERAT','RATEMP'] or not r['name'].startswith('SH'):continue
  name=r['name']+(('_v'+str(r['variant']))if r['variant']else'');ref=refs[r['theater']+'/'+name];w,h=r['cells']
  if 'file'in ref:
   p=source/ref['file'];assert hashlib.sha256(p.read_bytes()).hexdigest()==ref['sha256'];a=np.array(Image.open(p).convert('L'))
  else:a=np.zeros((h*c,w*c),np.uint8)
  for icon in r['have']:
   y,x=divmod(icon,w);key=(r['tid'],icon,r['variant'],int(r['theater']=='RATEMP'),0)
   expected=a[y*c:(y+1)*c,x*c:(x+1)*c].copy()
   packed_key=r['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',*key)
   if packed_key.hex() in repairs:
    repair=repairs[packed_key.hex()]
    expected=np.frombuffer(edges.remove_border_fragments(expected.tobytes(),c,repair['edge_support']),np.uint8).reshape(c,c)
   assert np.array_equal(data[key],expected),(name,icon)
   count+=1
  templates+=1
 return dict(passed=True,shore_templates=templates,native_cells=count,approved_grass_only_masks=sum('file'in r for r in refs.values()),original_painting_fallbacks=sum('file'not in r for r in refs.values()),beach_and_water_not_generic_clear_grass=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--playable',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();r=check(a.source,a.playable)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps(r))
