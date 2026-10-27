"""Compile the versioned TD connector repair without changing unrelated pack bytes.

Deltas carry both source and result bytes. Complete normalized pack hashes reject
stale artwork; record hashes allow existing repaired records to survive a partial
rebake. No live RGB classification, template-ID guessing or runtime cost.
"""
from pathlib import Path
from functools import lru_cache
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[2]
FOLDER=ROOT/'art/remaster/textures/terrain_connector_repair_v01'
def digest(data):return hashlib.sha256(data).hexdigest()
@lru_cache(None)
def manifest():return json.loads((FOLDER/'MANIFEST.json').read_text())
@lru_cache(None)
def recipe(name):return next((p for p in manifest()['packs']if p['path']==name),None)
@lru_cache(None)
def runs(name):
 p=recipe(name)
 if p is None:return {}
 payload=(FOLDER/p['delta']).read_bytes()
 if digest(payload)!=p['delta_sha256']:raise ValueError('Connector delta changed: '+name)
 raw=zlib.decompress(payload);result={};end=0
 for r in p['records']:
  pos=r['run_offset'];assert pos==end
  rows=[];last=0
  for _ in range(r['run_count']):
   offset,size=struct.unpack_from('<II',raw,pos);pos+=8
   if size<1 or offset<last or offset+size>p['stride'] or pos+size*2>len(raw):raise ValueError('Invalid connector delta: '+name)
   rows.append((offset,raw[pos:pos+size],raw[pos+size:pos+size*2]));pos+=size*2;last=offset+size
  result[r['index']]=rows;end=pos
 if end!=len(raw):raise ValueError('Extra connector delta bytes: '+name)
 return result
def layout(data,p):
 version,cell,count=struct.unpack_from('<III',data,8)
 if version!=1 or cell!=p['cell'] or len(data)!=20+count*p['key_size']+count*p['stride']:raise ValueError('Connector pack layout changed: '+p['path'])
 start=20+count*p['key_size'];indices={bytes(data[20+i*p['key_size']:20+(i+1)*p['key_size']]).hex():i for i in range(count)}
 if len(indices)!=count:raise ValueError('Duplicate connector keys: '+p['path'])
 return start,indices

def normalize(data,name,strict=True):
 """Undo known connector result records; leave producer inputs intact in lax mode."""
 p=recipe(name)
 if p is None:return bytes(data)
 start,indices=layout(data,p);out=bytearray(data)
 for r in p['records']:
  if r['key']not in indices:raise ValueError('Connector key missing: '+name)
  i=indices[r['key']];off=start+i*p['stride'];h=digest(out[off:off+p['stride']])
  if h==r['after_sha256']:
   for rel,before,after in runs(name)[r['index']]:out[off+rel:off+rel+len(before)]=before
  elif strict and h!=r['before_sha256']:raise ValueError('Connector source changed: '+name+'/'+r['key'])
 if strict and digest(out)not in [p['before_sha256']]+[x['before_sha256']for x in p.get('producer_layouts',[])]:raise ValueError('Unrelated connector pack bytes changed: '+name)
 return bytes(out)
def correct(data,name):
 p=recipe(name)
 if p is None:return bytes(data)
 if digest(data)in [p['after_sha256']]+[x['after_sha256']for x in p.get('producer_layouts',[])]:return bytes(data)
 out=bytearray(normalize(data,name));start,indices=layout(out,p)
 for r in p['records']:
  off=start+indices[r['key']]*p['stride']
  for rel,before,after in runs(name)[r['index']]:out[off+rel:off+rel+len(after)]=after
 if digest(out)not in [p['after_sha256']]+[x['after_sha256']for x in p.get('producer_layouts',[])]:raise ValueError('Connector output differs: '+name)
 return bytes(out)
def apply_file(path):
 path=Path(path);before=path.read_bytes();after=correct(before,path.name)
 if after!=before:path.write_bytes(after)
def apply(folder):
 for p in manifest()['packs']:
  path=Path(folder)/p['path']
  if path.is_file():apply_file(path)
if __name__=='__main__':
 import sys
 apply(Path(sys.argv[1])if len(sys.argv)>1 else ROOT/'game')
