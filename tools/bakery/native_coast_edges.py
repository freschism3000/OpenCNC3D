"""Apply versioned native-supported mask edge corrections; never inspect live RGB."""
from pathlib import Path
import hashlib,json,struct
ROOT=Path(__file__).resolve().parents[2]
FOLDER=ROOT/'art/remaster/textures/native_coast_edge_repair_v01'
def digest(data):return hashlib.sha256(data).hexdigest()
def recipe(name):
 return next(p for p in json.loads((FOLDER/'MANIFEST.json').read_text())['packs']if p['path']==name)
def remove_border_fragments(channel,cell,certified_edges):
 """Repair only an unsupported thin rim separated from inward coverage."""
 a=bytearray(channel);scale=cell//24
 def position(side,along,inward):
  if side==0:return along*cell+inward
  if side==1:return along*cell+cell-1-inward
  if side==2:return inward*cell+along
  return (cell-1-inward)*cell+along
 for side in range(4):
  for along in range(cell):
   if not certified_edges[side][along//scale]:continue
   if any(a[position(side,along,j)]for j in range(4,8)):continue
   for j in range(4):a[position(side,along,j)]=0
 return bytes(a)
def correct(data,name):
 from terrain_connectors import normalize,correct as connectors
 normalized=normalize(data,name,strict=False);retained=normalized!=data
 m=recipe(name);data=bytearray(normalized);assert data[:8].hex()==m['magic']
 version,cell,count=struct.unpack_from('<III',data,8);assert version==1 and cell==m['cell']
 stride=cell*cell*m['channels'];start=20+count*24;assert len(data)==start+count*stride
 records={data[20+i*24:44+i*24].hex():i for i in range(count)}
 for r in m['records']:
  i=records[r['key']];offset=start+i*stride+r['channel'];channel=bytes(data[offset:offset+stride:m['channels']]);actual=digest(channel)
  if actual==r['after_sha256']:continue
  if actual!=r['before_sha256']:raise ValueError('Native edge source changed: '+name+'/'+r['name']+'/'+str(r['icon']))
  corrected=remove_border_fragments(channel,cell,r['edge_support'])
  assert digest(corrected)==r['after_sha256'],r['key']
  data[offset:offset+stride:m['channels']]=corrected
 return connectors(bytes(data),name)if retained else bytes(data)
def apply(folder):
 for name in ['coastmask_TEMPERAT.pack','grassmask_TEMPERAT.pack']:
  p=Path(folder)/name
  if not p.is_file():continue
  before=p.read_bytes();after=correct(before,name)
  if after!=before:p.write_bytes(after)
