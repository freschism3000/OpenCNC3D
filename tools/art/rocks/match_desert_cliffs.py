#!/usr/bin/env python3
"""Rebake the existing rock UVs with the approved cliff material; never change a mesh.

Both rock systems keep their binary geometry, UVs, normals, physics, mapping and
animation bytes. Only opaque albedo streams change. Desert overrides leave other
biomes' placed rocks untouched. No directional light or cast shade is baked.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib
import numpy as np
from PIL import Image
from scipy import ndimage as nd


def sha(b): return hashlib.sha256(b).hexdigest()


def module(path, name):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    return mod


def albedo(p,n,seed,daylight,luma=.44):
    # Triplanar world-position sampling in cells. The normal only chooses the
    # projection; it never darkens a face or assumes a sun direction.
    w=np.abs(n)**4;w/=np.maximum(w.sum(1,keepdims=True),1e-9)
    result=np.zeros_like(p)
    for axis,(a,b) in enumerate(((2,1),(0,2),(0,1))):
        tex,mean=daylight.desert_sample(p[:,a]*144,p[:,b]*144,seed)
        # Unlike a cliff face, these atlases have no separate source mineral
        # luminance to retain. Keep the material's full fine-grain contrast;
        # chroma-only sampling made small stones look like flat brown polygons.
        result+=w[:,axis,None]*(luma*tex/mean)
    return np.uint8(np.rint(np.clip(result,0,1)*255))


def raster(P,N,U,T,size,seed,daylight,luma=.44):
    width,height=size
    pixels=np.zeros((height,width,4),np.uint8);pixels[...,3]=255
    covered=np.zeros((height,width),bool)
    for t in T:
        uv=U[t]*[width,height];pos=P[t];norm=N[t]
        lo=np.maximum(np.floor(uv.min(0)).astype(int),0)
        hi=np.minimum(np.ceil(uv.max(0)).astype(int),[width,height])
        if np.any(hi<=lo):continue
        yy,xx=np.mgrid[lo[1]:hi[1],lo[0]:hi[0]]
        x=xx.ravel()+.5;y=yy.ravel()+.5
        a,b,c=uv;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-10:continue
        u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den
        v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den
        weights=np.c_[u,v,1-u-v];keep=np.all(weights>=-1e-6,axis=1)
        xp=xx.ravel()[keep];yp=yy.ravel()[keep];weights=weights[keep]
        pixels[yp,xp,:3]=albedo(weights@pos,weights@norm,seed,daylight,luma)
        covered[yp,xp]=True
    assert covered.any(),'UV atlas has no covered texels'
    nearest=nd.distance_transform_edt(~covered,return_distances=False,return_indices=True)
    pixels[...,:3]=pixels[nearest[0],nearest[1],:3]
    return pixels


def save_pair(out,name,image):
    Image.fromarray(image,'RGBA').save(out/(name+'.png'))
    Image.new('L',(image.shape[1],image.shape[0]),0).save(out/(name+'_player_mask.png'))
    return dict(albedo=name+'.png',player_colour_mask=name+'_player_mask.png',
                albedo_sha256=sha((out/(name+'.png')).read_bytes()),
                mask_sha256=sha((out/(name+'_player_mask.png')).read_bytes()),
                recolourable=False,dimensions=[image.shape[1],image.shape[0]])


def scatter(raw,out,bt,daylight):
    assert raw[:8]==b'TREE3D4\0'
    version,nm,ns=struct.unpack_from('<3I',raw,8);assert version==4 and nm==ns==45
    p=20;models=[]
    for i in range(nm):
        nv,nt,tex=struct.unpack_from('<3I',raw,p);p+=20
        v=np.frombuffer(raw,dtype='<f4',count=nv*12,offset=p).reshape(nv,12);p+=nv*48
        tris=np.frombuffer(raw,dtype='<u4',count=nt*3,offset=p).reshape(nt,3);p+=nt*12
        bones,frames=struct.unpack_from('<2I',raw,p);p+=8
        assert bones==frames==0
        models.append((v,tris,tex))
    count=struct.unpack_from('<I',raw,p)[0];p+=4+count*12
    prefix=raw[:p];pieces=[prefix];pairs=[]
    for i in range(ns):
        head=raw[p:p+16];aw,ah,bw,bh=struct.unpack('<4I',head);p+=16;pieces.append(head)
        blobs=[]
        for k in range(4):
            start=p;levels=struct.unpack_from('<I',raw,p)[0];p+=4
            for lv in range(levels):
                w,h,rsize,csize=struct.unpack_from('<4I',raw,p);p+=16+csize
            blobs.append(raw[start:p])
        v,t,tex=models[i];assert tex==i
        ref=.135 if i//3>=10 else .30
        image=raster(v[:,:3]*ref,v[:,3:6],v[:,6:8],t,(bw,bh),201+i//3,
                     daylight,luma=.44*(.96,1.,1.04)[i%3])
        blobs[2]=bt._sheet_blob(image,False)
        pieces.extend(blobs);pairs.append(save_pair(out,f'RK{i//3+1:02d}{"ABC"[i%3]}',image))
    assert p==len(raw)
    result=b''.join(pieces);assert result[:len(prefix)]==prefix
    (out/'rock3d_DESERT.pack').write_bytes(result)
    return dict(models=nm,triangles=sum(len(t) for v,t,s in models),
                geometry_uv_mapping_sha256=sha(prefix),geometry_uv_bytes_unchanged=True,
                source_sha256=sha(raw),output_sha256=sha(result),textures=pairs)


def placed(raw,name,out,eb,daylight):
    h=eb.HEADER.unpack_from(raw);magic,version,code,nt,ns,ne,np_=h
    assert magic==b'EBLD' and version==6 and ns==1
    p=eb.HEADER.size+np_*4;prefix=raw[:p];textures=[]
    for i in range(nt):
        head=raw[p:p+eb.TEXHEAD5.size];p+=eb.TEXHEAD5.size
        w,hh,grid,frames,fps,role,house,fpsdiv,flags=eb.TEXHEAD5.unpack(head)
        csize=struct.unpack_from('<I',raw,p)[0];p+=4
        blob=raw[p:p+csize];p+=csize
        textures.append((head,blob,w,hh,role,house))
    rest=raw[p:];q=p;npiece,ntri=eb.SETHEAD.unpack_from(raw,q);q+=8
    for k in range(npiece):
        piece=eb.PIECE.unpack_from(raw,q);q+=eb.PIECE.size+piece[-1]*12
    tri=np.frombuffer(raw,dtype=eb.TRI,count=ntri,offset=q)
    pairs=[];parts=[prefix]
    for i,(head,blob,w,hh,role,house) in enumerate(textures):
        if role==0:
            assert house==0, 'Natural stone must have no player paint'
            subset=tri[tri['tex']==i]['v'];P=subset['p'].reshape(-1,3)/1024
            N=subset['n'].reshape(-1,3);U=subset['uv'].reshape(-1,2)
            T=np.arange(len(P)).reshape(-1,3)
            image=raster(P,N,U,T,(w,hh),40+int(name[-1]),daylight)
            old=np.frombuffer(zlib.decompress(blob),np.uint8).reshape(hh,w,4)
            image[...,3]=old[...,3]
            blob=zlib.compress(image.tobytes(),9)
            pairs.append(save_pair(out,name+'_DES_'+str(i),image))
        parts.extend((head,struct.pack('<I',len(blob)),blob))
    parts.append(rest);result=b''.join(parts)
    assert result.endswith(rest) and result[:len(prefix)]==prefix
    dest=out/(name+'_DES.ebld');dest.write_bytes(result);checked=eb.check(str(dest),quiet=True)
    return dict(source_sha256=sha(raw),output_sha256=sha(result),
                geometry_uv_physics_sha256=sha(prefix+rest),geometry_uv_bytes_unchanged=True,
                triangles=ntri,textures=pairs)


def build(root,source,out,material_project=None):
    out.mkdir(parents=True,exist_ok=True)
    daylight=module((material_project or root)/'tools/bakery/terrain_daylight.py','rock_daylight')
    bt=module(root/'game/bake_tree3d.py','rock_pack_writer')
    eb=module(root/'tools/art/remaster/bake_ebld.py','rock_ebld_reader')
    report=dict(material_sha256=daylight.DESERT_MATERIAL_SHA,
                contract='Existing meshes/UVs preserved; shared approved cliff albedo, no baked directional shade; explicit black player masks.',
                scatter=scatter(source.read_bytes(),out,bt,daylight),placed={})
    for i in range(1,8):
        name='ROCK'+str(i)
        report['placed'][name]=placed((root/'game/ebld'/(name+'.ebld')).read_bytes(),name,out,eb,daylight)
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Matched 45 scattered looks and all 7 placed Desert rocks; geometry and UV bytes preserved')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[3])
    p.add_argument('--scatter',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--material-project',type=Path)
    a=p.parse_args();build(a.source,a.scatter,a.output,a.material_project)
