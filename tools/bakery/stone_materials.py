#!/usr/bin/env python3
"""Compile UV-paired mineral normals and specular roughness without changing art.

Run AFTER terrain_daylight and the Desert rock albedo rebake. --source is the
checkout owning the final saved art; --runtime holds its final game packs. The
output folder is staging only: this tool never installs or mutates its inputs.
Normals represent fine mineral relief, not broad painted shadows or the physical
terrain heightfield. All material channels are linear data, never sRGB.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import zlib

import numpy as np
from PIL import Image
from scipy import ndimage as nd

RECIPE='mineral-detail-normal-roughness-v01'
WEIGHTS=np.array([.2126,.7152,.0722],np.float32)
F0=.04

def sha(b):return hashlib.sha256(b).hexdigest()
def u32(b,p):return struct.unpack_from('<I',b,p)[0]
def load_module(path):
    spec=importlib.util.spec_from_file_location('stone_daylight',path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def select_banks(day,runtime,banks=None):
    """Validate an explicit destination contract; default to every source bank."""
    available={{'RATEMP':'RA_TEMPERAT','RASNOW':'RA_SNOW'}.get(bank,bank):bank
               for bank in {r['theater'] for r in day.manifest()['assets']}}
    if 'DESERT' in available:available['SAND']='DESERT'
    if banks is None:
        banks=[bank for bank in available if bank!='SAND' or (runtime/'terrainsurface_SAND.pack').is_file()]
    banks=list(banks)
    if not banks or len(banks)!=len(set(banks)):
        raise ValueError('Select a nonempty, duplicate-free material bank list')
    unknown=set(banks)-available.keys()
    if unknown:raise ValueError('Unknown material banks: '+', '.join(sorted(unknown)))
    if 'SAND' in banks and 'DESERT' not in banks:
        raise ValueError('SAND material requires the selected DESERT donor bank')
    for bank in banks:
        path=runtime/f'terrainsurface_{bank}.pack'
        if not path.is_file():raise FileNotFoundError('Bake current physical surfaces first: '+str(path))
    return {bank:available[bank] for bank in sorted(banks)}

def mineral(rgba,coverage=None):
    """Bounded UV-space microrelief and roughness from the paired mineral signal.

    Suppress broad (>6px) lighting and form, and nearest-extend only the explicit
    mineral region before filtering. The physical mesh retains all macro relief.
    Micro-normal slopes are limited to .55 (~29 degrees) to avoid noisy glitter.
    """
    rgb=np.asarray(rgba,dtype=np.float32)[...,:3]/255
    h,w=rgb.shape[:2]
    mask=np.ones((h,w),np.float32) if coverage is None else np.asarray(coverage,np.float32)/255
    valid=mask>.5
    y=rgb@WEIGHTS
    if valid.any():
        indices=nd.distance_transform_edt(~valid,return_distances=False,return_indices=True)
        y=y[indices[0],indices[1]]
    else:y=np.zeros_like(y)
    detail=nd.gaussian_filter(y,.65,mode='nearest')-nd.gaussian_filter(y,6,mode='nearest')
    dy,dx=np.gradient(detail)
    xy=np.stack((-dx,-dy),axis=-1)*5.0
    slope=np.linalg.norm(xy,axis=-1);xy*=np.minimum(1,.55/np.maximum(slope,1e-9))[...,None]
    normal=np.dstack((xy,np.ones((h,w),np.float32)))
    normal/=np.linalg.norm(normal,axis=-1,keepdims=True)
    grain=np.sqrt(np.maximum(nd.gaussian_filter(detail*detail,2),0))
    rough=np.clip(.80-.16*np.tanh(detail/.09)+.035*np.clip(grain/.08,0,1),.58,.90)
    normal[~valid]=[0,0,1];rough[~valid]=.82
    nr=np.uint8(np.rint(np.dstack((normal*.5+.5,rough))*255))
    spec=np.uint8(np.rint(np.dstack((np.full((h,w),F0),rough,np.zeros((h,w)),mask))*255))
    spec[...,1]=nr[...,3]  # preserve exact paired 8-bit roughness after quantization
    return nr,spec

def image(path,a,mode='RGBA'):
    path.parent.mkdir(parents=True,exist_ok=True);Image.fromarray(a,mode).save(path)
    return sha(path.read_bytes())

def pair(out,name,rgba,nr,spec,albedo_source,coverage=None):
    d=out/'maps'/name;d.mkdir(parents=True,exist_ok=True)
    rec=dict(recipe=RECIPE,dimensions=[rgba.shape[1],rgba.shape[0]],
             albedo=albedo_source,albedo_rgba_sha256=sha(rgba.tobytes()),
             normal=dict(file=str((d/'normal_roughness.png').relative_to(out)),
                         sha256=image(d/'normal_roughness.png',nr),channels='RGB tangent normal; A perceptual roughness'),
             specular=dict(file=str((d/'specular.png').relative_to(out)),
                           sha256=image(d/'specular.png',spec),channels='R dielectric F0=0.04; G perceptual roughness; B metallic=0; A explicit coverage'),
             player_colour_mask=dict(file=str((d/'player_colour_mask.png').relative_to(out)),
                                     sha256=image(d/'player_colour_mask.png',np.zeros(rgba.shape[:2],np.uint8),'L'),
                                     recolourable=False,reason='Natural mineral; all-black explicit mask paired to this UV layout.'),
             normal_slope_max=float(np.max(np.linalg.norm(nr[...,:2]/127.5-1,axis=-1))),
             roughness_range=[float(nr[...,3].min()/255),float(nr[...,3].max()/255)])
    return rec

def terrain(source,runtime,out,report,banks):
    day=load_module(source/'tools/bakery/terrain_daylight.py')
    # The manifest is intentionally dynamic. Added FORD/FALLS/BRIDGE keys must not
    # be omitted just because an earlier report happened to have 629 entries.
    allrows=day.manifest()['assets']
    for bank in sorted({r['theater'] for r in allrows}):
        rt={'RATEMP':'RA_TEMPERAT','RASNOW':'RA_SNOW'}.get(bank,bank)
        if rt not in banks:continue
        surface=runtime/f'terrainsurface_{rt}.pack'
        if not surface.exists():raise FileNotFoundError('Bake current physical surfaces first: '+str(surface))
        raw=surface.read_bytes();assert raw[:8]==b'C3DSURF\0'
        version,cell,count=struct.unpack_from('<III',raw,8);assert version==1
        keys=raw[20:20+24*count];maps={};rows=[]
        for r in day.rows(bank):
            material=day.surface_source(r)
            # Match the final compiler's colour operations, rather than deriving
            # normals from a pre-grade source that lost the new mineral signal.
            im=day.checked(material,'albedo.png','RGB')
            if r['name'].startswith('SH') and bank in ('TEMPERAT','RATEMP'):
                im=Image.open(source/r['source']).convert('RGB')
            elif bank in ('TEMPERAT','RATEMP'):
                im=day.temperate_stone(im,day.checked(material,'rock_mask.png','L'))
            elif bank=='DESERT':
                im=day.shared_stone(im,day.checked(material,'rock_mask.png','L')) if material is not r else day.desert_stone(r)[0]
            albedo=np.asarray(im.convert('RGBA'))
            # The true saved mineral mask is the only coverage classification.
            rock=np.asarray(day.checked(material,'rock_mask.png','L'))
            player=day.checked(material,'player_colour_mask.png','L');assert player.getextrema()==(0,0)
            nr,sp=mineral(albedo,rock)
            rec=pair(out,rt+'/'+r['name']+'_v'+str(r['variant']),albedo,nr,sp,
                     dict(file=str((day.SOURCE/material['export']/'albedo.png').relative_to(source)),
                          sha256=material['albedo_sha256'],delivered_rgba_sha256=sha(albedo.tobytes()),player_colour_mask_sha256=material['player_colour_mask_sha256'],
                          rock_mask_sha256=material['rock_mask_sha256']))
            # Terrain packs omit the redundant normal Z; reconstruct it in shader.
            packed=np.dstack((nr[...,:2],nr[...,3],rock));w,h=r['cells']
            for icon in r['have']:
                z,x=divmod(icon,w)
                key=r['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',r['tid'],icon,r['variant'],int(bank.startswith('RA')),0)
                maps[key]=packed[z*144:(z+1)*144,x*144:(x+1)*144].tobytes()
            rows.append(rec)
        ordered=[keys[i*24:(i+1)*24] for i in range(count)]
        if set(ordered)!=set(maps):raise ValueError('Physical/material key mismatch in '+bank)
        pixels=b''.join(maps[k] for k in ordered);z=zlib.compress(pixels,9)
        dest=out/f'terrainmaterial_{rt}.pack';dest.write_bytes(b'C3DMATL\0'+struct.pack('<III',1,144,count)+keys+struct.pack('<I',len(z))+z)
        report['terrain'][rt]=dict(tiles=count,source_surface_sha256=sha(raw),key_sha256=sha(keys),
                                    output_sha256=sha(dest.read_bytes()),templates=rows)

def stream(raw,p):
    start=p;n=u32(raw,p);p+=4;levels=[]
    for lv in range(n):
        w,h,rsize,csize=struct.unpack_from('<4I',raw,p);p+=16
        data=zlib.decompress(raw[p:p+csize]);p+=csize;assert len(data)==rsize==w*h*4
        levels.append(np.frombuffer(data,np.uint8).reshape(h,w,4))
    return start,p,levels

def normal_blob(nr):
    pieces=[];levels=[];cur=nr
    while True:
        h,w=cur.shape[:2];z=zlib.compress(cur.tobytes(),9);levels.append(struct.pack('<4I',w,h,w*h*4,len(z))+z)
        if w==h==1:break
        nw,nh=max(w//2,1),max(h//2,1)
        small=np.asarray(Image.fromarray(cur,'RGBA').resize((nw,nh),Image.Resampling.BOX)).copy()
        n=small[...,:3].astype(np.float32)/127.5-1;n/=np.maximum(np.linalg.norm(n,axis=-1,keepdims=True),1e-7)
        small[...,:3]=np.uint8(np.rint((n*.5+.5)*255));cur=small
    return struct.pack('<I',len(levels))+b''.join(levels)

def scatter(path,out,report):
    raw=path.read_bytes();assert raw[:8]==b'TREE3D4\0'
    ver,nm,ns=struct.unpack_from('<3I',raw,8);assert ver==4
    p=20
    for i in range(nm):
        nv,nt,tex=struct.unpack_from('<3I',raw,p);p+=20+nv*48+nt*12
        bones,frames=struct.unpack_from('<2I',raw,p);p+=8
        if bones or frames:raise ValueError('Rock pack unexpectedly animated')
    count=u32(raw,p);p+=4+count*12
    prefix=raw[:p];parts=[prefix];pairs=[];unchanged=[]
    for i in range(ns):
        head=raw[p:p+16];p+=16;parts.append(head);blobs=[];levels=[]
        for k in range(4):
            a,p,lev=stream(raw,p);blobs.append(raw[a:p]);levels.append(lev)
        albedo=levels[2][0];nr,sp=mineral(albedo)
        pairs.append(pair(out,path.stem+'/set_'+str(i),albedo,nr,sp,dict(pack=path.name,set=i,stream=2)))
        keep=blobs[0]+blobs[1]+blobs[2];unchanged.append(sha(keep))
        blobs[3]=normal_blob(nr);parts.extend(blobs)
    assert p==len(raw)
    result=b''.join(parts);dest=out/path.name;dest.write_bytes(result);assert result[:len(prefix)]==prefix
    report['scatter'][path.name]=dict(models=nm,sets=ns,source_sha256=sha(raw),output_sha256=sha(result),
                                     geometry_uv_physics_sha256=sha(prefix),geometry_uv_bytes_unchanged=True,
                                     unchanged_albedo_stream_sha256=unchanged,textures=pairs)

def placed(path,out,records,report):
    raw=path.read_bytes();magic,version,code,nt,ns,ne,np_=struct.unpack_from('<4sI8sIIII',raw)
    assert magic==b'EBLD' and version in (5,6)
    p=32+np_*4;pairs=[];specs=[]
    for i in range(nt):
        w,h,grid,frames,fps,role,house,fpsdiv,flags=struct.unpack_from('<HHBBBBBBBx',raw,p);p+=12
        n=u32(raw,p);p+=4;data=zlib.decompress(raw[p:p+n]);p+=n
        if role!=0:continue
        assert house==0,'Natural mineral unexpectedly carries player paint'
        rgba=np.frombuffer(data[:w*h*4],np.uint8).reshape(h,w,4);nr,sp=mineral(rgba)
        pairs.append(pair(out,path.stem+'/tex_'+str(i),rgba,nr,sp,dict(pack='ebld/'+path.name,texture=i)))
        crc=zlib.crc32(rgba.tobytes());key=(w,h,crc)
        if key in records:assert records[key]==nr.tobytes(),'Ambiguous albedo/normal pairing'
        records[key]=nr.tobytes()
        # EVSP uses reflectance strength and normalized Blinn gloss. Convert the
        # same roughness used by GGX, retaining broad dry stone highlights.
        rough=nr[...,3].astype(np.float32)/255
        exponent=np.maximum(2/(rough**4)-2,8)
        gloss=np.clip((np.log2(exponent)-3)/6,0,1)
        rg=np.uint8(np.rint(np.dstack((np.full((h,w),F0),gloss))*255))
        z=zlib.compress(rg.tobytes(),9);specs.append(struct.pack('<6I',i,w,h,255,0,len(z))+z)
    dest=out/'ebld'/(path.stem+'.spec');dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_bytes(b'EVSP'+struct.pack('<II',2,len(specs))+b''.join(specs))
    report['placed'][path.name]=dict(source_sha256=sha(raw),geometry_uv_albedo_bytes_unchanged=True,
                                    specular_sha256=sha(dest.read_bytes()),textures=pairs)

def build(args):
    source=args.source.resolve();runtime=args.runtime.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    if out==runtime or runtime in out.parents:raise ValueError('Output must be separate staging; do not overwrite runtime inputs')
    day=load_module(source/'tools/bakery/terrain_daylight.py')
    banks=select_banks(day,runtime,getattr(args,'banks',None))
    report=dict(recipe=RECIPE,colour_space='linear data; normals RGB tangent, roughness perceptual; natural mineral F0=.04 and metallic=0',
                source_root=str(source),runtime_root=str(runtime),selected_terrain_banks=list(banks),terrain={},scatter={},placed={})
    terrain(source,runtime,out,report,banks)
    for path in sorted(runtime.glob('rock3d*.pack')):scatter(path,out,report)
    # SAND uses the same physical Desert mineral layout; its later global tint
    # changes neither tangent detail nor material roughness. Keep native keys.
    if 'SAND' in banks:
        desert=out/'terrainmaterial_DESERT.pack';sand=out/'terrainmaterial_SAND.pack'
        keys=(runtime/'terrainsurface_SAND.pack').read_bytes()
        n=u32(keys,16);assert keys[20:20+24*n]==desert.read_bytes()[20:20+24*n]
        sand.write_bytes(desert.read_bytes())
        report['terrain']['SAND']=dict(shared_material='DESERT',tiles=n,
            source_surface_sha256=sha(keys),output_sha256=sha(sand.read_bytes()),
            key_sha256=sha(keys[20:20+24*n]),templates=[])
    records={}
    for path in sorted((runtime/'ebld').glob('ROCK*.ebld')):placed(path,out,records,report)
    index=[];payload=[];offset=16+20*len(records)
    for (w,h,crc),data in sorted(records.items()):
        z=zlib.compress(data,9);index.append(struct.pack('<5I',w,h,crc,offset,len(z)));payload.append(z);offset+=len(z)
    (out/'stonematerials.pack').write_bytes(b'C3DSTNM\0'+struct.pack('<II',1,len(index))+b''.join(index)+b''.join(payload))
    report['normal_pack']=dict(textures=len(index),sha256=sha((out/'stonematerials.pack').read_bytes()))
    (out/'MANIFEST.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(terrain_tiles={k:v['tiles'] for k,v in report['terrain'].items()},scatter_packs=len(report['scatter']),placed_models=len(report['placed']),uv_paired_textures=len(index))))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--banks',nargs='+',help='Required runtime bank names; default all source banks and available SAND')
    build(p.parse_args())
