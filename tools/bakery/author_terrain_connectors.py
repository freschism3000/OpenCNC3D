from pathlib import Path
from PIL import Image
import json,struct,runpy,hashlib,collections
import numpy as np
from scipy import ndimage as nd
import argparse,base64,zlib
cli=argparse.ArgumentParser();cli.add_argument('--base-runtime',type=Path,required=True);cli.add_argument('--output',type=Path,required=True);args=cli.parse_args()
ROOT=Path(__file__).resolve().parents[2];F=ROOT/'art/remaster/textures/terrain_connector_repair_v01';P=args.output;P.mkdir(parents=True,exist_ok=True);R=args.base_runtime;C=144
native_source=json.loads(zlib.decompress((F/'native-alpha.json.z').read_bytes()))

sha=lambda b:hashlib.sha256(b).hexdigest()
class UF:
 def __init__(self):self.p={}
 def get(self,a):
  if a not in self.p:self.p[a]=a
  if self.p[a]!=a:self.p[a]=self.get(self.p[a])
  return self.p[a]
 def join(self,a,b):self.p[self.get(a)]=self.get(b)
 def groups(self):
  g=collections.defaultdict(list)
  for a in list(self.p):g[self.get(a)].append(a)
  return list(g.values())
def readpack(path,kind):
 raw=path.read_bytes();_,cell,n=struct.unpack_from('<3I',raw,8);assert cell==C
 ks=4 if kind=='rgb'else 24;ch={'rgb':3,'ground':2,'coast':1}[kind];start=20+n*ks;keys={};arrays={};labels={}
 for i in range(n):
  if kind=='rgb':
   tid,field=struct.unpack_from('<HH',raw,20+i*4);key=(tid,field&255,(field>>8)&3);valid=True;label=''
  else:
   k=raw[20+i*24:44+i*24];tid,ic,v,game,alt=struct.unpack_from('<HHBBH',k,16);key=(tid,ic,v);valid=not game and not alt;label=k[:16].rstrip(b'\0').decode()
  if valid:keys[key]=i;labels[key]=label;arrays[key]=np.frombuffer(raw,np.uint8,cell*cell*ch,start+i*cell*cell*ch).reshape(cell,cell,ch).copy()
 return dict(raw=raw,keys=keys,arrays=arrays,labels=labels,ch=ch,start=start,path=path)
def edge(a,s):return[a,a[:,::-1],a.transpose(1,0,2),a.transpose(1,0,2)[:,::-1]][s]
def field_distance(a):
 inside=a>=128
 if inside.all():return np.full(C,200.)
 if not inside.any():return np.full(C,-200.)
 return nd.distance_transform_edt(inside)-nd.distance_transform_edt(~inside)
def native_tiles(scene):
 bank='TEMPERAT'if scene=='SCB61EA'else'DESERT'
 return{tuple(map(int,k.split(','))):np.frombuffer(base64.b64decode(v),np.uint8).reshape(24,24)for k,v in native_source[bank].items()}
