#!/usr/bin/env python3
"""Export explicit P01/P03 object keepers and a soft ground contact matte.

The original paintings and all model/relief/coast data remain immutable. Only
world-ground coverage changes; polygons use the 144px source pixel coordinate.
"""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd
KEEPERS={
 'P01':[
  [(19,25),(25,21),(25,19),(31,16),(36,19),(36,21),(45,25),(48,29),(45,35),(37,36),(29,35),(23,31)],
  [(72,69),(77,68),(84,69),(90,74),(88,82),(78,81),(73,78)],
  [(96,69),(102,69),(107,74),(113,78),(113,83),(103,85),(96,82),(94,77)],
  [(36,103),(43,101),(50,100),(54,104),(54,108),(46,111),(41,114),(38,110)]],
 'P03':[
  [(25,21),(29,19),(35,18),(45,18),(51,24),(48,32),(47,40),(40,46),(31,42),(25,36),(23,29)],
  [(58,27),(67,32),(75,43),(75,47),(68,47),(62,42),(56,38)],
  [(92,32),(100,31),(109,34),(114,40),(111,47),(112,55),(118,61),(127,64),(134,73),(132,82),(124,87),(122,92),(113,99),(104,104),(95,100),(90,102),(82,94),(81,88),(74,84),(69,76),(69,67),(76,62),(82,55),(88,51),(91,45)],
  [(23,98),(29,95),(34,92),(40,97),(45,98),(47,106),(43,112),(37,117),(29,118),(22,114),(21,108)]]}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def smooth(x):x=np.clip(x,0,1);return x*x*(3-2*x)
def repair(root,out):
 art=root/'art/remaster/textures/DESERT';feature=art/'2026-10-19/cliff_shore_repair_v07';rows=json.loads((feature/'FEATURE_MANIFEST.json').read_text())['assets'];records=[]
 out.mkdir(parents=True,exist_ok=True)
 for name,polys in KEEPERS.items():
  row=next(r for r in rows if r['name']==name);old=feature/row['export'];source=art/name/'final_144.png';mask=np.asarray(Image.open(old/'ground_mask.png'))<128
  keep=Image.new('L',(144,144));draw=ImageDraw.Draw(keep)
  for poly in polys:draw.polygon(poly,fill=255)
  keep=(np.asarray(keep)>0)&mask
  outside=nd.distance_transform_edt(~keep);inside=nd.distance_transform_edt(mask)
  weight=(1-smooth(outside/8))*smooth(inside/8);weight[keep]=1
  ground=np.uint8(np.rint(255*(1-weight)));assert np.all(ground[~mask]==255)and np.all(ground[keep]==0)
  dest=out/name;dest.mkdir(exist_ok=True);Image.fromarray(ground).save(dest/'ground_mask.png');Image.fromarray(np.uint8(keep)*255).save(dest/'object_keep_mask.png');Image.new('L',(144,144),0).save(dest/'player_colour_mask.png')
  records.append(dict(name=name,tid=row['tid'],variant=0,game=0,icon=0,source=str(source.relative_to(root)),source_sha256=sha(source),original_ground=str((old/'ground_mask.png').resolve().relative_to(root)),original_ground_sha256=sha(old/'ground_mask.png'),source_pixels_per_cell=144,keeper_polygons=polys,outside_keeper_feather_px=8,inward_old_perimeter_feather_px=8,ground_mask=str((dest/'ground_mask.png').relative_to(out)),ground_mask_sha256=sha(dest/'ground_mask.png'),object_keep_mask=str((dest/'object_keep_mask.png').relative_to(out)),object_keep_mask_sha256=sha(dest/'object_keep_mask.png'),player_colour_mask=str((dest/'player_colour_mask.png').relative_to(out)),player_colour_mask_sha256=sha(dest/'player_colour_mask.png'),player_colour_pairing='Original source albedo, same144x144 UV layout; intentionally black, non-recolourable terrain',soft_pixels=int(((ground>0)&(ground<255)).sum()),painted_rgb_changed=False,geometry_changed=False))
 (out/'MANIFEST.json').write_text(json.dumps(dict(schema=1,scope='Enhanced Desert P01/P03 ground coverage only',assets=records),indent=2)+'\n');print([(r['name'],r['soft_pixels'])for r in records])
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();repair(a.root,a.root/'art/remaster/textures/unified_daylight_v01/desert_feature_edges_v01')
