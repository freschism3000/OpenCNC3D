#!/usr/bin/env python3
"""Compile source-labelled Desert decorations from existing shipped meshes.

No geometry is designed here: the shipped bush LODs
are reused with their original normals/UVs. Saved source labels place instances,
not random scatter, and provide a coupled alternate ground matte. Missing runtime
models leave the original painting/matte available as a fallback.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,struct,zlib
import numpy as np
from PIL import Image
from scipy import ndimage as nd

def module(p,n):
 s=importlib.util.spec_from_file_location(n,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def components(mask,minsize=160):
 labels,n=nd.label(mask);out=[]
 for k in range(1,n+1):
  z,x=np.where(labels==k)
  if len(x)>=minsize:out.append((labels==k,x,z))
 return out

def peaks(mask):
 d=nd.distance_transform_edt(mask);lab,n=nd.label((d==nd.maximum_filter(d,45))&(d>8));cand=[]
 for k in range(1,n+1):
  z,x=np.where(lab==k);i=np.argmax(d[z,x]);cand.append((float(d[z[i],x[i]]),float(x[i]),float(z[i])))
 out=[]
 for r,x,z in sorted(cand,reverse=True):
  if all(np.hypot(x-a,z-b)>.90*(r+q) for q,a,b in out):out.append((r,x,z))
 return out

def masks(root,out,surface_source=None):
 """One-time source labelling, saved for inspection; never runs in the game."""
 out.mkdir(parents=True,exist_ok=True);meta=json.loads((root/'art/remaster/textures/DESERT/set_144.json').read_text())['templates'];day=surface_source or root/'art/remaster/textures/unified_daylight_v01';entries=[]
 rows=json.loads((day/'MANIFEST.json').read_text())['assets']
 for name in ['BR1','BR2','BR6','BR7','BR8','BR9']:
  q=meta[name];p=root/'art/remaster/textures/DESERT'/name/'final_144.png';rgb=np.asarray(Image.open(p).convert('RGB'),float)/255;r,g,b=rgb.transpose(2,0,1)
  leaf=(g>r*.88)&(b<g*.72)&(g>.05)
  leaf=nd.binary_fill_holes(nd.binary_closing(leaf,iterations=5));leaf=nd.binary_dilation(leaf,iterations=4)
  # Include the whole old painted shadow beneath a bush, not its cast silhouette.
  for region,x,z in components(leaf):
   leaf|=nd.binary_dilation(region,iterations=12)
  Image.fromarray(np.uint8(leaf*255)).save(out/(name+'_foliage.png'))
  entries.append(dict(name=name,tid=q['tid'],variant=0,cells=q['cells'],have=q['have'],foliage=name+'_foliage.png',source=str(p.relative_to(root)),source_sha256=sha(p)))
 for q in rows:
  if q['theater']!='DESERT' or q.get('surface_kind')!='river-rock':continue
  p=day/q['export'];name=q['name']
  for kind in ['rock','foliage','water']:
   (out/(name+'_'+kind+'.png')).write_bytes((p/(kind+'_mask.png')).read_bytes())
  entries.append(dict(name=name,tid=q['tid'],variant=q['variant'],cells=q['cells'],have=q['have'],rock=name+'_rock.png',foliage=name+'_foliage.png',water=name+'_water.png',source=q['source'],source_sha256=q['source_sha256']))
  (out/(name+'_ground.png')).write_bytes((p/'ground_mask.png').read_bytes())
 # Shore bushes are explicitly bounded from the native source drawings; the
 # dark leaf matte inside each recorded envelope excludes the coast and water.
 shore={'SH31':(270,280,420,430),'SH42':(40,2,108,90),'SH51':(80,225,255,425),'SH52':(425,215,525,325),'SH54':(135,12,286,90)}
 for name,box in shore.items():
  q=next(r for r in rows if r['theater']=='DESERT' and r['name']==name and r['variant']==0)
  rgb=np.asarray(Image.open(root/q['source']).convert('RGB'),float)/255;y=rgb@np.array([.2126,.7152,.0722]);select=np.zeros(y.shape,bool);x0,y0,x1,y1=box;select[y0:y1,x0:x1]=True
  leaf=nd.binary_dilation(nd.binary_fill_holes(nd.binary_closing((y<.34)&select,iterations=2)),iterations=3)
  lab,n=nd.label(leaf);sizes=np.bincount(lab.ravel());leaf&=sizes[lab]>100
  Image.fromarray(np.uint8(leaf*255)).save(out/(name+'_foliage.png'));(out/(name+'_ground.png')).write_bytes((day/q['export']/'ground_mask.png').read_bytes())
  entries.append(dict(name=name,tid=q['tid'],variant=0,cells=q['cells'],have=q['have'],foliage=name+'_foliage.png',source=q['source'],source_sha256=q['source_sha256'],author_outline=box,semantic='Saved source bush envelope; no runtime colour classification.'))
 (out/'labels.json').write_text(json.dumps(dict(contract='Explicit saved source labels; dry land, river water and old painted bushes remain separate.',entries=entries),indent=2)+'\n')

def bush_pack(root,out,author):
 raw=(root/'data/foliage/foliage3d.pack').read_bytes();nm,ns=struct.unpack_from('<2I',raw,12);p=20;widths=[]
 for i in range(nm):
  nv,nt,ts=struct.unpack_from('<3I',raw,p);p+=20;v=np.frombuffer(raw,'<f4',nv*12,p).reshape(nv,12);p+=nv*48+nt*12
  nb,nf=struct.unpack_from('<2I',raw,p);p+=8+nv*6*(nb>0)+nb*nf*48
  widths.append(float(np.ptp(v[:,[0,2]],axis=0).max()))
 nc=struct.unpack_from('<I',raw,p)[0];p+=4+nc*12;prefix=raw[:p];pieces=[prefix];bt=module(root/'game/bake_tree3d.py','bush_writer');pairs=[]
 for i in range(ns):
  head=raw[p:p+16];p+=16;pieces.append(head)
  for k in range(4):
   begin=p;nl=struct.unpack_from('<I',raw,p)[0];p+=4;image=None
   for l in range(nl):
    w,h,rs,cs=struct.unpack_from('<4I',raw,p);p+=16;blob=raw[p:p+cs];p+=cs
    if l==0:image=np.frombuffer(zlib.decompress(blob),np.uint8).reshape(h,w,4).copy()
   if k==0:
    rgb=image[...,:3].astype(float)/255;y=rgb@np.array([.2126,.7152,.0722]);rgb=.25*rgb+.75*y[...,None]*[1.10,1.12,.72];image[...,:3]=np.uint8(np.rint(np.clip(rgb,0,1)*255))
    pieces.append(bt._sheet_blob(image,True));name=f'bush{i}_albedo.png';Image.fromarray(image).save(author/name);maskname=f'bush{i}_player_mask.png';Image.new('L',(image.shape[1],image.shape[0]),0).save(author/maskname);pairs.append(dict(albedo=name,player_colour_mask=maskname,recolourable=False))
   else:
    pieces.append(raw[begin:p])
    if k==2:
     name=f'bush{i}_bark_albedo.png';maskname=f'bush{i}_bark_player_mask.png'
     Image.fromarray(image).save(author/name);Image.new('L',(image.shape[1],image.shape[0]),0).save(author/maskname)
     pairs.append(dict(albedo=name,player_colour_mask=maskname,recolourable=False))
 assert p==len(raw);(out/'foliage3d_DESERT.pack').write_bytes(b''.join(pieces))
 return [widths[0],widths[3]],dict(source_sha256=hashlib.sha256(raw).hexdigest(),geometry_uv_prefix_sha256=hashlib.sha256(prefix).hexdigest(),geometry_uv_bytes_unchanged=True,pairs=pairs)

def build(root,author,out,rocks):
 out.mkdir(parents=True,exist_ok=True);data=json.loads((author/'labels.json').read_text());report=[];bwidth,bushreport=bush_pack(root,out,author);placements=[];alt=[]
 for q in data['entries']:
  w,h=q['cells'];fol=np.asarray(Image.open(author/q['foliage']))>127;water=np.asarray(Image.open(author/q['water']))>127 if 'water'in q else np.zeros_like(fol);rock=np.asarray(Image.open(author/q['rock']))>127 if 'rock'in q else np.zeros_like(fol)
  # Every component is represented by existing bush meshes, with a few overlapping
  # crowns in the large clumps. Their roots use source coordinates, never scatter RNG.
  plants=[]
  for region,x,z in components(fol):
   d=nd.distance_transform_edt(region);cand=peaks(region)
   if not cand:cand=[(max(10,float(d.max())),float(x.mean()),float(z.mean()))]
   for k,(r,px,pz) in enumerate(cand):
    model=k%2;height=2*r/144/bwidth[model]*1.20
    plants.append((model,px/144,pz/144,max(.15,height),float((k*137+q['tid']*29)%360)))
  for model,px,pz,scale,yaw in plants:
   ix=int(np.floor(px));iz=int(np.floor(pz));icon=iz*w+ix
   # Root can lie outside a piece due to the model's original origin. Assign it to
   # the nearest occupied cell and keep the signed offset, preserving that origin.
   if icon not in q['have'] or ix<0 or iz<0 or ix>=w or iz>=h:
    icon=min(q['have'],key=lambda n:(n%w+.5-px)**2+(n//w+.5-pz)**2);iz,ix=divmod(icon,w)
   placements.append(dict(tid=q['tid'],icon=icon,variant=q['variant'],model=int(model),x=float(px-ix),z=float(pz-iz),scale=float(scale),yaw=float(yaw),name=q['name']))
  # Alternate coverage removes bush paint while retaining the rock and river
  # mattes. Only a successfully loaded foliage family enables this alternate.
  gp=author/(q['name']+'_ground.png')
  ground=np.asarray(Image.open(gp)).copy() if gp.exists() else np.full(fol.shape,255,np.uint8)
  ground[fol]=255
  for icon in q['have']:
   iz,ix=divmod(icon,w);key=q['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',q['tid'],icon,q['variant'],0,1)
   tile=ground[iz*144:(iz+1)*144,ix*144:(ix+1)*144];la=np.stack((tile,np.full_like(tile,128)),axis=-1);alt.append((key,la.tobytes()))
  q['instances']=dict(bushes=len(plants))
 raw=b'C3DDECO\0'+struct.pack('<II',1,len(placements))
 for a in placements:raw+=struct.pack('<HHBBH4f',a['tid'],a['icon'],a['variant'],a['model'],0,a['x'],a['z'],a['scale'],a['yaw'])
 (out/'desert_details.pack').write_bytes(raw)
 (out/'desert_details_masks.pack').write_bytes(b'C3DGRAS\0'+struct.pack('<III',1,144,len(alt))+b''.join(k for k,v in alt)+b''.join(v for k,v in alt))
 (author/'instances.json').write_text(json.dumps(placements,indent=2)+'\n')
 (author/'manifest.json').write_text(json.dumps(dict(entries=data['entries'],bushes=bushreport,contract='Existing shipped geometry reused unchanged. Source labels are authored offline. Runtime masks and placements switch together; no baked cast shadows.'),indent=2)+'\n')
 print(len(placements),'source-placed decorations')

def deck_ground_overrides(surface_source,cell):
 """Saved bridge deck coverage also owns the old decoration-mask variants."""
 if surface_source is None:return {}
 surface_source=Path(surface_source);manifest=json.loads((surface_source/'MANIFEST.json').read_text());out={}
 for q in manifest['assets']:
  if q['theater']!='DESERT' or 'deck_support_mask_sha256'not in q:continue
  folder=surface_source/q['export'];images=[]
  for filename,key in [('deck_support_mask.png','deck_support_mask_sha256'),('deck_end_weight.png','deck_end_weight_sha256')]:
   path=folder/filename
   if sha(path)!=q[key]:raise ValueError('Bridge deck mask changed: '+str(path))
   image=Image.open(path).convert('L');images.append(image)
  w,h=q['cells']
  if any(im.size!=(w*cell,h*cell)for im in images):raise ValueError('Bridge deck mask dimensions changed')
  for icon in q['have']:
   z,x=divmod(icon,w);box=(x*cell,z*cell,(x+1)*cell,(z+1)*cell)
   out[(q['tid'],icon,q['variant'],0)]=(np.asarray(images[0].crop(box))>0,np.asarray(images[1].crop(box)).astype(float)/255)
 return out

def ground_minimum_overrides(surface_source,cell):
 """Complete approach/bank retirement also survives frozen alternate exports."""
 if surface_source is None:return {}
 surface_source=Path(surface_source);manifest=json.loads((surface_source/'MANIFEST.json').read_text());out={}
 for q in manifest['assets']:
  if q['theater']!='DESERT' or 'ground_minimum_mask_sha256'not in q:continue
  folder=surface_source/q['export'];images=[]
  for filename,key in [('ground_minimum_mask.png','ground_minimum_mask_sha256'),('retirement_support_mask.png','retirement_support_mask_sha256')]:
   path=folder/filename
   if sha(path)!=q[key]:raise ValueError('Bridge ground retirement changed: '+str(path))
   with Image.open(path)as image:
    if image.mode!='L':raise ValueError('Bridge ground retirement must be linear grayscale')
    images.append(np.asarray(image).copy())
  w,h=q['cells']
  if any(image.shape!=(h*cell,w*cell)for image in images):raise ValueError('Bridge ground retirement dimensions changed')
  minimum,support=images
  if 'ground_override_mask_sha256'in q:
   path=folder/'ground_override_mask.png'
   if sha(path)!=q['ground_override_mask_sha256']:raise ValueError('Bridge boundary support changed: '+str(path))
   with Image.open(path)as image:boundary=np.asarray(image).copy()
   if boundary.shape!=support.shape or not np.isin(boundary,[0,255]).all():raise ValueError('Bridge boundary support dimensions or binary values changed')
   support=np.maximum(support,boundary)
  if np.any(minimum[support==0]):raise ValueError('Bridge ground retirement outside authored support')
  for icon in q['have']:
   z,x=divmod(icon,w)
   out[(q['tid'],icon,q['variant'],0)]=minimum[z*cell:(z+1)*cell,x*cell:(x+1)*cell]
 return out

def exact_ground_overrides(surface_source,cell):
 """A bounded source-semantic mineral/skirt repair runs last, on both variants."""
 if surface_source is None:return {}
 surface_source=Path(surface_source);manifest=json.loads((surface_source/'MANIFEST.json').read_text());out={}
 for q in manifest['assets']:
  if q['theater']!='DESERT' or 'ground_override_mask_sha256'not in q:continue
  folder=surface_source/q['export'];images=[]
  for filename,key in [('ground_override_mask.png','ground_override_mask_sha256'),('ground_override_values.png','ground_override_values_sha256'),('ground_mask.png','ground_mask_sha256')]:
   path=folder/filename
   if sha(path)!=q[key]:raise ValueError('Exact bridge ground source changed: '+str(path))
   with Image.open(path)as image:
    if image.mode!='L':raise ValueError('Exact bridge ground source must be grayscale')
    images.append(np.asarray(image).copy())
  support,values,ground=images;w,h=q['cells']
  if any(image.shape!=(h*cell,w*cell)for image in images):raise ValueError('Exact bridge ground dimensions changed')
  if not np.isin(support,[0,255]).all():raise ValueError('Exact bridge ground support must be binary')
  support=support>0
  if np.any(values[~support])or not np.array_equal(values[support],ground[support]):raise ValueError('Exact bridge ground override exceeds or differs from saved source support')
  for icon in q['have']:
   z,x=divmod(icon,w);box=(slice(z*cell,(z+1)*cell),slice(x*cell,(x+1)*cell))
   out[(q['tid'],icon,q['variant'],0)]=(support[box],values[box])
 return out

def contact_ground_minimums(surface_source,cell):
 if surface_source is None:return {}
 directory=Path(surface_source);out={}
 for row in json.loads((directory/'MANIFEST.json').read_text())['assets']:
  if row['theater']!='DESERT' or 'ground_contact_file'not in row:continue
  p=directory/row['ground_contact_file']
  if sha(p)!=row['ground_contact_sha256']:raise ValueError('Ground contact source changed: '+str(p))
  im=Image.open(p)
  if im.mode!='L':raise ValueError('Ground contact must be linear grayscale')
  a=np.asarray(im);w,h=row['cells']
  if a.shape!=(h*cell,w*cell):raise ValueError('Ground contact dimensions changed')
  for icon in row['have']:
   z,x=divmod(icon,w);out[(row['tid'],icon,row['variant'],0)]=a[z*cell:(z+1)*cell,x*cell:(x+1)*cell]
 return out

def append_masks(base,extra,*,surface_source=None):
 raw=base.read_bytes();data=extra.read_bytes();v,c,n=struct.unpack_from('<III',raw,8);_,cc,nn=struct.unpack_from('<III',data,8);assert c==cc==144
 stride=c*c*2;rows=[];decks=deck_ground_overrides(surface_source,c);minimums=ground_minimum_overrides(surface_source,c);exacts=exact_ground_overrides(surface_source,c);contacts=contact_ground_minimums(surface_source,c)
 for i in range(n):
  k=raw[20+i*24:44+i*24]
  if struct.unpack_from('<H',k,22)[0]==0:
   pixels=bytearray(raw[20+n*24+i*stride:20+n*24+(i+1)*stride]);identity=struct.unpack_from('<HHBB',k,16)
   if identity in minimums:
    tile=np.frombuffer(pixels,np.uint8).reshape(c,c,2);tile[...,0]=np.maximum(tile[...,0],minimums[identity])
   if identity in exacts:
    support,values=exacts[identity];tile=np.frombuffer(pixels,np.uint8).reshape(c,c,2);tile[...,0][support]=values[support]
   if identity in contacts:
    tile=np.frombuffer(pixels,np.uint8).reshape(c,c,2);tile[...,0]=np.maximum(tile[...,0],contacts[identity])
   rows.append((k,bytes(pixels)))
 for i in range(nn):
  key=data[20+i*24:44+i*24];pixels=bytearray(data[20+nn*24+i*stride:20+nn*24+(i+1)*stride]);identity=struct.unpack_from('<HHBB',key,16)
  if identity in decks:
   # Frozen decoration exports predate the deck repair. Preserve their bush
   # retirement outside the explicit road support and their exact shade bytes.
   support,weight=decks[identity];tile=np.frombuffer(pixels,np.uint8).reshape(c,c,2);tile[...,0][support]=np.uint8(np.rint(tile[...,0][support]*(1-weight[support])))
  if identity in minimums:
   tile=np.frombuffer(pixels,np.uint8).reshape(c,c,2);tile[...,0]=np.maximum(tile[...,0],minimums[identity])
  if identity in exacts:
   support,values=exacts[identity];tile=np.frombuffer(pixels,np.uint8).reshape(c,c,2);tile[...,0][support]=values[support]
  if identity in contacts:
   tile=np.frombuffer(pixels,np.uint8).reshape(c,c,2);tile[...,0]=np.maximum(tile[...,0],contacts[identity])
  rows.append((key,bytes(pixels)))
 base.write_bytes(b'C3DGRAS\0'+struct.pack('<III',v,c,len(rows))+b''.join(k for k,p in rows)+b''.join(p for k,p in rows))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--author',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--rocks',type=Path);p.add_argument('--label',action='store_true');p.add_argument('--append',type=Path);a=p.parse_args()
 if a.label:masks(a.root,a.author)
 if not a.label or a.rocks:build(a.root,a.author,a.out,a.rocks)
 if a.append:append_masks(a.append,a.out/'desert_details_masks.pack',surface_source=a.root/'art/remaster/textures/unified_daylight_v01')
