#!/usr/bin/env python3
"""Verify staged mineral data, exact UV/albedo pairings and sun-dependent response.

Reads source/runtime/staging only. Optional --render-prefix measures actual hidden
renderer captures PREFIX-a.png, PREFIX-b.png, PREFIX-dark.png, PREFIX-off.png.
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

def sha(b):return hashlib.sha256(b).hexdigest()
def u32(b,p):return struct.unpack_from('<I',b,p)[0]
def load(path):
 s=importlib.util.spec_from_file_location('material_gate_baker',path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def run(args):
 stage=args.stage;runtime=args.runtime
 manifest=json.loads((stage/'MANIFEST.json').read_text());baker=load(Path(__file__).resolve().parent/'bakery/stone_materials.py')
 pairs=[]
 for row in manifest['terrain'].values():pairs.extend(row['templates'])
 for group in ('scatter','placed'):
  for row in manifest[group].values():pairs.extend(row['textures'])
 nonflat=0;maximum_unit_error=0;max_slope=0;rough_min=1;rough_max=0
 for pair in pairs:
  w,h=pair['dimensions'];nr_path=stage/pair['normal']['file'];sp_path=stage/pair['specular']['file'];mask_path=stage/pair['player_colour_mask']['file']
  for path,key in [(nr_path,'normal'),(sp_path,'specular'),(mask_path,'player_colour_mask')]:assert sha(path.read_bytes())==pair[key]['sha256'],path
  nr=np.asarray(Image.open(nr_path));sp=np.asarray(Image.open(sp_path));pm=np.asarray(Image.open(mask_path))
  assert nr.shape==sp.shape==(h,w,4) and pm.shape==(h,w) and not pm.any()
  # Explicit non-recolourable previews for red and blue are identical to their
  # paired source under the specified zero mask, independently of diffuse hue.
  for colour in ([1,0,0],[0,0,1]):assert not (pm[...,None]*colour).any()
  n=nr[...,:3]/127.5-1;error=float(np.abs(np.linalg.norm(n,axis=-1)-1).max());maximum_unit_error=max(maximum_unit_error,error);assert error<.012
  max_slope=max(max_slope,float((np.linalg.norm(n[...,:2],axis=-1)/np.maximum(n[...,2],1e-7)).max()))
  assert nr[...,3].min()>=147 and nr[...,3].max()<=230
  rough_min=min(rough_min,float(nr[...,3].min()/255));rough_max=max(rough_max,float(nr[...,3].max()/255))
  assert np.all(sp[...,0]==10) and not sp[...,2].any(),'Natural stone F0/metallicity changed'
  assert np.array_equal(nr[...,3],sp[...,1]),'Normal roughness and specular map diverged'
  nonflat+=int(np.count_nonzero(np.linalg.norm(n[...,:2],axis=-1)>.035))
 assert nonflat>10000 and max_slope<.57
 terrain={}
 for bank,row in manifest['terrain'].items():
  material=(stage/f'terrainmaterial_{bank}.pack').read_bytes();surface=(runtime/f'terrainsurface_{bank}.pack').read_bytes()
  assert sha(material)==row['output_sha256'] and sha(surface)==row['source_surface_sha256']
  assert material[:8]==b'C3DMATL\0' and u32(material,8)==1 and u32(material,12)==144
  n=u32(material,16);head=20+24*n;assert n==u32(surface,16) and material[20:head]==surface[20:head]
  assert sha(material[20:head])==row['key_sha256'] and head+4+u32(material,head)==len(material)
  data=zlib.decompress(material[head+4:]);assert len(data)==n*144*144*4
  terrain[bank]=n
 scatter={}
 for name,row in manifest['scatter'].items():
  before=(runtime/name).read_bytes();after=(stage/name).read_bytes();assert sha(before)==row['source_sha256'] and sha(after)==row['output_sha256']
  p=20;nm=u32(before,12);ns=u32(before,16)
  for _ in range(nm):
   nv,nt,_=struct.unpack_from('<III',before,p);p+=20+nv*48+nt*12;b,f=struct.unpack_from('<II',before,p);p+=8;assert b==f==0
  p+=4+u32(before,p)*12;assert before[:p]==after[:p] and sha(before[:p])==row['geometry_uv_physics_sha256']
  q=p
  for i in range(ns):
   assert before[p:p+16]==after[q:q+16];p+=16;q+=16
   for stream in range(4):
    a,p,_=baker.stream(before,p);b,q,levels=baker.stream(after,q)
    if stream!=3:assert before[a:p]==after[b:q],(name,i,stream,'non-normal stream changed')
    else:assert np.array_equal(levels[0],np.asarray(Image.open(stage/row['textures'][i]['normal']['file'])))
  assert p==len(before) and q==len(after);scatter[name]=ns
 normal=(stage/'stonematerials.pack').read_bytes();assert normal[:8]==b'C3DSTNM\0' and u32(normal,8)==1
 assert sha(normal)==manifest['normal_pack']['sha256'];records={}
 for i in range(u32(normal,12)):
  w,h,crc,off,size=struct.unpack_from('<5I',normal,16+i*20);nr=zlib.decompress(normal[off:off+size]);assert len(nr)==w*h*4;records[(w,h,crc)]=nr
 placed=0
 for name,row in manifest['placed'].items():
  raw=(runtime/'ebld'/name).read_bytes();assert sha(raw)==row['source_sha256'];nt=u32(raw,16);p=32+u32(raw,28)*4
  for i in range(nt):
   w,h,_,_,_,role,house,_,_=struct.unpack_from('<HHBBBBBBBx',raw,p);p+=12;c=u32(raw,p);p+=4;pixels=zlib.decompress(raw[p:p+c]);p+=c
   if role!=0:continue
   key=(w,h,zlib.crc32(pixels[:w*h*4]));assert key in records
   pair=next(v for v in row['textures'] if v['albedo']['texture']==i)
   assert records[key]==np.asarray(Image.open(stage/pair['normal']['file'])).tobytes()
  sp=(stage/'ebld'/(Path(name).stem+'.spec')).read_bytes();assert sha(sp)==row['specular_sha256'] and sp[:4]==b'EVSP' and u32(sp,4)==2
  placed+=1
 # The actual BRDF contract: at zero sun energy there is no directional glint;
 # opposed sun bearings move both NoL and the specular lobe on nonplanar normals.
 n=np.asarray(Image.open(stage/pairs[0]['normal']['file']))[...,:3]/127.5-1;n/=np.maximum(np.linalg.norm(n,axis=-1,keepdims=True),1e-7)
 # Synthetic world-space test normals cover vertical/upward facets, not a flat
 # swatch that cannot distinguish changing azimuth.
 normals=np.array([[0,1,0],[.8,.6,0],[-.8,.6,0],[0,.6,.8],[0,.6,-.8]])
 V=np.array([0,.7,1.]);V/=np.linalg.norm(V)
 def ggx(L,energy):
  L=np.asarray(L);L/=np.linalg.norm(L);H=L+V;H/=np.linalg.norm(H)
  nl=np.maximum(normals@L,0);nv=np.maximum(normals@V,.001);nh=np.maximum(normals@H,0);vh=max(V@H,0);rough=.78;a2=rough**4
  D=a2/(np.pi*(nh*nh*(a2-1)+1)**2);k=(rough+1)**2/8;G=nl/(nl*(1-k)+k)*nv/(nv*(1-k)+k);F=.04+.96*(1-vh)**5
  return energy*F*D*G*nl/np.maximum(4*nl*nv,.001)
 a=ggx([.8,.5,.2],1);b=ggx([-.8,.5,-.2],1);assert np.max(np.abs(a-b))>.001 and np.max(ggx([.8,.5,.2],0))==0
 result=dict(passed=True,terrain_cells=terrain,scatter_sets=scatter,placed_models=placed,uv_paired_textures=len(records),
             explicit_black_player_masks=len(pairs),normal_nonflat_texels=nonflat,normal_unit_error_max=maximum_unit_error,
             tangent_slope_max=max_slope,roughness_range=[rough_min,rough_max],dielectric_F0=.04,metallic=0,
             geometry_uv_albedo_bytes_preserved=True,opposed_sun_specular_delta=float(np.max(np.abs(a-b))),zero_sun_specular=0)
 if args.render_prefix:
  pref=str(args.render_prefix);ra=np.asarray(Image.open(pref+'-a.png')).astype(int);rb=np.asarray(Image.open(pref+'-b.png')).astype(int);dark=np.asarray(Image.open(pref+'-dark.png')).astype(int)
  # Restrict to the observed cliff crop of this fixture, excluding HUD and fog.
  sl=(slice(32,130),slice(330,750),slice(0,3));delta=np.abs(ra[sl]-rb[sl]);assert np.count_nonzero(np.any(delta>2,axis=-1))>1000
  result['hidden_renderer']=dict(sun_azimuths=[65,245],changed_cliff_pixels=int(np.count_nonzero(np.any(delta>2,axis=-1))),
      opposed_sun_mean_delta=float(delta.mean()),zero_energy_cliff_max=int(dark[sl].max()),
      captures={suffix:sha(Path(pref+'-'+suffix+'.png').read_bytes()) for suffix in ['a','b','dark']})
  off=Path(pref+'-off.png')
  if off.exists():
   ro=np.asarray(Image.open(off)).astype(int);delta=np.abs(ra[sl]-ro[sl]);changed=int(np.count_nonzero(np.any(delta>1,axis=-1)));assert changed>100
   result['hidden_renderer']['mapped_vs_unmapped_cliff_pixels']=changed
 if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--stage',type=Path,required=True);p.add_argument('--render-prefix',type=Path);p.add_argument('--output',type=Path);run(p.parse_args())