summary=[];recipe=[]
for bank in ['TEMPERAT','DESERT']:
 native=native_tiles('SCB61EA'if bank=='TEMPERAT'else'SCB13EC')
 templates=json.loads((ROOT/'art/remaster/textures'/bank/'set_144.json').read_text())['templates'];sizes={r['tid']:r['cells']for r in templates.values()}
 def outer(key,side):
  w,h=sizes[key[0]];x,y=key[1]%w,key[1]//w;return [x==0,x==w-1,y==0,y==h-1][side]
 packs={'rgb':readpack(R/f'terrainenh_{bank}.pack','rgb'),'ground':readpack(R/('grassmask_TEMPERAT.pack'if bank=='TEMPERAT'else'groundmask_DESERT.pack'),'ground'),'coast':readpack(R/f'coastmask_{bank}.pack','coast')}
 kinds={k:('road'if 93<=k[0]<=135 else'coast')for k,label in packs['ground']['labels'].items()if 93<=k[0]<=135 or label.startswith(('SH','RV','FORD','FALLS','BRIDGE'))}
 uf=UF();corners=UF();pairs=set();maps=[];seeds=set();marked={"SCG15EC":(31,20),"SCB61EA":(25,4),"SCG14EA":(35,8),"SCB13EC":(39,46)}
 def profile(k,s,kind):
  a=packs[kind]['arrays'].get(k)
  if a is None:return None
  q=edge(a,s);return q[:,8:14,0].mean(1)
 for m in json.loads((F/'MAPS.json').read_text()):
  if m['game']!='TD'or m['theater']!=bank:continue
  scene=m['scenario'];mp=R/'missions'/(scene+'.BIN');rawmap=mp.read_bytes();assert sha(rawmap)==m['sha256'],'Mission source changed: '+scene;arr=np.frombuffer(rawmap,np.uint8).reshape(64,64,2);t,ic=arr[:,:,0],arr[:,:,1];maps.append(dict(scenario=scene,source='missions/'+scene+'.BIN',sha256=sha(rawmap)))
  x0,y0,w,h=m['rect']
  for y in range(y0,y0+h):
   for x in range(x0,x0+w):
    a=(int(t[y,x]),int(ic[y,x]),0)
    if a not in kinds or a not in packs['rgb']['arrays']:continue
    for dx,dy,side in [(1,0,1),(0,1,3)]:
     xx,yy=x+dx,y+dy
     if xx>=min(t.shape[1],x0+w)or yy>=min(t.shape[0],y0+h):continue
     b=(int(t[yy,xx]),int(ic[yy,xx]),0)
     if b not in kinds or kinds[a]!=kinds[b]or b not in packs['rgb']['arrays']:continue
     kind='ground'if kinds[a]=='road'else'coast';pa,pb=profile(a,side,kind),profile(b,side^1,kind)
     if pa is None or pb is None:continue
     if kinds[a]=='road':
      if ((pa<128)&(pb<128)).sum()<18:continue
     else:
      # Whole paintings already meet across their internal cuts. Only stitch their outside faces.
      if not outer(a,side)or not outer(b,side^1):continue
      # Original native edge shapes distinguish bank connectors from different faces.
      if a not in native or b not in native:continue
      aa=edge(native[a][:,:,None],side)[:,0,0]>=128;bb=edge(native[b][:,:,None],side^1)[:,0,0]>=128
      if aa[0]!=bb[0]or aa[-1]!=bb[-1]or(aa!=bb).sum()>3 or min(aa.sum(),bb.sum())<1 or np.count_nonzero(aa[1:]!=aa[:-1])!=np.count_nonzero(bb[1:]!=bb[:-1]):continue
     na=(*a,side);nb=(*b,side^1);uf.join(na,nb);pairs.add((na,nb))
     if scene in marked and abs(x-marked[scene][0])<=5 and abs(y-marked[scene][1])<=3:seeds.update([na,nb])
     cs={0:(0,3),1:(1,2),2:(0,1),3:(3,2)}
     for ca,cb in zip(cs[side],cs[side^1]):corners.join((*a,ca),(*b,cb))
 groups=[g for g in uf.groups()if any(n in seeds for n in g)]
 originals={kind:{k:a.copy()for k,a in pack['arrays'].items()}for kind,pack in packs.items()}
 # Move each coast contour and its colour/matte together, rather than exposing
 # old water pixels by editing alpha alone.
 warps={};stable=[]
 def crossings(profile):
  v=nd.gaussian_filter1d(profile.astype(float),2)>=128
  points=np.flatnonzero(v[1:]!=v[:-1])+.5
  return v[0],v[-1],points
 for g in groups:
  if kinds[g[0][:3]]!='coast':stable.append(g);continue
  rows=[];native_rows=[]
  for node in g:
   k,side=node[:3],node[3];a=packs['coast']['arrays'].get(k)
   if a is None:break
   nr=crossings(np.repeat(edge(native[k][:,:,None],side)[:,0,0],6));native_rows.append(nr)
   row=crossings(edge(a,side)[:,2,0])
   if len(nr[2]) and len(row[2])!=len(nr[2]):
    # Tiny painted alpha speckles are not additional river branches.
    # Retain the native connector's topology and the nearest painted crossing.
    chosen=[min(row[2],key=lambda x:abs(x-p)) if len(row[2]) else p for p in nr[2]]
    if len(set(chosen))!=len(chosen):chosen=nr[2]
    row=(nr[0],nr[1],np.array(chosen))
   rows.append(row)
  if len(rows)!=len(g):continue
  signature={(bool(a),bool(b),len(p))for a,b,p in native_rows}
  if len(signature)!=1:continue
  if len(native_rows[0][2])>3:continue
  if any(len(row[2])!=len(native_rows[0][2])for row in rows):continue
  stable.append(g)
  if not len(rows[0][2]):continue
  target=np.mean([r[2]for r in rows],axis=0)
  if len(target)>1 and np.min(np.diff(target))<8:continue
  for node,(_,_,points) in zip(g,rows):
   knots=np.r_[0,target,C-1];values=np.r_[0,points,C-1]
   warps[node]=np.interp(np.arange(C),knots,values)-np.arange(C)
 groups=stable;new={};patches={}
 nodes_by_key=collections.defaultdict(list)
 for g in groups:
  for node in g:nodes_by_key[node[:3]].append(node)
 yy,xx=np.mgrid[:C,:C];dist=[xx,C-1-xx,yy,C-1-yy]
 for k,nodes in nodes_by_key.items():
  shiftx=np.zeros((C,C));shifty=np.zeros((C,C))
  for node in nodes:
   if node not in warps:continue
   side=node[3];t=np.clip(dist[side]/24,0,1);f=1-t*t*(3-2*t)
   if side<2:shifty+=warps[node][:,None]*f
   else:shiftx+=warps[node][None,:]*f
  for kind,pack in packs.items():
   if k not in pack['arrays']:continue
   a=pack['arrays'][k];out=a.copy()
   for ch in range(a.shape[2]if kind=='rgb'else 1):out[:,:,ch]=np.rint(nd.map_coordinates(a[:,:,ch].astype(float),[yy+shifty,xx+shiftx],order=1,mode='nearest')).clip(0,255).astype(np.uint8)
   pack['arrays'][k]=out
 # Trim the two guide pixels only on the connected faces. Preserve all grain
 # beyond that rim through an additive, smoothly decaying boundary correction.
 for k,nodes in nodes_by_key.items():
  for node in nodes:
   side=node[3]
   for kind in ['rgb','ground']:
    q=edge(packs[kind]['arrays'][k],side)
    if kind=='ground':
     gap=q[:,8:14,0].max(1)<32;q[gap,:8,0]=0
    q[:,:2,:]=q[:,2:3,:]
 for g in groups:
  common={}
  for kind,pack in packs.items():
   if any(n[:3]not in pack['arrays']for n in g):continue
   ch=3 if kind=='rgb'else 1
   common[kind]=np.mean([edge(pack['arrays'][n[:3]],n[3])[:,0,:ch]for n in g],axis=0)
  for node in g:new[node]={kind:v.copy()for kind,v in common.items()}
  if kinds[g[0][:3]]=='coast':
   patch=np.mean([edge(packs['rgb']['arrays'][n[:3]],n[3])[:,:24,:]for n in g],axis=0)
   for node in g:patches[node]=patch.copy()
 endcorner={0:(0,3),1:(1,2),2:(0,1),3:(3,2)}
 for cg in corners.groups():
  endpoints=[]
  for corner in cg:
   k,c=corner[:3],corner[3]
   for side,ends in endcorner.items():
    if c in ends and (*k,side)in new:endpoints.append(((*k,side),0 if ends[0]==c else C-1))
  for kind in ['rgb','ground','coast']:
   valid=[(n,p)for n,p in endpoints if kind in new[n]]
   if not valid:continue
   mean=np.mean([new[n][kind][p]for n,p in valid],axis=0)
   for n,p in valid:new[n][kind][p]=mean
 changes=collections.Counter()
 for k,nodes in nodes_by_key.items():
  for kind,pack in packs.items():
   if k not in pack['arrays']:continue
   src=pack['arrays'][k];ch=3 if kind=='rgb'else 1;total=np.zeros((C,C));delta=np.zeros((C,C,ch));amount=np.zeros((C,C))
   for node in nodes:
    if kind not in new[node]:continue
    side=node[3];d=dist[side].astype(float);band=4 if kind=='coast'else 24 if kind=='rgb'and node in patches else 10
    t=np.clip(d/band,0,1);fade=1-t*t*(3-2*t);weight=fade/(d+.0001)**2
    old=edge(src,side)[:,0,:ch].astype(float);value=new[node][kind]-old
    field=np.broadcast_to(value[:,None,:],(C,C,ch))if side<2 else np.broadcast_to(value[None,:,:],(C,C,ch))
    if kind=='rgb'and node in patches:
     patch=patches[node];patch[:,0,:]=new[node]['rgb'];field=patch[yy if side<2 else xx,np.minimum(dist[side],23)]-src[:,:,:ch]
    delta+=field*weight[:,:,None];total+=weight;amount=np.maximum(amount,fade)
   src[:,:,:ch]=np.rint(src[:,:,:ch]+delta/np.maximum(total[:,:,None],1e-20)*amount[:,:,None]).clip(0,255).astype(np.uint8)
   changes[kind]+=int(np.any(src!=originals[kind][k],axis=2).sum())
 # Boundary equations should survive packing exactly (including corners).
 if bank=='TEMPERAT':
  # SH14's guide line continues into its dry end cap, which meets clear ground.
  a=packs['rgb']['arrays'][(11,6,0)];a[4:-4,:2,:]=a[4:-4,2:3,:]
 max_error={kind:0 for kind in packs}
 for a,b in pairs:
  if a not in new or b not in new:continue
  for kind,pack in packs.items():
   if a[:3]not in pack['arrays']or b[:3]not in pack['arrays']:continue
   ch=3 if kind=='rgb'else 1;aa=edge(pack['arrays'][a[:3]],a[3])[:,0,:ch].astype(int);bb=edge(pack['arrays'][b[:3]],b[3])[:,0,:ch].astype(int);max_error[kind]=max(max_error[kind],int(abs(aa-bb).max()))
 out=P/'candidate-packs';out.mkdir(exist_ok=True)
 for kind,pack in packs.items():
  data=bytearray(pack['raw'])
  for k,a in pack['arrays'].items():
   idx=pack['keys'][k];off=pack['start']+idx*C*C*pack['ch'];data[off:off+C*C*pack['ch']]=a.tobytes()
  (out/pack['path'].name).write_bytes(data);recipe.append(dict(bank=bank,kind=kind,name=pack['path'].name,before_sha256=sha(pack['raw']),after_sha256=sha(data)))
 summary.append(dict(bank=bank,maps=maps,connector_groups=groups,connector_pairs=[(a,b)for a,b in sorted(pairs)if a in new and b in new],groups=len(groups),pairs=len(pairs),cells=len(nodes_by_key),changed_pixels=dict(changes),max_boundary_error=max_error));print(bank,{k:v for k,v in summary[-1].items()if k not in ('maps','connector_groups','connector_pairs')}|{'maps':len(maps)},flush=True)
