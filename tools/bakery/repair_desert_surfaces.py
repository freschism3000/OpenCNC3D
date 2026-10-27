#!/usr/bin/env python3
"""Save complete bank/water/bush coverage, without editing immutable paintings.

All source classification is an offline authoring operation. Runtime draws saved
coverage; it never guesses stone, water or foliage from diffuse hue. Existing
mesh geometry and UVs are untouched. Ground feathering occurs outside stone.
"""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd
from scipy.spatial import ConvexHull

LUMA=np.array([.2126,.7152,.0722])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def morph(a,op,n):
    pad=n*2+2;return op(np.pad(a,pad,mode='edge'),iterations=n)[pad:-pad,pad:-pad]
def large(a,minsize):
    lab,n=nd.label(a);sizes=np.bincount(lab.ravel());return a&(sizes[lab]>=minsize)
def filled_small(a,limit):
    holes=nd.binary_fill_holes(a)&~a;lab,n=nd.label(holes);sizes=np.bincount(lab.ravel());return a|(holes&(sizes[lab]<=limit))
def foliage_body(seed,water):
    # Sparse green highlights previously left the entire brown/black bush body
    # behind as raised rock. Group the saved leaf samples and retain their full
    # crown envelope, including the original painted contact shadow.
    lab,n=nd.label(nd.binary_dilation(seed,iterations=26));body=np.zeros_like(seed)
    for i in range(1,n+1):
        yy,xx=np.where(seed&(lab==i))
        if len(xx)<18:continue
        points=np.c_[xx,yy]
        if len(np.unique(xx))<3 or len(np.unique(yy))<3:continue
        hull=ConvexHull(points);im=Image.new('1',(seed.shape[1],seed.shape[0]));ImageDraw.Draw(im).polygon([tuple(map(int,p))for p in points[hull.vertices]],fill=1)
        body|=nd.binary_dilation(np.array(im,dtype=bool),iterations=10)
    return body&~nd.binary_dilation(water,iterations=2)
def save(p,name,a):
    Image.fromarray(np.uint8(np.rint(np.clip(a,0,1)*255))).save(p/name)
def material_sample(material,x,y,seed):
    h,w=material.shape;u=np.mod(x+seed*137,w);v=np.mod(y+seed*311,h)
    return material[v.astype(int),u.astype(int)]
