#!/usr/bin/env python3
"""Quilt native-resolution mineral detail from the complete shared cliff sources.

The former 98x175 crop was stretched over 2.15 world cells and repeated on every
bank. This material retains the cliffs' 144 texels/cell density. Minimum-error
seams preserve mineral contours; periodic-plus-smooth decomposition removes only
the outer boundary mismatch. There are no painted lighting terms or new meshes.
"""
from pathlib import Path
import argparse, hashlib, json
import numpy as np
from PIL import Image
from scipy import ndimage as nd

LUMA=np.array([.2126,.7152,.0722])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def cut(cost):
    score=cost.copy();parent=np.zeros(cost.shape,np.int16)
    for y in range(1,len(cost)):
        prev=np.pad(score[y-1],1,constant_values=np.inf)
        choices=np.stack([prev[:-2],prev[1:-1],prev[2:]])
        step=choices.argmin(0)-1;parent[y]=step
        score[y]+=choices.min(0)
    path=np.empty(len(cost),int);path[-1]=score[-1].argmin()
    for y in range(len(cost)-1,0,-1):path[y-1]=path[y]+parent[y,path[y]]
    return path
def periodic(a):
    h,w=a.shape;b=np.zeros_like(a)
    b[0]=a[-1]-a[0];b[-1]=a[0]-a[-1]
    b[:,0]+=a[:,-1]-a[:,0];b[:,-1]+=a[:,0]-a[:,-1]
    denom=2*np.cos(2*np.pi*np.arange(h)/h)[:,None]+2*np.cos(2*np.pi*np.arange(w)/w)[None,:]-4
    denom[0,0]=1;s=np.fft.fft2(b)/denom;s[0,0]=0
    return a-np.fft.ifft2(s).real
def build(root,out,size=1024):
    d=root/'art/remaster/textures/unified_daylight_v01';m=json.loads((d/'MANIFEST.json').read_text())
    pool=[];sources=[];P=96;overlap=24;step=P-overlap;rng=np.random.default_rng(20261021)
    for q in m['assets']:
        if q['theater']!='TEMPERAT' or not q['name'][1:].isdigit() or not q['name'].startswith('S'):continue
        p=d/q['export'];rgb=np.asarray(Image.open(p/'albedo.png').convert('RGB'),float)/255
        mask=np.asarray(Image.open(p/'rock_mask.png'))>127;y=rgb@LUMA
        low=nd.gaussian_filter(y,18);broad=nd.gaussian_filter(y,50)
        neutral=np.clip(.50+(y-low)*.95+(low-broad)*.25,.24,.76)
        valid=nd.minimum_filter(mask.astype('u1'),size=P,mode='constant')>0
        yy,xx=np.where(valid);keep=rng.permutation(len(xx))[:12]
        for k in keep:
            x,z=xx[k]-P//2,yy[k]-P//2;a=neutral[z:z+P,x:x+P]
            if a.shape==(P,P):pool.append(a);sources.append(dict(albedo=str((p/'albedo.png').relative_to(root)),albedo_sha256=sha(p/'albedo.png'),rock_mask_sha256=sha(p/'rock_mask.png'),crop_xyxy=[int(x),int(z),int(x+P),int(z+P)]))
    assert len(pool)>32,'Insufficient complete mineral source patches'
    pool=np.array(pool);side=int(np.ceil((size-P)/step))*step+P;canvas=np.zeros((side,side));used=[]
    for z in range(0,side-P+1,step):
        for x in range(0,side-P+1,step):
            ids=rng.choice(len(pool),min(64,len(pool)),replace=False);cost=np.zeros(len(ids))
            if x:cost+=((pool[ids,:,:overlap]-canvas[z:z+P,x:x+overlap])**2).mean((1,2))
            if z:cost+=((pool[ids,:overlap,:]-canvas[z:z+overlap,x:x+P])**2).mean((1,2))
            choices=np.argsort(cost)[:4];i=int(ids[rng.choice(choices)]);a=pool[i].copy();old=canvas[z:z+P,x:x+P];take=np.ones((P,P),bool)
            if x:
                path=cut((a[:,:overlap]-old[:,:overlap])**2)
                take[:,:overlap]&=np.arange(overlap)[None,:]>=path[:,None]
            if z:
                path=cut(((a[:overlap]-old[:overlap])**2).T)
                take[:overlap]&=np.arange(overlap)[:,None]>=path[None,:]
            canvas[z:z+P,x:x+P]=np.where(take,a,old);used.append(i)
    value=np.clip(periodic(canvas[:size,:size]),.22,.78);rgb=np.clip(value[...,None]*[1.16,1.015,.80],0,1)
    out.mkdir(parents=True,exist_ok=True);Image.fromarray(np.uint8(np.rint(rgb*255))).save(out/'material.png');Image.new('L',(size,size),0).save(out/'material_player_mask.png')
    report=dict(version=5,texels_per_world_cell=144,dimensions=[size,size],patch=96,overlap=24,patch_count=len(pool),source_patches=sources,used_patch_indices=used,material_sha256=sha(out/'material.png'),player_mask_sha256=sha(out/'material_player_mask.png'),recipe='Native-resolution shared cliff mineral crops; minimum-error quilt seams; periodic-plus-smooth outer boundary; neutral warm sandstone. No directional shading, generated upscaling or new geometry.')
    (out/'source.json').write_text(json.dumps(report,indent=2)+'\n');print(len(pool),'source mineral patches;',report['material_sha256'])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--size',type=int,default=1024);a=p.parse_args();build(a.source,a.output,a.size)
