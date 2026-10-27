#!/usr/bin/env python3
"""Feather D29's detached rock-soil islands, preserving its actual road body."""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd
ROOT=Path(__file__).resolve().parents[2]
KEEPERS=[[(216,91),(221,86),(229,85),(237,90),(240,96),(236,100),(231,105),(224,105),(217,101)],
 [(4,207),(12,200),(14,190),(24,182),(34,180),(42,187),(45,197),(48,204),(52,211),(48,218),(35,222),(25,218),(16,222),(6,215)],
 [(7,240),(12,235),(20,232),(28,233),(35,238),(34,244),(29,250),(26,254),(18,256),(10,251)]]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def smooth(x):x=np.clip(x,0,1);return x*x*(3-2*x)
def build():
 art=ROOT/'art/remaster/textures/DESERT';feature=art/'2026-10-19/cliff_shore_repair_v07';r=next(r for r in json.loads((feature/'FEATURE_MANIFEST.json').read_text())['assets']if r['name']=='D29');old=feature/r['export']/'ground_mask.png';source=art/'D29/final_144.png';ground=np.array(Image.open(old).convert('L'));mask=ground<128;components,count=nd.label(mask,np.ones((3,3)));selected=np.isin(components,[int(components[205,30]),int(components[96,230])]);assert int(components[205,30])!=int(components[50,50]),'rock selection reached road'
 keep=Image.new('L',ground.shape[::-1]);d=ImageDraw.Draw(keep)
 for poly in KEEPERS:d.polygon(poly,fill=255)
 keep=(np.array(keep)>0)&selected;weight=(1-smooth(nd.distance_transform_edt(~keep)/8))*smooth(nd.distance_transform_edt(selected)/8);weight[keep]=1;fixed=ground.copy();fixed[selected]=np.uint8(np.rint(255*(1-weight[selected])));assert np.array_equal(fixed[~selected],ground[~selected]);assert np.all(fixed[keep]==0)
 out=ROOT/'art/remaster/textures/unified_daylight_v01/desert_feature_edges_v02';dest=out/'D29';dest.mkdir(parents=True,exist_ok=True)
 for name,data in [('ground_mask',fixed),('object_keep_mask',np.uint8(keep)*255),('repair_region_mask',np.uint8(selected)*255)]:Image.fromarray(data).save(dest/(name+'.png'))
 Image.new('L',ground.shape[::-1],0).save(dest/'player_colour_mask.png')
 row=dict(name='D29',tid=121,cells=r['cells'],have=r['have'],variant=0,game=0,source=str(source.relative_to(ROOT)),source_sha256=sha(source),original_ground=str(old.resolve().relative_to(ROOT)),original_ground_sha256=sha(old),keeper_polygons=KEEPERS,component_seeds=[[30,205],[230,96]],feather_px=8,ground_mask='D29/ground_mask.png',ground_mask_sha256=sha(dest/'ground_mask.png'),object_keep_mask='D29/object_keep_mask.png',repair_region_mask='D29/repair_region_mask.png',player_colour_mask='D29/player_colour_mask.png',player_colour_pairing='all-black linear grayscale; original D29 288x288 albedo, identical UV layout and dimensions',soft_pixels=int(((fixed>0)&(fixed<255)).sum()),road_coverage_changed=False,painted_rgb_changed=False,geometry_changed=False)
 (out/'MANIFEST.json').write_text(json.dumps(dict(schema=1,scope='Nod1 D29 detached rock contacts only; road component exact',assets=[row]),indent=2)+'\n');print('D29',row['soft_pixels'],'soft pixels')
if __name__=='__main__':build()
