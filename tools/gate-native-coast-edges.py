#!/usr/bin/env python3
"""Verify native evidence, exact mask edits, and untouched alpha/RA/interiors."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,struct
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('edges',ROOT/'tools/bakery/native_coast_edges.py');edges=importlib.util.module_from_spec(spec);spec.loader.exec_module(edges)
def check(runtime):
 manifest=json.loads((edges.FOLDER/'MANIFEST.json').read_text());rows=[]
 for p in manifest['packs']:
  from sys import path as search_path
  search_path.insert(0,str(ROOT/'tools/bakery'))
  from terrain_connectors import normalize
  data=normalize((Path(runtime)/p['path']).read_bytes(),p['path']);assert edges.correct(data,p['path'])==data,'Uncorrected border fragments: '+p['path']
  v,c,n=struct.unpack_from('<III',data,8);channels=p['channels'];stride=c*c*channels;start=20+n*24;before=bytearray(data)
  records={data[20+i*24:44+i*24].hex():i for i in range(n)}
  for r in p['records']:
   ref=Image.open(edges.FOLDER/r['reference_file']).convert('RGBA');assert ref.size==(24,24) and edges.digest(ref.tobytes())==r['original_native_rgba_sha256']
   raw=ref.tobytes();native=[raw[i:i+4]for i in range(0,len(raw),4)];cert=[int(q[3]==0 if channels==1 else True)for q in native]
   actual=[[cert[y*24]for y in range(24)],[cert[y*24+23]for y in range(24)],cert[:24],cert[-24:]];assert actual==r['edge_support']
   i=records[r['key']];key=data[20+i*24:44+i*24];assert key[21]==0 and key[22:24]==bytes(2)
   offset=start+i*stride+r['channel'];channel=bytearray(data[offset:offset+stride:channels]);assert edges.digest(channel)==r['after_sha256']
   for pos,value in r['edits']:
    x,y=pos%c,pos//c;assert min(x,y,c-1-x,c-1-y)<4;assert channel[pos]==0 and value>0;channel[pos]=value
   assert edges.digest(channel)==r['before_sha256'];assert edges.remove_border_fragments(channel,c,actual)==data[offset:offset+stride:channels]
   before[offset:offset+stride:channels]=channel
  assert edges.digest(data)==p['after_sha256'];assert edges.digest(before)==p['before_sha256'],'Unexpected changes outside the explicit mask repair'
  rows.append(dict(pack=p['path'],changed_cells=len(p['records']),changed_channel_pixels=p['changed_pixels'],all_other_bytes_preserved=True,red_alert_records_preserved=True))
 return dict(passed=True,rows=rows,native_reference_pack_sha256=manifest['source_pack_sha256'])
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();r=check(a.runtime)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps(r))
