#!/usr/bin/env python3
"""Enhanced SAND: matching native TD template footprints, shared daylight geometry.

The native Dune-derived bank stays untouched. Enhanced borrows the source-led
Desert remaster geometry/material detail, with pale sand calibrated to the native
SAND CLEAR1 palette. Exact saved masks separate ground and rock; no runtime hue key.
"""
from pathlib import Path
import json,struct,hashlib,shutil,sys
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]

def pack(out):
    out.mkdir(parents=True,exist_ok=True);art=ROOT/'art/remaster/textures/DESERT'
    # Actual native CLEAR1 samples, not a guessed orange substitute.
    ex=ROOT/'tools/bakery/sharecopy/assets/extracted';pal=np.frombuffer((ex/'PA8/SAND.PA8').read_bytes(),'u1').reshape(-1,3)
    ids=np.frombuffer((ex/'DA8/SAND.DA8').read_bytes(),'u1')[:16*24*24];native=pal[ids].mean(0)/255
    source=ROOT/'game/clearblend_DESERT.pack';clear=bytearray(source.read_bytes());header=40 if struct.unpack_from('<I',clear,8)[0]==3 else 28
    c=np.frombuffer(clear,dtype='u1',offset=header);assert len(c)%3==0
    reference=c.reshape(-1,3).mean(0)/255;ratio=np.clip(native/reference,.6,2.5)
    c[:]=np.rint(np.clip(c.reshape(-1,3).astype(float)*ratio,0,255)).astype('u1').ravel()
    (out/'clearblend_SAND.pack').write_bytes(clear)
    # Neutral saved coverage and physical relief use the same DESERT engine keys.
    for a,b in [('groundmask_DESERT.pack','groundmask_SAND.pack'),('coastmask_DESERT.pack','coastmask_SAND.pack'),('terrainsurface_DESERT.pack','terrainsurface_SAND.pack')]:shutil.copy2(ROOT/'game'/a,out/b)
    m=(ROOT/'game/groundmask_DESERT.pack').read_bytes();_,cell,n=struct.unpack_from('<III',m,8);pixels=np.frombuffer(m,dtype='u1',offset=20+n*24).reshape(n,cell,cell,2)
    masks={}
    for i in range(n):
        tid,icon,v,game,alternate=struct.unpack_from('<HHBBH',m,20+i*24+16)
        if not game and not alternate:masks[(tid,icon,v)]=pixels[i,...,0].astype(float)/255
    meta=json.loads((art/'set_144.json').read_text());names={r['tid']:name for name,r in meta['templates'].items()}
    raw=bytearray((art/'terrainenh_DESERT.pack').read_bytes());_,cell,n=struct.unpack_from('<III',raw,8);start=20+n*4;tiles=np.frombuffer(raw,dtype='u1',offset=start).reshape(n,cell,cell,3)
    for i in range(n):
        tid,key=struct.unpack_from('<HH',raw,20+i*4);icon=key&255;v=(key>>8)&3;name=names[tid]
        cov=masks.get((tid,icon,v));a=tiles[i].astype(float)
        if name.startswith('CLEAR'):cov=np.ones((cell,cell))
        if cov is None:continue
        ground=np.clip(a*ratio,0,255);rock=np.clip(a*np.array([1.08,1.04,.96]),0,255)
        tiles[i]=np.rint(ground*cov[...,None]+rock*(1-cov[...,None])).astype('u1')
    (out/'terrainenh_SAND.pack').write_bytes(raw)
    raw=bytearray((art/'terrainseam_DESERT.pack').read_bytes());_,cell,margin,n=struct.unpack_from('<IIII',raw,8);start=24+n*8;tiles=np.frombuffer(raw,dtype='u1',offset=start).reshape(n,cell*margin,4)
    # Physical seam strips contain rock only after daylight compilation.
    tiles[...,:3]=np.rint(np.clip(tiles[...,:3].astype(float)*[1.08,1.04,.96],0,255)).astype('u1')
    (out/'terrainseam_SAND.pack').write_bytes(raw)
    receipt={'version':1,'native_clear_mean':native.tolist(),'enhanced_source_mean':reference.tolist(),'ground_rgb_gain':ratio.tolist(),'source_theater':'DESERT','runtime_theater':'SAND','native_bank_unchanged':True,'player_colour':'Explicit all-black masks paired with the shared source albedo/UV in unified_daylight_v01/MANIFEST.json','outputs':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*_SAND.pack')}}
    (out/'sand-daylight.json').write_text(json.dumps(receipt,indent=2)+'\n');print('SAND daylight:',n,'physical joins; native palette',native.tolist())
if __name__=='__main__':pack(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'game')
