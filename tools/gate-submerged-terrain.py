#!/usr/bin/env python3
"""Check actual bed supports and GPU water ownership; no RGB water classifier."""
from pathlib import Path
import argparse, importlib.util, json, re, struct
import numpy as np
spec=importlib.util.spec_from_file_location('ownership',Path(__file__).with_name('gate-water-normal-ownership.py'))
G=importlib.util.module_from_spec(spec);spec.loader.exec_module(G)

def lift(path):
    b=path.read_bytes();assert b[:8]==b'C3DLIFT1'
    version,w,h,steps,gw,gh,wet,*_=struct.unpack_from('<10I',b,8);assert version==1
    a=np.frombuffer(b,'<f4',offset=52,count=w*h).reshape(h,w)
    water=np.frombuffer(b,'u1',offset=52+w*h*4,count=w*h).reshape(h,w) if wet else np.zeros_like(a,dtype='u1')
    return a,water,steps

def snow_boundary(before,after):
    old,wet,steps=lift(before);new,wet2,steps2=lift(after)
    assert old.shape==new.shape and steps==steps2
    previous=[];current=[]
    for q,r,w in [(old,new,wet),(old.T,new.T,wet.T)]:
        for i in range(steps,len(q)-steps,steps):
            # Freeze the original exterior template foot, excluding wet supports.
            k=(q[i]==0)&((q[i-1]==0)^(q[i+1]==0))&~(w[i-1]|w[i]|w[i+1]).astype(bool)
            previous.extend(abs(q[i+1]-2*q[i]+q[i-1])[k]);current.extend(abs(r[i+1]-2*r[i]+r[i-1])[k])
    assert len(previous)>100
    a,b=np.array(previous),np.array(current)
    assert np.quantile(b,.99)<np.quantile(a,.99)*.5
    assert b.max()<a.max()*.5
    assert not np.any(new[wet2.astype(bool)])
    return dict(samples=len(a),before_peak=float(a.max()),after_peak=float(b.max()),before_q99=float(np.quantile(a,.99)),after_q99=float(np.quantile(b,.99)),wet_relief_max=float(new[wet2.astype(bool)].max(initial=0)))

