#!/usr/bin/env python3
"""Check actual GDI1 water motion, projected coast influence and GPU cycle output.

Consumes existing hidden captures only. Native screenshot pixel coordinates,
actual material ownership and the GPU RGBA16 coast field determine selections.
No image hue classification or successful source test can substitute for captures.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys

import cv2
import numpy as np


ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('water_ownership',ROOT/'tools/gate-water-normal-ownership.py')
G=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(G)


def object_effect_region(normals):
    # Native sprite AA ropes and colour-only muzzleflash/smoke extend beyond
    # normal-pass alpha coverage. Use actual object silhouettes, independently
    # of diagnostic RGB, to reserve one projected object height around each
    # component. This fixture's boat mast is within6px and firing plume within
    #56px of its66px-high silhouette; no source-water colour is classified away.
    objects=(normals[...,3]>.01)&(normals[...,3]<.75)&~G.water_class(normals)
    count,_,stats,_=cv2.connectedComponentsWithStats(objects.astype(np.uint8))
    height,width=objects.shape;region=np.zeros_like(objects)
    for x,y,w,h,area in stats[1:count]:
        if area<8:continue
        padding=max(8,int(h))
        region[max(0,y-padding):min(height,y+h+padding),max(0,x-padding):min(width,x+w+padding)]=True
    return region


def projection(directory,prefix='capture',map_inset=1):
    depth,_=G.read_buffer(directory/prefix,'depth')
    normals,_=G.read_buffer(directory/prefix,'normals')
    matrix=np.asarray(json.loads((directory/(prefix+'-matrices.json')).read_text())['inverse_vp']).reshape(4,4).T
    field,res,ran=G.read_field(directory/'field16.bin')
    world,valid=G.world_positions(depth[...,0],matrix)
    # GDI1 has one water body. Intersect the actual water surface instead of its
    # bed .04 cells below; the material mask still comes from the real depth pass.
    log=(directory/'run.log').read_text()
    body=re.search(r'WATER\|plane\|[^\n]*?\|bodies=(\d+)[^\n]*\|y=(-?\d+(?:\.\d+)?)\.\.(-?\d+(?:\.\d+)?)',log)
    if not body or int(body[1])!=1 or abs(float(body[2])-float(body[3]))>1e-5:
        raise ValueError('This motion fixture requires a logged single-level GDI1 water body')
    yy,xx=np.indices(depth.shape[:2]);height,width=depth.shape[:2]
    clip=np.stack(((xx+.5)/width*2-1,1-(yy+.5)/height*2,np.full((height,width),-1.),np.ones((height,width))),axis=-1)
    near=clip@matrix.T;near=near[...,:3]/near[...,3,None]
    clip[...,2]=1;far=clip@matrix.T;far=far[...,:3]/far[...,3,None]
    ray=far-near;den=ray[...,1];t=(float(body[2])-near[...,1])/np.where(abs(den)>1e-10,den,1)
    surface=near+ray*t[...,None]
    sd,alpha,inside=G.field_at(field,surface,res,ran)
    rect=re.search(r'MAP theater=\d+ origin=(\d+),(\d+) size=(\d+)x(\d+)',log)
    if not rect:raise ValueError('Missing actual runtime MAP rectangle')
    rx,rz,rw,rh=map(int,rect.groups())
    # The normal/depth extension reaches beyond the drawn colour surface; the
    # permanent one-cell shroud rim plus its corner-average feather also survives
    # --noshroud/reveal. Diagnostic colour needs either rim0 or two-cell inset;
    # visible production motion can retain the feather, with texture/reliability
    # checks excluding its fully black pixels.
    playable=(surface[...,0]>=rx+map_inset)&(surface[...,0]<rx+rw-map_inset)&(surface[...,2]>=rz+map_inset)&(surface[...,2]<rz+rh-map_inset)
    effects=object_effect_region(normals)
    water=valid&inside&playable&G.water_class(normals)&(alpha>=.999)&~effects
    support=cv2.erode(water.astype(np.uint8),np.ones((29,29),np.uint8),borderType=cv2.BORDER_CONSTANT,borderValue=0)>0
    near_mask=water&support&(sd>.45)&(sd<1.05)
    offshore=water&support&(sd>=3.0)
    land=valid&(normals[...,3]>.75)
    land=cv2.erode(land.astype(np.uint8),np.ones((29,29),np.uint8),borderType=cv2.BORDER_CONSTANT,borderValue=0)>0
    return dict(sd=sd,water=water,near=near_mask,offshore=offshore,land=land,shape=depth.shape[:2],plane=float(body[2]),runtime_map=[rx,rz,rw,rh],surface=surface,object_effect_pixels=int(effects.sum()))


def image(path,shape):
    bgr=cv2.imread(str(path),cv2.IMREAD_COLOR)
    if bgr is None or bgr.shape[:2]!=shape:raise ValueError('Missing/wrong native-size frame: '+str(path))
    return cv2.cvtColor(bgr,cv2.COLOR_BGR2GRAY)


def movement(directory,out):
    masks=projection(directory);files=sorted(directory.glob('frame-[0-9][0-9][0-9].png'))
    if len(files)<40:raise ValueError('Need at least40 consecutive production frames covering the swash cycle')
    pairs=[];failures=[]
    for kind in ('near','offshore'):
        if masks[kind].sum()<500:raise ValueError('Insufficient actual wet '+kind+' pixels')
        cv2.imwrite(str(out/(kind+'-mask.png')),masks[kind].astype(np.uint8)*255)
    opts=dict(pyr_scale=.5,levels=1,winsize=23,iterations=5,poly_n=7,poly_sigma=1.5,flags=0)
    yy,xx=np.indices(masks['shape'],dtype=np.float32)
    previous=image(files[0],masks['shape'])
    for first,last in zip(files,files[1:]):
        current=image(last,masks['shape'])
        flow=cv2.calcOpticalFlowFarneback(previous,current,None,**opts)
        back=cv2.calcOpticalFlowFarneback(current,previous,None,**opts)
        speed=np.linalg.norm(flow,axis=2)
        mx=xx+flow[...,0];my=yy+flow[...,1]
        fb=np.linalg.norm(flow+cv2.remap(back,mx,my,cv2.INTER_LINEAR),axis=2)
        photo=np.abs(previous.astype(np.float32)-cv2.remap(current.astype(np.float32),mx,my,cv2.INTER_LINEAR))
        grey=previous.astype(np.float32);mean=cv2.GaussianBlur(grey,(0,0),2)
        texture=cv2.GaussianBlur(grey*grey,(0,0),2)-mean*mean>=2.5**2
        reliable=(fb<=.5+.1*speed)&(photo<=12)&(mx>3)&(my>3)&(mx<masks['shape'][1]-4)&(my<masks['shape'][0]-4)
        row=dict(first=first.name,last=last.name)
        for kind in ('near','offshore','land'):
            pick=masks[kind]&texture&reliable
            n=int(pick.sum());values=speed[pick]
            row[kind]=dict(pixels=n,median_pixels_per_frame=float(np.median(values)) if n else None,moving_fraction=float((values>.05).mean()) if n else None)
            if kind!='land':
                if n<300:failures.append(last.name+': insufficient reliable '+kind+' motion observations')
                elif np.median(values)<=.05 or (values>.05).mean()<.50:
                    failures.append(last.name+': '+kind+' water appears paused')
        if row['land']['pixels']>=500 and row['land']['median_pixels_per_frame']>.05:
            failures.append(last.name+': static-land control moved; camera/lighting confound')
        pairs.append(row);previous=current
    return dict(passed=not failures,frames=len(files),pairs=pairs,failures=failures,selected_pixels={k:int(masks[k].sum())for k in('near','offshore','land')},selection='Actual water material+GPU wet alpha>=.999, runtime MAP inset1cell,14px pure-water window support; near sd.45..1.05, offshore sd>=3, foam band excluded',scope='Consecutive actual final-colour motion; Farneback low-speed gate0.05 native pixels/frame'),masks


def diagnostic_inset(directory):
    commands=(directory/'commands.txt').read_text()
    invocation=json.loads((directory/'invocation.json').read_text())
    return 1 if re.search(r'^rim\s+0\s*$',commands,re.M) or '--norim' in invocation.get('argv',[]) else 2


def band(directory,out):
    # Existing capture_shore_cases writes a swash-off control before its frames.
    # That buffer leaves the debug colour unpainted inside the beach-film reach.
    prefix='control' if (directory/'control-scene.bin.gz').exists() else 'capture'
    masks=projection(directory,prefix,diagnostic_inset(directory));scene,_=G.read_buffer(directory/prefix,'scene')
    sd=masks['sd'];pick=masks['water']&(sd>.30)
    q=np.clip((np.maximum(sd,0)-.35)/(2.5-.35),0,1);expected=1-q*q*q*(q*(q*6-15)+10)
    error=abs(scene[...,1]-expected);off=pick&(sd>=2.55);inner=pick&(sd<=.35);transition=pick&(sd>.45)&(sd<2.4)
    assertions=dict(minimum_water=bool(pick.sum()>=5000),minimum_offshore=bool(off.sum()>=5000),minimum_transition=bool(transition.sum()>=1000),offshore_influence_zero=bool(np.all(abs(scene[...,1][off])<1e-5)),sea_classification=bool(np.all(abs(scene[...,2][pick])<1e-5)),band_median_error=bool(np.median(error[pick])<.001),band_max_error=bool(np.max(error[pick])<.012))
    if inner.any():assertions['inner_full_lock']=bool(np.max(abs(scene[...,1][inner]-1))<.012)
    cv2.imwrite(str(out/'actual-band.png'),np.uint8(np.clip(scene[...,1],0,1)*255))
    return dict(passed=all(assertions.values()),assertions=assertions,water_pixels=int(pick.sum()),offshore_pixels=int(off.sum()),transition_pixels=int(transition.sum()),median_error=float(np.median(error[pick])),max_error=float(np.max(error[pick])),object_effect_region_pixels=masks['object_effect_pixels'],scope='Actual raw GPU audit1 green compared with independent RGBA16 field and water-plane projection; source-normal object/effect exclusion')


def cycle(directory):
    prefixes=sorted(p.name.removesuffix('-scene.bin.gz')for p in directory.glob('phase-*-scene.bin.gz'))
    if len(prefixes)<7:raise ValueError('Need seven audit6 raw scene dumps spread across the cycle')
    rows=[];bins=set();failures=[]
    for prefix in prefixes:
        masks=projection(directory,prefix,diagnostic_inset(directory));scene,_=G.read_buffer(directory/prefix,'scene')
        pick=masks['water']&(masks['sd']>.4)
        if pick.sum()<5000:raise ValueError('Insufficient actual wet audit6 pixels')
        values=scene[pick];bins.update(np.minimum(9,(values[:,0]*10).astype(int)).tolist())
        missing=int((values[:,2]<.999).sum())
        if missing:failures.append(prefix+': GPU continuous-speed flag missing')
        rows.append(dict(prefix=prefix,selected_pixels=int(pick.sum()),missing_continuous_speed_flag_pixels=missing,phase_median=float(np.median(values[:,0])),surge_median=float(np.median(values[:,1])),speed_min=float(values[:,2].min()),object_effect_region_pixels=masks['object_effect_pixels']))
    if len(bins)<9:failures.append('Audit6 capture did not cover at least90% of cycle phase bins')
    return dict(passed=not failures,observations=rows,phase_bins=sorted(bins),failures=failures,scope='Actual GPU diagnostic: phase red, swash surge green, continuously positive actual travel speed blue; pre-post scene buffers')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sequence',type=Path,required=True);p.add_argument('--audit1',type=Path,required=True);p.add_argument('--audit6',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    try:
        motion,_=movement(a.sequence,a.output);influence=band(a.audit1,a.output);speed=cycle(a.audit6)
        result=dict(passed=motion['passed']and influence['passed']and speed['passed'],motion=motion,directional_band=influence,gpu_cycle=speed,checker_sha256=G.digest(Path(__file__)),inputs=dict(sequence=str(a.sequence),audit1=str(a.audit1),audit6=str(a.audit6)))
    except (ValueError,OSError) as error:result=dict(passed=False,status='INCONCLUSIVE',error=str(error))
    (a.output/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items()if k not in('motion','gpu_cycle')},indent=2));return 0 if result['passed']else 1


if __name__=='__main__':sys.exit(main())
