#!/usr/bin/env python3
"""Continue each Snow road's own grain through its native exits, version2.

Version1 compared an exit with a full interior row; on curves both rows could
contain mostly snow and a real truncated exit escaped the audit. Select actual
interior road grain before testing the edge. Originals and v1 remain immutable.
"""
from pathlib import Path
import hashlib,importlib.util,json
import numpy as np
from PIL import Image
from scipy import ndimage as nd
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def smooth(t):t=np.clip(t,0,1);return t*t*t*(t*(t*6-15)+10)
def build(bank="RASNOW"):
 source=ROOT/'art/remaster/textures/ra_road_continuity_v01';base=ROOT/'art/remaster/textures/ra_road_continuity_v02';out=base if bank=='RASNOW'else base/bank;out.mkdir(parents=True,exist_ok=True)
 p=ROOT/'tools/ra3d/bake_ra_terrain3d.py';spec=importlib.util.spec_from_file_location('ra',p);ra=importlib.util.module_from_spec(spec);spec.loader.exec_module(ra);palette=ra.palette('SNOW.PAL');slots=ra.bank_slots('RASNOW');native_meta=json.loads((ROOT/'art/remaster/textures/RASNOW/set_144.json').read_text())['templates']
 m=json.loads((source/'MANIFEST.json').read_text());
 if bank!='RASNOW':
  original=ROOT/'art/remaster/textures'/bank;meta=json.loads((original/'set_144.json').read_text())['templates'];m={'assets':[dict(name=n,tid=r['tid'],cells=r['cells'],have=r['have'])for n,r in meta.items()if n.startswith('D')and n[1:].isdigit()]}
 logical=json.loads((source/'native_exits_v02.json').read_text());native={r['name']:r for r in logical['templates']};rows=[];repaired=0;audited=0
 for row in m['assets']:
  name=row['name'];w,h=row['cells'];have=set(row['have']);src=source/name/'albedo.png'if bank=='RASNOW'else ROOT/'art/remaster/textures'/bank/name/'final_144.png';assert row['cells']==native[name]['cells']and row['have']==native[name]['have'];before=np.array(Image.open(src).convert('RGB'));after=before.copy();support=np.zeros(before.shape[:2],float);road=np.zeros(before.shape[:2],float);exits=[]
  # Saved source topology, not the live diffuse image, owns each exit's span.
  edges=native[name]['edge_masks']
  for icon in have:
   cy,cx=divmod(icon,w)
   for side,dx,dy,e in [('L',-1,0,0),('R',1,0,1),('T',0,-1,2),('B',0,1,3)]:
    nx,ny=cx+dx,cy+dy
    if 0<=nx<w and 0<=ny<h and ny*w+nx in have:continue
    bits=edges[icon][e];edge=np.array([(bits>>i)&1 for i in range(24)],np.uint8)
    labels,count=nd.label(edge);sizes=np.bincount(labels);sizes[0]=0
    if not count or sizes.max()<3:continue
    selected=labels==sizes.argmax();coverage=np.asarray(Image.fromarray(np.uint8(selected[None,:])*255).resize((144,1),Image.Resampling.BILINEAR),float)[0]/255;core=coverage>.7;indices=np.flatnonzero(core);audited+=1
    vertical=side in 'TB';x0=cx*144;y0=cy*144
    def line(depth):
     return before[y0+depth if side=='T'else y0+143-depth,x0:x0+144]if vertical else before[y0:y0+144,x0+depth if side=='L'else x0+143-depth]
    donors=[]
    for depth in range(20,81):
     original=line(depth);good=original.mean(1)<185
     # The dominant contiguous grain run excludes snowy shoulders and isolated marks.
     labs,n=nd.label(good);ns=np.bincount(labs);ns[0]=0
     if not n or ns.max()<18:continue
     run=np.flatnonzero(labs==ns.argmax());donors.append((depth,int(run[0]),int(run[-1])))
    if not donors:continue
    cap=line(0).mean(1)[core];donor_values=np.concatenate([line(d)[lo:hi+1].mean(1)for d,lo,hi in donors]);snow_fraction=float(np.mean(cap>195));black_fraction=float(np.mean(cap<18));mean=float(np.median(donor_values))
    if snow_fraction<.20 and black_fraction<.05 and float(cap.mean())-mean<30:continue
    for depth in range(48):
     target_depth=min(donors,key=lambda q:abs(q[0]-(30+depth//2)));d,lo,hi=target_depth
     pos=np.clip((np.arange(144)-indices[0])/max(1,indices[-1]-indices[0]),0,1)*(hi-lo)+lo
     grain=np.stack([np.interp(pos,np.arange(144),line(d)[:,c])for c in range(3)],axis=1)
     weight=coverage*(1-smooth(depth/48))
     if vertical:
      yy=y0+depth if side=='T'else y0+143-depth;xx=slice(x0,x0+144)
      after[yy,xx]=np.uint8(np.rint(after[yy,xx]*(1-weight[:,None])+grain*weight[:,None]));support[yy,xx]=np.maximum(support[yy,xx],weight)
     else:
      xx=x0+depth if side=='L'else x0+143-depth;yy=slice(y0,y0+144)
      after[yy,xx]=np.uint8(np.rint(after[yy,xx]*(1-weight[:,None])+grain*weight[:,None]));support[yy,xx]=np.maximum(support[yy,xx],weight)
    exits.append(dict(icon=icon,side=side,native_span_px=[int(indices[0]),int(indices[-1])],snow_fraction=snow_fraction,donor_median=mean));repaired+=1
  dest=out/name;dest.mkdir(exist_ok=True);Image.fromarray(after).save(dest/'albedo.png');Image.fromarray(np.uint8(np.rint(support*255))).save(dest/'exit_mask.png');Image.new('L',(w*144,h*144),0).save(dest/'player_colour_mask.png')
  # Retain the first authoring pass's strip coverage and add the repaired contacts.
  old_mask=np.asarray(Image.open(source/name/'exit_mask.png'),float)/255 if bank=='RASNOW'else np.zeros(support.shape)
  Image.fromarray(np.uint8(np.rint(np.maximum(old_mask,support)*255))).save(dest/'strip_mask.png')
  small=np.zeros((h*24,w*24,3),np.uint8);occupied=np.zeros((h*24,w*24),bool)
  for tid,icon,data in slots:
   if tid!=native_meta[name]['tid']:continue
   yy,xx=divmod(icon,w);small[yy*24:(yy+1)*24,xx*24:(xx+1)*24]=palette[np.frombuffer(data,np.uint8)].reshape(24,24,3);occupied[yy*24:(yy+1)*24,xx*24:(xx+1)*24]=True
  weight=nd.gaussian_filter(occupied.astype(float),.65);value=nd.gaussian_filter(small.mean(2).astype(float)*occupied,.65)/np.maximum(weight,1e-6)
  native_body=(value<190)&occupied
  body=np.asarray(Image.fromarray(np.uint8(native_body)*255).resize((w*144,h*144),Image.Resampling.BILINEAR))>48
  # Keep native road, detached source stones and all warm mineral grit. Replace
  # only exposed Snow, including obsolete baked rectangular shade around it.
  warm=(after.mean(2)<195)&(after[...,0].astype(int)-after[...,2].astype(int)>5)
  protect=nd.binary_dilation(body|warm,iterations=2)
  ground=np.uint8(np.rint(255*smooth(nd.distance_transform_edt(~protect)/10)))
  Image.fromarray(ground).save(dest/'ground_mask.png')
  files={p.name:sha(p)for p in dest.glob('*.png')};rows.append(dict(name=name,tid=row['tid'],cells=row['cells'],have=row['have'],source=str(src.relative_to(ROOT)),source_sha256=sha(src),native_source_sha256=native[name]['native_sha256'],exits=exits,files=files))
 (out/'MANIFEST.json').write_text(json.dumps(dict(schema=2,theater=bank,contract='Existing own road grain extended through saved native exits;48px C2 inward support; originals/v1, geometry, interior and player identity preserved.',native_exits_source=str((source/'native_exits_v02.json').relative_to(ROOT)),native_exits_sha256=sha(source/'native_exits_v02.json'),native_exits_audited=audited,contacts_repaired=repaired,assets=rows),indent=2)+'\n');print(bank,': audited',audited,'native exits; repaired',repaired)
if __name__=='__main__':
 for bank in ['RASNOW','SNOW','WINTER']:build(bank)
