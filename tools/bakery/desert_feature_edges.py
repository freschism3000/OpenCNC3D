"""Compile exact saved contact mattes without changing paintings or relief."""
from pathlib import Path
import hashlib,json
from PIL import Image

def ground_updates(root):
 result={}
 for version in ('desert_feature_edges_v01','desert_feature_edges_v02','desert_feature_edges_v03'):
  source=root/'art/remaster/textures/unified_daylight_v01'/version;m=json.loads((source/'MANIFEST.json').read_text())
  for r in m['assets']:
   original=root/r['source'];assert hashlib.sha256(original.read_bytes()).hexdigest()==r['source_sha256']
   p=source/r['ground_mask'];assert hashlib.sha256(p.read_bytes()).hexdigest()==r['ground_mask_sha256']
   w,h=r.get('cells',[1,1]);im=Image.open(p).convert('L');assert im.size==(w*144,h*144)
   assert Image.open(source/r['player_colour_mask']).getextrema()==(0,0)
   for icon in r.get('have',[r.get('icon',0)]):
    y,x=divmod(icon,w);result[r['tid'],icon,r['variant'],r['game']]=im.crop((x*144,y*144,(x+1)*144,(y+1)*144)).tobytes()
 return result
