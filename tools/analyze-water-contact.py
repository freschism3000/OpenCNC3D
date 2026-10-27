#!/usr/bin/env python3
"""Actual GDI1 contact-strip motion and shore-confined swash, from matching hidden captures.

Selects water material and plane-projected field distance, never colour/hue.
The ordinary visible bank feather is excluded independently using the GPU owner
mask. A no-shore run separates the foam/wet film from moving water itself.
"""
import argparse,importlib.util,json
from pathlib import Path
import cv2
import numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--before',type=Path,required=True);ap.add_argument('--after',type=Path,required=True);ap.add_argument('--control',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
s=importlib.util.spec_from_file_location('motion',a.source/'tools/analyze-water-shore-motion.py');M=importlib.util.module_from_spec(s);s.loader.exec_module(M)
m=M.projection(a.after,'phase-000');N,_=M.G.read_buffer(a.after/'phase-000','normals');D,_=M.G.read_buffer(a.after/'phase-000','depth');_,valid=M.G.world_positions(D[...,0],np.asarray(json.loads((a.after/'phase-000-matrices.json').read_text())['inverse_vp']).reshape(4,4).T)
P=m['surface'];rx,rz,rw,rh=m['runtime_map'];shown=(P[...,0]>rx+1)&(P[...,0]<rx+rw-1)&(P[...,2]>rz+1)&(P[...,2]<rz+rh-1)
water=valid&shown&M.G.water_class(N)&~M.object_effect_region(N)
opaque=M.G.read_opaque(a.after/'opaque.bin',water.shape);distance=cv2.distanceTransform((~opaque).astype('u1'),cv2.DIST_L2,cv2.DIST_MASK_PRECISE)
# Bound the 0.08-cell feather in screen pixels using the independent camera
# projection. Two extra pixels cover raster/derivative rounding, with the shader's
# absolute12px safety ceiling retained.
dx=np.gradient(P[...,[0,2]],axis=1);dy=np.gradient(P[...,[0,2]],axis=0)
feather=np.clip(.08/np.maximum(np.minimum(np.linalg.norm(dx,axis=2),np.linalg.norm(dy,axis=2)),.0001),1,12)+2
foot=water&(m['sd']>-.35)&(m['sd']<-.07)&(distance>feather)
sea=water&(m['sd']>.08)&(m['sd']<.45)&(distance>feather)
offshore=water&(m['sd']>1.2)
land=valid&shown&(N[...,3]>.75)&(m['sd']<-1.0)&~M.object_effect_region(N)
assert foot.sum()>500 and sea.sum()>1000 and offshore.sum()>5000
phases=[0,20,40,60,80,90]
def ranges(root):return np.ptp(np.stack([M.G.read_buffer(root/('phase-%03d'%i),'scene')[0][...,:3]for i in phases]),axis=0).max(2)
before=ranges(a.before);after=ranges(a.after)
sea_raw_max=max(float(np.abs(M.G.read_buffer(a.after/('phase-%03d'%i),'scene')[0][sea,:3]-M.G.read_buffer(a.control/('phase-%03d'%i),'scene')[0][sea,:3]).max())for i in phases)
records=[];foot_foam=np.zeros_like(water);sea_foam=np.zeros_like(water);offshore_max=0;land_max=0;sea_max=0
frames=sorted(a.after.glob('frame-[0-9][0-9][0-9].png'));assert len(frames)>=81
for p in frames:
 on=cv2.imread(str(p),cv2.IMREAD_COLOR).astype(float);off=cv2.imread(str(a.control/p.name),cv2.IMREAD_COLOR).astype(float);delta=(on-off).mean(2)
 foam=delta>6
 foot_foam|=foot&foam;sea_foam|=sea&foam
 sea_max=max(sea_max,int(np.abs(on-off)[sea].max()))
 offshore_max=max(offshore_max,int(np.abs(on-off)[offshore].max()));land_max=max(land_max,int(np.abs(on-off)[land].max()))
 records.append(dict(frame=p.name,foot_foam_pixels=int((foot&foam).sum()),sea_foam_pixels=int((sea&foam).sum())))
result=dict(passed=False,selection='Actual water normal material, runtime MAP inset1cell, independent camera-projected0.08cell opaque-bank exclusion plus2px raster allowance, source RGBA16 field distance; foam from matched no-shore image difference, no hue classification.',foot_pixels=int(foot.sum()),before_foot_moving_fraction=float((before[foot]>2/255).mean()),after_foot_moving_fraction=float((after[foot]>2/255).mean()),after_foot_median_colour_range=float(np.median(after[foot])),foot_foam_pixels_over_cycle=int(foot_foam.sum()),sea_foam_pixels_over_cycle=int(sea_foam.sum()),sea_pixels=int(sea.sum()),offshore_control_max_error=offshore_max,far_land_control_max_error=land_max,frames=len(frames),samples=records)
result['water_side_swash_max_error']=sea_max
result['water_side_raw_swash_max_error']=sea_raw_max
# Final post-processing can carry a <=3/255 filter tail across the narrow edge.
# The actual pre-post colour buffer must have no swash in this open-water band.
result['passed']=bool(result['after_foot_moving_fraction']>.95 and result['after_foot_moving_fraction']>result['before_foot_moving_fraction']+.10 and result['foot_foam_pixels_over_cycle']>foot.sum()*.1 and result['sea_foam_pixels_over_cycle']==0 and sea_raw_max==0 and sea_max<=3 and offshore_max==0 and land_max==0)
for name,mask in [('contact-strip',foot),('water-approach',sea),('contact-foam',foot_foam),('approach-foam',sea_foam)]:cv2.imwrite(str(a.output.parent/(name+'.png')),mask.astype('u1')*255)
a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items()if k!='samples'},indent=2));raise SystemExit(0 if result['passed']else 1)
