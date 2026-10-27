#!/usr/bin/env python3
"""Check installed saved bank cuts, complete bush retirement and material pairings."""
import argparse, hashlib, json, struct
from pathlib import Path
import numpy as np
from PIL import Image

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def masks(path,channels,magic):
 b=path.read_bytes();assert b[:8]==magic
 ver,c,n=struct.unpack_from('<III',b,8);assert ver==1
 start=20+n*24;stride=c*c*channels;assert len(b)==start+n*stride
 return c,{struct.unpack_from('<HHBBH',b,20+i*24+16):np.frombuffer(b,'u1',stride,start+i*stride).reshape(c,c,channels) for i in range(n)}
def check(source,runtime,native_source=None):
 native_source=native_source or source
 d=source/'art/remaster/textures/unified_daylight_v01';m=json.loads((d/'MANIFEST.json').read_text())
 _,ground=masks(runtime/'groundmask_DESERT.pack',2,b'C3DGRAS\0');_,coast=masks(runtime/'coastmask_DESERT.pack',1,b'C3DCOAS\0');hc,relief=masks(runtime/'terrainsurface_DESERT.pack',1,b'C3DSURF\0')
 rows=[r for r in m['assets'] if r['theater']=='DESERT' and r['export'].startswith('desert_surfaces_v04/')];assert len(rows)==18
 count=wet=foliage=0
 for r in rows:
  p=d/r['export'];get=lambda n:np.array(Image.open(p/(n+'.png')).convert('L'))
  rock=get('rock_mask');height=get('height');g=get('ground_mask');alpha=get('land_alpha');fol=get('foliage_mask')>127;water=get('water_mask')>127
  assert not rock[fol].any() and not height[fol].any(),(r['name'],'bush substrate retained')
  assert not height[water].any(),(r['name'],'raised water')
  assert np.all(g<=alpha),(r['name'],'sand painted into open water')
  assert Image.open(p/'player_colour_mask.png').getextrema()==(0,0)
  w,h=r['cells']
  for icon in r['have']:
   iz,ix=divmod(icon,w);sl=np.s_[iz*144:(iz+1)*144,ix*144:(ix+1)*144];key=(r['tid'],icon,r['variant'],0,0)
   assert np.array_equal(ground[key][...,0],g[sl]),(r['name'],icon,'base ground')
   expected=np.array(Image.fromarray(height[sl]).resize((hc,hc),Image.Resampling.BOX))
   assert np.array_equal(relief[key][...,0],expected),(r['name'],icon,'physical height')
   if water.any():
    assert np.array_equal(coast[key][...,0],alpha[sl]),(r['name'],icon,'water and terrain cuts disagree')
    wet+=int((alpha[sl]==0).sum())
   if fol[sl].any():
    alt=ground[(*key[:4],1)][...,0]
    assert np.all(alt[fol[sl]]==255),(r['name'],icon,'old bush paint retained')
   count+=1
  foliage+=int(fol.sum())
  if r['name']=='P02':
   assert np.array_equal(np.array(Image.open(p/'albedo.png').convert('RGB')),np.array(Image.open(native_source/r['source']).convert('RGB'))),'mineral source changed'
  if r['name'].startswith('BRIDGE'):
   native=np.array(Image.open(native_source/r['source']).convert('RGB'));out=np.array(Image.open(p/'albedo.png').convert('RGB'));protected=(rock==0)&(g==0)&~water
   assert np.array_equal(native[protected],out[protected]),'bridge timber repainted'
 material=source/'art/remaster/ROCKS/cliff_sandstone_v05';assert Image.open(material/'material.png').size==(1024,1024)
 s=json.loads((material/'source.json').read_text());assert s['texels_per_world_cell']==144
 return dict(passed=True,templates=len(rows),native_cells=count,open_water_texels_without_sand=wet,retired_bush_texels_without_rock_or_height=foliage,bridge_timber_preserved=True,mineral_source_preserved=True,material_pixels=[1024,1024],material_density=144)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--native-source',type=Path);p.add_argument('--playable',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();r=check(a.source,a.playable,a.native_source)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps(r,indent=2))
