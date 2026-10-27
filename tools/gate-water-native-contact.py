#!/usr/bin/env python3
"""Verify native contact heights, no generated bed, and actual GPU water depth."""
from pathlib import Path
import argparse,importlib.util,json,re,struct
import numpy as np
spec=importlib.util.spec_from_file_location('ownership',Path(__file__).with_name('gate-water-normal-ownership.py'));G=importlib.util.module_from_spec(spec);spec.loader.exec_module(G)
def check(o):
 b=(o/'bed.bin').read_bytes();assert b==b'C3DBED1\0'+struct.pack('<I',0),'generated seabed mesh remains'
 log=(o/'run.log').read_text();error=re.search(r'native_height_max_error=([\d.]+)\|generated_bank_vertices=0',log);assert error and float(error[1])==0,'water disagrees with native heights'
 b=(o/'plane.bin').read_bytes();assert b[:8]==b'C3DPLN1\0';w,h=struct.unpack_from('<2I',b,8);corners=np.frombuffer(b,'<f4',offset=16,count=w*h*4).reshape(h,w,4);ids=np.frombuffer(b,'<i4',offset=16+w*h*16).reshape(h,w)
 k=(ids[:,:-1]>=0)&(ids[:,1:]>=0);assert np.array_equal(corners[:,:-1,[1,3]][k],corners[:,1:,[0,2]][k]),'horizontal water crack'
 k=(ids[:-1]>=0)&(ids[1:]>=0);assert np.array_equal(corners[:-1,:,[2,3]][k],corners[1:,:,[0,1]][k]),'vertical water crack'
 d,_=G.read_buffer(o/'capture','depth');d=d[...,0];n,_=G.read_buffer(o/'capture','normals');M=np.array(json.loads((o/'capture-matrices.json').read_text())['inverse_vp']).reshape(4,4,order='F');P,valid=G.world_positions(d,M)
 def surface(Q):
  x=np.clip(np.floor(Q[...,0]).astype(int),0,w-1);z=np.clip(np.floor(Q[...,2]).astype(int),0,h-1);fx=Q[...,0]-x;fz=Q[...,2]-z;a,b,c,e=np.moveaxis(corners[z,x],-1,0)
  return np.where(fx+fz<=1,a+(b-a)*fx+(c-a)*fz,e+(c-e)*(1-fx)+(b-e)*(1-fz)),ids[z,x]
 field,res,ran=G.read_field(o/'field16.bin');sd,alpha,covered=G.field_at(field,P,res,ran);Y,owner=surface(P)
 water=valid&G.water_class(n)&(owner>=0)&(sd>.3)&(alpha>.999)&covered&~G.object_halo(n);count=int(water.sum());assert count>100
 e=abs(P[...,1]-Y);bad=int((water&(e>.002)).sum());assert bad==0,(o,bad,float(e[water].max()))
 # Independent projected water points: a background hole cannot pass by dropping
 # out of the depth-selected set above. Exclude source coast occlusion explicitly.
 yy,xx=np.indices(d.shape);hh,ww=d.shape;clip=np.stack(((xx+.5)/ww*2-1,1-(yy+.5)/hh*2,np.full(d.shape,-1.),np.ones(d.shape)),axis=-1);near=clip@M.T;near=near[...,:3]/near[...,3,None];clip[...,2]=1;far=clip@M.T;far=far[...,:3]/far[...,3,None];ray=far-near;Y=np.full(d.shape,float(np.median(corners[ids>=0])))
 for _ in range(32):
  t=(Y-near[...,1])/ray[...,1];Q=near+ray*t[...,None];Y,owner=surface(Q)
 qs,qa,qc=G.field_at(field,Q,res,ran);opaque=G.read_opaque(o/'opaque.bin',d.shape)
 selected=(Q[...,0]>1)&(Q[...,0]<w-1)&(Q[...,2]>1)&(Q[...,2]<h-1)&(owner>=0)&(abs(Q[...,1]-Y)<.0001)&(t>=0)&(t<=1)&qc&(qs>.4)&(qa>.999)&~opaque&~G.object_halo(n)
 # Native cliffs/objects, including boats over wet source ground, can stand in
 # front of the expected sea. Valid depth above it is foreground; background
 # holes and zero-normal floors at or below the plane still remain selected.
 foreground=valid&(P[...,1]>Y+.02);selected&=~foreground
 missing=int((selected&((d>=.999999)|np.all(n==0,axis=2))).sum());assert missing==0,(o,missing)
 return dict(capture=str(o),generated_bed_vertices=0,native_height_error=float(error[1]),gpu_water_pixels=count,wrong_depth_pixels=bad,depth_error_max=float(e[water].max()),projected_water_pixels=int(selected.sum()),background_holes=missing)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('captures',nargs='+',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();r=dict(passed=True,reports=[check(o)for o in a.captures]);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
