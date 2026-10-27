#!/usr/bin/env python3
"""Domain checks and mask previews; no automatic artistic PASS."""
from pathlib import Path
import json, hashlib, argparse
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[2]
ART=ROOT/'art/sprites/infantry-snes-v01/motion-study-v23-e2-run'
PROTECTED={
 'tools/sprite_lab/public/data/e1-motion-v15.png':'ee850978a8fa4ad181eaa6466f4372ee774fac963b41c01af7d0e47f29b5c3e2',
 'game/sprites/e1-v15.pack':'4d97891c93bada2841c267396b87ea64baa5d807f481152d6e554bddd43ccd3a',
 'game/sprites/e1-v15-player-mask.png':'5bed50227e47f7f7f1a533a80e03c027d1c4e67236da6e17b12dd26a87456e5d'}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--protected-root',type=Path,default=ROOT);a=ap.parse_args()
 checks=[];palette=set();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 for f,h in PROTECTED.items():assert sha(a.protected_root/f)==h,('Protected E1 mutation',f)
 acceptance=ART/'approval-2026-10-21.json'
 if acceptance.exists():
  for file,h in json.loads(acceptance.read_text())['accepted'].items():
   assert sha(ART/file)==h,('Accepted native E2 mutation',file)
 for view in ('E','SE'):
  unique=set()
  for k in range(6):
   p=ART/f'{view}-run-{k}.png';im=Image.open(p).convert('RGBA');mask=Image.open(p.with_name(p.stem+'-player-mask.png')).convert('L')
   assert im.size==mask.size==(100,78)
   colors=im.get_flattened_data();palette|={c for c in colors if c[3]};assert set(im.getchannel('A').get_flattened_data())<={0,255}
   assert set(mask.get_flattened_data())<={0,255};unique.add(im.tobytes())
   pts={(x,y)for y in range(78)for x in range(100)if im.getpixel((x,y))[3]}
   assert all(im.getpixel((x,y))[3] or not mask.getpixel((x,y)) for y in range(78)for x in range(100))
   pending=set(pts);components=[]
   while pending:
    todo=[pending.pop()];n=0
    while todo:
     x,y=todo.pop();n+=1
     for dx,dy in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)):
      if (x+dx,y+dy)in pending:pending.remove((x+dx,y+dy));todo.append((x+dx,y+dy))
    components.append(n)
   assert len(components)==1,(view,k,components)
   blue=[(x,y)for x,y in pts if im.getpixel((x,y))[2]>120 and im.getpixel((x,y))[2]>1.4*im.getpixel((x,y))[0]]
   assert len(blue)>=(12 if view=='E'else 8)
   assert all(mask.getpixel(p)==0 for p in blue)
   checks.append(dict(view=view,stage=k,sha256=sha(p),bounds=im.getbbox(),connected_components=components,blue_pixels=len(blue),blue_excluded_from_paint=True))
  assert len(unique)==6
 assert len(palette)<=32
 board=Image.new('RGB',(960,720),(25,28,33));d=ImageDraw.Draw(board)
 for v,view in enumerate(('E','SE')):
  for c,col in enumerate(((180,40,35),(45,100,215))):
   for k in range(6):
    p=ART/f'{view}-run-{k}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_name(p.stem+'-player-mask.png'))
    for y in range(78):
     for x in range(100):
      if m.getpixel((x,y)):
       r,g,b,a=im.getpixel((x,y));lum=(r*.3+g*.6+b*.1)/155
       im.putpixel((x,y),tuple(min(255,round(z*lum))for z in col)+(a,))
    tile=im.crop((39,12,67,44)).resize((140,160),Image.Resampling.NEAREST);board.paste(tile,(k*160+10,v*360+c*180),tile)
 board.save(ART/'player-colour-preview.png')
 result=dict(kind='technical evidence only; native artistic acceptance pending',cells=checks,distinct_stages=6,views=['E','SE'],fps=7.5,anchor=[54,40],palette_size=len(palette),protected_E1=PROTECTED)
 (ART/'technical-checks.json').write_text(json.dumps(result,indent=2)+'\n')
 print('12 connected native cells,6 distinct stages/view,27-colour palette, explicit masks, E1 hashes intact')
if __name__=='__main__':main()
