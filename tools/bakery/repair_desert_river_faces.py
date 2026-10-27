#!/usr/bin/env python3
"""Rebuild saved source-led river face materials and mattes; preserve original art."""
from pathlib import Path
import json,hashlib,numpy as np
from PIL import Image
from scipy import ndimage as nd
P=Path(__file__).resolve().parents[2];R=P;D=P/'art/remaster/textures/unified_daylight_v01';A=P/'art/remaster/desert_details_v01';j=json.loads((D/'MANIFEST.json').read_text())
for q in j['assets']:
 if q.get('surface_kind')!='river-rock':continue
 original=q.get('pre_river_v03_export',q['export']);old=D/original;o=D/'desert_river_faces_v03'/q['name'];o.mkdir(parents=True,exist_ok=True)
 rgb=np.asarray(Image.open(R/q['source']).convert('RGB'),float)/255;r,g,b=rgb.transpose(2,0,1);seed=np.asarray(Image.open(old/'rock_mask.png'))>127
 water=np.asarray(Image.open(old/'water_mask.png'))>127;fol=np.asarray(Image.open(old/'foliage_mask.png'))>127
 mineral=(r<g*1.5)&(b>g*.58)&~water&~fol
 rock=nd.binary_fill_holes(nd.binary_closing(seed&mineral,iterations=3));rock=nd.binary_opening(rock,iterations=1)&seed
 lab,n=nd.label(rock);sizes=np.bincount(lab.ravel());rock&=sizes[lab]>100
 # Original face structure is the material source. The rejected v02 filled this
 # entire footprint with tiny repeating grain and erased every fracture plane.
 y=rgb@np.array([.2126,.7152,.0722]);low=nd.gaussian_filter(y,14);broad=nd.gaussian_filter(y,42)
 value=np.clip(.49+(y-low)*.90+(low-broad)*.35,.23,.72)
 painted=value[...,None]*[1.16,1.015,.80];result=np.where(rock[...,None],painted,rgb)
 distance=nd.distance_transform_edt(rock);h=(np.clip(distance/24,0,1)**.6)*(.58+.25*np.clip((y-.25)/.5,0,1))*rock;h=nd.gaussian_filter(h,.8)*rock
 ground=(~(water|fol|rock)).astype(float)
 for f,a in [('albedo.png',result),('rock_mask.png',rock),('ground_mask.png',ground),('height.png',h),('water_mask.png',water),('foliage_mask.png',fol)]:Image.fromarray(np.uint8(np.rint(np.clip(a,0,1)*255))).save(o/f)
 (o/'player_colour_mask.png').write_bytes((old/'player_colour_mask.png').read_bytes())
 (A/(q['name']+'_ground.png')).write_bytes((o/'ground_mask.png').read_bytes())
 q['pre_river_v03_export']=original;q['export']=str(o.relative_to(D));q['rock_pixels']=int(rock.sum());q['material_recipe']='Original native rock fracture planes, local broad-light removal, shared Desert sandstone colour; fixed cast-shadow ground removed by explicit ground matte.'
 for f,k in [('albedo.png','albedo_sha256'),('rock_mask.png','rock_mask_sha256'),('ground_mask.png','ground_mask_sha256'),('height.png','height_sha256'),('player_colour_mask.png','player_colour_mask_sha256')]:q[k]=hashlib.sha256((o/f).read_bytes()).hexdigest()
 # Existing margin keys remain; derive matching RGB in the compiler and remove
 # their painted foliage so it cannot return beside a replacement bush.
 margins=[];w,hh=q['cells']
 for mar in q['margins']:
  rgba=np.array(Image.open(D/mar['file']).convert('RGBA'));mh,mw=rgba.shape[:2];cy,cx=divmod(mar['icon'],w);ox,oy=(cx*144-mw,cy*144) if mar['side']=='L' else (cx*144,cy*144-mh)
  yy,xx=np.indices((mh,mw));xx=np.clip(xx+ox,0,w*144-1);yy=np.clip(yy+oy,0,hh*144-1);rgba[...,3]=np.uint8(rgba[...,3]*(~fol[yy,xx]));p=o/Path(mar['file']).name;Image.fromarray(rgba).save(p);margins.append(dict(mar,file=str(p.relative_to(D)),sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
 q['margins']=margins
(D/'MANIFEST.json').write_text(json.dumps(j,indent=2)+'\n')
# The compiler uses this finished neutral material directly; a second tint / grain
# application would destroy the same source face structure again.
p=P/'tools/bakery/terrain_daylight.py';s=p.read_text();s=s.replace("    _,mean_y=desert_material()\n    old=", "    if r.get('material_recipe'):\n        result=(checked(r,'albedo.png','RGB'),np.asarray(checked(r,'ground_mask.png','L')))\n        DESERT_CACHE[key]=result;return result\n    _,mean_y=desert_material()\n    old=",1);p.write_text(s)

# Shared cliff-detail pass, after resetting the river faces from original art.
from pathlib import Path
import json,numpy as np,hashlib,re
from PIL import Image
from scipy import ndimage as nd
P=Path(__file__).resolve().parents[2];R=P;D=P/'art/remaster/textures/unified_daylight_v01';j=json.loads((D/'MANIFEST.json').read_text());q=next(a for a in j['assets'] if a['theater']=='TEMPERAT' and a['name']=='S25' and a['variant']==0);f=D/q['export']/'albedo.png';rgb=np.asarray(Image.open(f).convert('RGB'),float)[30:205,112:210]/255;y=rgb@np.array([.2126,.7152,.0722]);v=np.clip(.5+(y-nd.gaussian_filter(y,18))*.95+(nd.gaussian_filter(y,18)-nd.gaussian_filter(y,50))*.25,.30,.70);h,w=v.shape
v-=np.linspace(-.5,.5,w)[None,:]*(v[:,-1]-v[:,0])[:,None];v-=np.linspace(-.5,.5,h)[:,None]*(v[-1]-v[0])[None,:]
o=P/'art/remaster/ROCKS/cliff_sandstone_v04';o.mkdir(parents=True,exist_ok=True);Image.fromarray(np.uint8(np.rint(np.clip(v[...,None]*[1.16,1.015,.80],0,1)*255))).save(o/'material.png');Image.new('L',(w,h),0).save(o/'material_player_mask.png');digest=hashlib.sha256((o/'material.png').read_bytes()).hexdigest();(o/'source.json').write_text(json.dumps(dict(source=str(f.relative_to(P)),source_sha256=hashlib.sha256(f.read_bytes()).hexdigest(),crop_xyxy=[112,30,210,205],material_sha256=digest,recipe='Same source cliff fracture planes, broad light removed, periodic edge correction, shared warm sandy colour. No painted cast shadow.'),indent=2)+'\n')
p=P/'tools/bakery/terrain_daylight.py';s=p.read_text().replace('ROCKS/shared_sandstone_v03/material.png','ROCKS/cliff_sandstone_v04/material.png');s=re.sub("DESERT_MATERIAL_SHA='[a-f0-9]+'",f"DESERT_MATERIAL_SHA='{digest}'",s);p.write_text(s)
# Add the same mineral surface detail to the native river face structure.
for q in j['assets']:
 if q.get('surface_kind')!='river-rock':continue
 p=D/q['export'];old=np.asarray(Image.open(p/'albedo.png').convert('RGB'),float)/255;mask=np.asarray(Image.open(p/'rock_mask.png'))>127;yy,xx=np.indices(mask.shape);tex=v[(yy+q['tid']*19)%h,(xx+q['tid']*13)%w]
 yold=old@np.array([.2126,.7152,.0722]);value=np.clip(.49+(yold-.49)*.70+(tex-v.mean())*.85,.23,.72);paint=value[...,None]*[1.20,1.005,.72];result=np.where(mask[...,None],paint,old);Image.fromarray(np.uint8(np.rint(np.clip(result,0,1)*255))).save(p/'albedo.png');q['albedo_sha256']=hashlib.sha256((p/'albedo.png').read_bytes()).hexdigest();q['shared_mineral_detail_source']='art/remaster/ROCKS/cliff_sandstone_v04/material.png'
(D/'MANIFEST.json').write_text(json.dumps(j,indent=2)+'\n')
p=P/'tools/art/rocks/stones.sh';s=p.read_text().replace('shared-sandstone-material-v03','cliff-sandstone-material-v04').replace('shared-sandstone-v03','cliff-sandstone-v04').replace('ROCKS/shared_sandstone_v03/material.png','ROCKS/cliff_sandstone_v04/material.png');p.write_text(s)