(P/'connector-authoring.json').write_text(json.dumps(dict(summary=summary,packs=recipe),indent=2))

# The two whole-template river/ford masks use one signed-distance feather.
p=P/'candidate-packs/coastmask_DESERT.pack';b=bytearray(p.read_bytes());_,c,n=struct.unpack_from('<3I',b,8);st=20+n*24
for tid,w,h in [(154,4,4),(161,3,3)]:
 a=np.full((h*c,w*c),255,np.uint8);idx={}
 for i in range(n):
  t,ic,v,g,alt=struct.unpack_from('<HHBBH',b,20+i*24+16)
  if t!=tid or v or g or alt:continue
  idx[ic]=i;a[ic//w*c:(ic//w+1)*c,ic%w*c:(ic%w+1)*c]=np.frombuffer(b,np.uint8,c*c,st+i*c*c).reshape(c,c)
 solid=np.pad(a>=128,24,mode='edge');sd=nd.distance_transform_edt(solid)-nd.distance_transform_edt(~solid);t=np.clip((sd[24:-24,24:-24]+15)/30,0,1);alpha=np.rint(t*t*(3-2*t)*255).astype(np.uint8)
 for ic,i in idx.items():b[st+i*c*c:st+(i+1)*c*c]=alpha[ic//w*c:(ic//w+1)*c,ic%w*c:(ic%w+1)*c].tobytes()
 Image.fromarray(alpha).save(P/('normalized-alpha-'+str(tid)+'.png'))
p.write_bytes(b)