def repair(root,project):
    d=project/'art/remaster/textures/unified_daylight_v01';m=json.loads((d/'MANIFEST.json').read_text());native=root/'art/remaster/textures/DESERT';meta=json.loads((native/'set_144.json').read_text())['templates']
    coastroot=root/'art/remaster/textures/DESERT/2026-10-18/cliffs_shores_v04';cm=json.loads((coastroot/'COAST_MANIFEST.json').read_text());coasts={q['name']:q for q in cm['templates']}
    material=project/'art/remaster/ROCKS/cliff_sandstone_v05/material.png';mineral=np.asarray(Image.open(material).convert('RGB'),float)/255@LUMA;mineral-=mineral.mean()-.5
    rows={q['name']:q for q in m['assets']if q['theater']=='DESERT'and q['variant']==0}
    names=['RV'+str(i)for i in range(14,26)]+['FALLS1','FALLS2','FORD2','BRIDGE3','BRIDGE4','P02']
    features=root/'art/remaster/textures/DESERT/2026-10-19/cliff_shore_repair_v07';fm=json.loads((features/'FEATURE_MANIFEST.json').read_text());fr={q['name']:q for q in fm['assets']if q['variant']==0}
    report=[]
    for name in names:
        new=name not in rows
        if new:
            t=meta[name];src=native/name/'final_144.png';q=dict(name=name,variant=0,source=str(src.relative_to(root)),source_sha256=sha(src),cells=t['cells'],tid=t['tid'],have=t['have'],theater='DESERT',margins=[])
            for mar in t.get('margins',[]):
                p=native/name/mar['file'];q['margins'].append(dict(icon=mar['icon'],side=mar['side'],file=str(p),sha256=sha(p)))
            m['assets'].append(q);rows[name]=q
        else:q=rows[name]
        src=root/q['source'];rgb=np.asarray(Image.open(src).convert('RGB'),float)/255;rr,gg,bb=rgb.transpose(2,0,1);y=rgb@LUMA;w,h=q['cells'];have=np.zeros(y.shape,bool)
        for icon in q['have']:
            iz,ix=divmod(icon,w);have[iz*144:(iz+1)*144,ix*144:(ix+1)*144]=True
        old=d/q.get('pre_surface_v04_export',q.get('export',''))
        if not new:q.setdefault('pre_surface_v04_export',q['export'])
        water=np.zeros_like(have)
        if name in coasts:
            oldalpha=np.asarray(Image.open(coastroot/coasts[name]['land_alpha']))/255
            wet=(bb>rr+.015)&(bb>gg*.85)&(nd.distance_transform_edt(oldalpha>.5)<12)
            water=large(filled_small(morph(wet,nd.binary_closing,2),700),500)&have
        fol=np.zeros_like(have)
        if name.startswith('RV') and (old/'foliage_mask.png').exists():
            fol=foliage_body(np.asarray(Image.open(old/'foliage_mask.png'))>127,water)&have
        elif name=='FALLS1':
            fol=foliage_body((gg>rr*.95)&(bb<gg*.82)&(gg>.03),water)&have
        if name in ['FALLS1','FALLS2','FORD2','BRIDGE3','BRIDGE4']:
            seed=(rr<gg*1.47)&(bb>gg*.60)&~water
            # Bridge timber is outside its two explicitly recorded bank envelopes.
            if name in ['BRIDGE3','BRIDGE4']:
                allowed=np.zeros_like(have)
                yy,xx=np.indices(have.shape);s=xx/(w*144)+yy/(h*144)
                allowed=(s<.65)|(s>1.35) if name=='BRIDGE3' else (yy/(h*144)-xx/(w*144)<-.25)|(yy/(h*144)-xx/(w*144)>.25)
                seed&=allowed
            seed&=np.asarray(Image.open(features/fr[name]['export']/'ground_mask.png'))<128
        elif name=='P02':
            seed=(bb>rr*.72)&(bb>gg*.83)&(y>.32)
        else:seed=np.asarray(Image.open(old/'rock_mask.png'))>127
        rock=large(morph(morph(seed&~water&~fol,nd.binary_closing,2),nd.binary_opening,1),70)&have&~water&~fol
        # Fill enclosed mineral fissures, but never reinsert a retired bush crown.
        rock=nd.binary_fill_holes(rock)&~fol&~water&have
        dist=nd.distance_transform_edt(np.pad(~water,16,mode='edge'))[16:-16,16:-16]-.5
        distwater=nd.distance_transform_edt(np.pad(water,16,mode='edge'))[16:-16,16:-16]-.5
        # Match the shared 2.5-native-texel coast reach at 144px/cell.
        # Smooth endpoints avoid a second visible cut at the fade's boundary.
        alpha=np.clip(.5+np.where(water,-distwater,dist)/30,0,1) if water.any() else np.ones_like(y)
        alpha=alpha*alpha*(3-2*alpha)
        outside=np.clip(nd.distance_transform_edt(np.pad(~rock,12,mode='edge'))[12:-12,12:-12]/5,0,1);ground=outside*outside*(3-2*outside)*alpha*have
        ground[fol]=0 # the native painting remains a deliberate no-mesh fallback
        near=nd.distance_transform_edt(~rock,return_distances=False,return_indices=True)
        low=nd.gaussian_filter(y,14);broad=nd.gaussian_filter(y,42)
        native_detail=(y-low)*.60+(low-broad)*.25;yy,xx=np.indices(y.shape);tex=material_sample(mineral,xx,yy,q['tid'])
        value=np.clip(.47+native_detail+(tex-.5)*.80,.22,.76)
        paint=np.clip(value[...,None]*[1.16,1.015,.80],0,1)
        # Extend RGB under only the narrow exterior feather. No ground is mixed
        # into this albedo before the world-ground shader applies it again.
        paint=paint[near[0],near[1]];result=np.where((ground<.999)[...,None],paint,rgb)
        result=np.where((water|fol)[...,None],rgb,result)
        if name=='P02':result=rgb.copy()
        if name.startswith('BRIDGE'):
            protected=(~rock)&(ground<.5)&~water;result[protected]=rgb[protected]
            # Timber is intentionally not world-ground: retain its original pixels.
            protected=(~rock)&(np.asarray(Image.open(features/fr[name]['export']/'ground_mask.png'))<128)&~water;ground[protected]=0;result[protected]=rgb[protected]
        rise=np.clip(nd.distance_transform_edt(rock)/18,0,1);height=nd.gaussian_filter(rise*.65,1)*rise
        if name=='P02':height[:]=0
        out=d/'desert_surfaces_v04'/name;out.mkdir(parents=True,exist_ok=True)
        for f,a in [('albedo.png',result),('rock_mask.png',rock),('ground_mask.png',ground),('height.png',height),('water_mask.png',water),('foliage_mask.png',fol),('land_alpha.png',alpha)]:save(out,f,a)
        Image.new('L',(w*144,h*144),0).save(out/'player_colour_mask.png')
        # New surface faces replace the old colour-only skirts. Bridge timber
        # retains its authored strips; the small mineral patch retains only ore.
        margins=[]
        for mar in q['margins']:
            p=Path(mar['file']);p=p if p.is_absolute() else d/p;im=np.array(Image.open(p).convert('RGBA'))
            if name.startswith('RV') or name.startswith('FALLS') or name=='FORD2':im[...,3]=0
            if name=='P02':
                c=im[...,:3].astype(float)/255;keep=(c[...,2]>c[...,0]*.72)&(c[...,2]>c[...,1]*.83)&((c@LUMA)>.32);im[...,3]*=nd.binary_dilation(keep,iterations=1).astype('u1')
            p=out/Path(mar['file']).name;Image.fromarray(im,'RGBA').save(p);margins.append(dict(mar,file=str(p.relative_to(d)),sha256=sha(p)))
        q.update(export=str(out.relative_to(d)),surface_kind='mineral-detail'if name=='P02'else'river-rock',rock_pixels=int(rock.sum()),margins=margins,material_recipe='Complete native bank forms with shared native-density cliff mineral detail; explicit full bush retirement, common water cut and outside-only ground feather.',shared_mineral_detail_source='art/remaster/ROCKS/cliff_sandstone_v05/material.png')
        for f,k in [('albedo.png','albedo_sha256'),('rock_mask.png','rock_mask_sha256'),('ground_mask.png','ground_mask_sha256'),('height.png','height_sha256'),('player_colour_mask.png','player_colour_mask_sha256'),('land_alpha.png','land_alpha_sha256')]:q[k]=sha(out/f)
        report.append(dict(name=name,rock_pixels=int(rock.sum()),retired_bush_pixels=int(fol.sum()),water_pixels=int(water.sum()),height_on_retired_bush_max=float(height[fol].max())if fol.any()else 0))
    (d/'MANIFEST.json').write_text(json.dumps(m,indent=2)+'\n');(d/'desert_surfaces_v04/REPORT.json').write_text(json.dumps(dict(material_sha256=sha(material),templates=report),indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();repair(a.source,a.project)