def check(o):
    b=(o/'bed.bin').read_bytes();assert b[:8]==b'C3DBED1\0';n=struct.unpack_from('<I',b,8)[0]
    A=np.frombuffer(b,'<f4',offset=12).reshape(n,7);assert np.isfinite(A).all()
    x,z,y,level,cliff,distance,native=A.T
    dry=distance<=0;deep=distance>3.3
    joins=float(abs((y-native+.006)[dry]).max(initial=0));assert joins<2e-5
    flat=float(abs((y-level+3)[deep]).max(initial=0));assert flat<2e-5
    field,res,ran=G.read_field(o/'field16.bin');gh,gw=field.shape[0]//res,field.shape[1]//res
    spacing=np.diff(np.unique(x));steps=int(round(1/spacing[spacing>1e-6].min()));assert steps in (8,16,32)
    H=np.full((gh*steps+1,gw*steps+1),np.nan,dtype='f4');H[np.rint(z*steps).astype(int),np.rint(x*steps).astype(int)]=level
    depth,_=G.read_buffer(o/'capture','depth');normals,_=G.read_buffer(o/'capture','normals');depth=depth[...,0]
    M=np.array(json.loads((o/'capture-matrices.json').read_text())['inverse_vp']).reshape(4,4,order='F');P,valid=G.world_positions(depth,M)
    opaque=G.read_opaque(o/'opaque.bin',depth.shape)
    def lattice(values,Q,spacing=steps):
        ix=np.clip(np.floor(Q[...,0]*spacing).astype(int),0,gw*spacing-1);iz=np.clip(np.floor(Q[...,2]*spacing).astype(int),0,gh*spacing-1)
        fx=np.clip(Q[...,0]*spacing-ix,0,1);fz=np.clip(Q[...,2]*spacing-iz,0,1)
        a,b,c,d=values[iz,ix],values[iz,ix+1],values[iz+1,ix],values[iz+1,ix+1]
        return np.where(fx+fz<=1,a+(b-a)*fx+(c-a)*fz,d+(c-d)*(1-fx)+(b-d)*(1-fz))
    plane=o/'plane.bin';projection='legacy depth-point selection (no plane dump)'
    if plane.exists():
        raw=plane.read_bytes();assert raw[:8]==b'C3DPLN1\0';pw,ph=struct.unpack_from('<2I',raw,8);assert (pw,ph)==(gw,gh)
        corners=np.frombuffer(raw,'<f4',offset=16,count=gw*gh*4).reshape(gh,gw,4)
        body=np.frombuffer(raw,'<i4',offset=16+gw*gh*16,count=gw*gh).reshape(gh,gw)
        assert len(raw)==16+gw*gh*20 and np.any(body>=0)
        def surface_at(Q):
            ix=np.clip(np.floor(Q[...,0]).astype(int),0,gw-1);iz=np.clip(np.floor(Q[...,2]).astype(int),0,gh-1)
            fx=Q[...,0]-ix;fz=Q[...,2]-iz;a,b,c,d=np.moveaxis(corners[iz,ix],-1,0)
            return np.where(fx+fz<=1,a+(b-a)*fx+(c-a)*fz,d+(c-d)*(1-fx)+(b-d)*(1-fz)),body[iz,ix]
        yy,xx=np.indices(depth.shape);hh,ww=depth.shape
        clip=np.stack(((xx+.5)/ww*2-1,1-(yy+.5)/hh*2,np.full(depth.shape,-1.),np.ones(depth.shape)),axis=-1)
        near=clip@M.T;near=near[...,:3]/near[...,3,None];clip[...,2]=1;far=clip@M.T;far=far[...,:3]/far[...,3,None]
        ray=far-near;den=ray[...,1];assert np.all(abs(den)>1e-10)
        surface=np.full(depth.shape,float(np.median(corners[body>=0])))
        # Iterate the authored piecewise surface, independently of the captured
        # material/depth. Flat bodies converge immediately; SW-NE low-bank corners
        # converge to their actual ray intersection. Never infer coverage from RGB.
        for _ in range(24):
            t=(surface-near[...,1])/den;Q=near+ray*t[...,None];surface,ids=surface_at(Q)
        residual=abs(Q[...,1]-surface)
        inside=(Q[...,0]>=0)&(Q[...,0]<gw)&(Q[...,2]>=0)&(Q[...,2]<gh)&(ids>=0)&(t>=0)&(t<=1)&(residual<.0001)
        bed=np.full_like(H,np.nan);bed[np.rint(z*steps).astype(int),np.rint(x*steps).astype(int)]=y
        visible_bank=lattice(bed,Q)>surface+.002
        inside&=~visible_bank
        projection='camera rays intersect authoritative per-cell water surface; above-water bed occlusion excluded'
    else:
        Q=P;surface=lattice(H,Q);inside=np.ones(depth.shape,bool)
    sd,alpha,covered=G.field_at(field,Q,res,ran);inside&=covered
    log=(o/'run.log').read_text();rect=re.search(r'RA3D\|scene\|ground=red alert\|theater=\w+\|rect=(\d+),(\d+) (\d+)x(\d+)',log)
    if not rect:rect=re.search(r'MAP theater=\d+ origin=(\d+),(\d+) size=(\d+)x(\d+)',log)
    if rect:
        rx,rz,rw,rh=map(int,rect.groups());inside&=(Q[...,0]>=rx+1)&(Q[...,0]<rx+rw-1)&(Q[...,2]>=rz+1)&(Q[...,2]<rz+rh-1)
    # An opaque sprite or native cliff can stand in front of the projected sea.
    # Confirm its actual point is on the dry/partially covered native coast, or matches the native
    # relief. A submerged floor/missing water pixel cannot satisfy this test.
    actual_sd,actual_alpha,_=G.field_at(field,P,res,ran)
    native_grid=np.full_like(H,np.nan);native_grid[np.rint(z*steps).astype(int),np.rint(x*steps).astype(int)]=native
    native_at_point=lattice(native_grid,P)
    base_at_point=native_at_point.copy()
    if (o/'lift.bin').exists():
        relief,_,relief_steps=lift(o/'lift.bin');base_at_point-=lattice(relief,P,relief_steps)
    foreground=(P[...,1]>surface+.02)&(((actual_sd<.12)&(actual_alpha<.999))|((abs(native_at_point-P[...,1])<.01)|(abs(base_at_point-P[...,1])<.01))) if plane.exists() else np.zeros(depth.shape,bool)
    selected=valid&inside&(sd>.30)&(alpha>.999)&~opaque&~G.object_halo(normals)&~foreground&np.isfinite(surface)
    count=int(selected.sum());assert count>100
    error=abs(P[...,1]-surface);bad_depth=int((selected&(error>.02)).sum());missing=int((selected&~G.water_class(normals)).sum())
    bank_occlusion=0
    if plane.exists() and (bad_depth or missing):
        # A foreground bank can cover the ray before it reaches the water, even
        # when the bed at Q is submerged. March the authoritative bed lattice;
        # neither captured material nor captured depth decides this occlusion.
        suspect=selected&((error>.02)|~G.water_class(normals));coords=np.where(suspect)
        nr,rr,tt=near[coords],ray[coords],t[coords];yy=surface[coords]
        top=float(np.nanmax(bed))+.01;start=np.clip((top-nr[:,1])/rr[:,1],0,tt)
        span=np.max(abs(rr[:,[0,2]]*(tt-start)[:,None]),axis=1)
        samples=max(2,int(np.ceil(span.max()*steps*2))+1);blocked=np.zeros(len(tt),bool)
        for fraction in np.linspace(0,1,samples):
            u=start+(tt-start)*fraction;point=nr+rr*u[:,None]
            bh=lattice(bed,point);_,owner=surface_at(point)
            in_map=(point[:,0]>=0)&(point[:,0]<gw)&(point[:,2]>=0)&(point[:,2]<gh)
            blocked|=in_map&np.isfinite(bh)&(bh>point[:,1]+.0005)&(point[:,1]>yy+.002)
        bank_occlusion=int(blocked.sum());selected[coords[0][blocked],coords[1][blocked]]=False
        count=int(selected.sum());bad_depth=int((selected&(error>.02)).sum());missing=int((selected&~G.water_class(normals)).sum())
    assert not bad_depth and not missing,(o,bad_depth,missing)
    return dict(capture=str(o),bed_supports=n,bank_steps=steps,dry_attachment_supports=int(dry.sum()),dry_attachment_error=joins,deep_basin_supports=int(deep.sum()),deep_basin_error=flat,cliff_supports=int((cliff>.5).sum()),gpu_water_pixels=count,wrong_water_depth_pixels=bad_depth,missing_water_material_pixels=missing,water_y_error_q99=float(np.quantile(error[selected],.99)),surface_projection=projection,foreground_dry_source_pixels=int((foreground&inside).sum()),foreground_bank_occlusion_pixels=bank_occlusion,source_cliff_detail='WATER|bed-material|'in log)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('captures',type=Path,nargs='+');p.add_argument('--baseline-lift',type=Path);p.add_argument('--snow-lift',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();reports=[check(o)for o in a.captures];r=dict(passed=True,reports=reports)
    if a.baseline_lift:r['snow_boundary']=snow_boundary(a.baseline_lift,a.snow_lift)
    a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
