#!/usr/bin/env python3
"""Verify sprite domain invariants and bind manual observations to actual cells.
No technical result supplies artistic acceptance of the six new cameras.
"""
from pathlib import Path
import argparse,hashlib,json
from PIL import Image,ImageDraw
from verify_e2_run_v23 import PROTECTED
ROOT=Path(__file__).resolve().parents[2]
VIEWS=('N','NW','W','SW','S','SE','E','NE')

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--protected-root',type=Path,required=True);a=ap.parse_args()
 art=ROOT/'art/sprites/infantry-snes-v01/motion-study-v28-e2-run';accepted=ROOT/'art/sprites/infantry-snes-v01/motion-study-v23-e2-run'
 for name,h in PROTECTED.items():assert sha(a.protected_root/name)==h,('Protected E1 changed',name)
 approval=json.loads((accepted/'approval-2026-10-21.json').read_text());notes=json.loads((art/'personal-observations.json').read_text());records=json.loads((art/'frames.json').read_text());checks=[];palette=set();manual=[]
 current_approval=art/'approval-2026-10-21.json'
 if current_approval.exists():
  receipt=json.loads(current_approval.read_text())
  for group in ('accepted','paired_mask_hashes','paired_role_hashes'):
   for file,h in receipt[group].items():assert sha(art/file)==h,('Accepted run component changed',file)
 for view in VIEWS:
  unique=set()
  for k in range(6):
   name=f'{view}-run-{k}.png';p=art/name;im=Image.open(p).convert('RGBA');mask=Image.open(p.with_name(p.stem+'-player-mask.png')).convert('L');roles=Image.open(p.with_name(p.stem+'-paint-roles.png')).convert('L')
   assert im.size==mask.size==roles.size==(100,78);assert set(im.getchannel('A').get_flattened_data())<={0,255};assert set(mask.get_flattened_data())<={0,255}
   unique.add(im.tobytes());palette|={c for c in im.get_flattened_data()if c[3]}
   points={(x,y)for y in range(78)for x in range(100)if im.getpixel((x,y))[3]};assert all(30<=x<78 and 10<=y<42 for x,y in points)
   assert all(im.getpixel((x,y))[3]or not mask.getpixel((x,y))for y in range(78)for x in range(100))
   todo=set(points);components=[]
   while todo:
    q=[todo.pop()];n=0
    while q:
     x,y=q.pop();n+=1
     for dx,dy in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)):
      if(x+dx,y+dy)in todo:todo.remove((x+dx,y+dy));q.append((x+dx,y+dy))
    components.append(n)
   assert len(components)==1,(view,k,components)
   blue=[p for p in points if (lambda c:c[2]>120 and c[2]>c[0]*1.4)(im.getpixel(p))]
   assert len(blue)>=7,(view,k,'Blue cue lost');assert all(mask.getpixel(p)==0 for p in blue)
   rec=next(r for r in records['keys']if r['view']==view and r['stage']==k);assert sha(p)==rec['sha256']
   if view in('E','SE'):
    assert sha(p)==approval['accepted'][name]
    for suffix in('-player-mask','-paint-roles'):assert(art/f'{view}-run-{k}{suffix}.png').read_bytes()==(accepted/f'{view}-run-{k}{suffix}.png').read_bytes()
   else:
    observation=notes['observations'][view][k];assert observation
    manual.append(dict(view=view,stage=k,sha256=sha(p),observation=observation,status='accepted21Oct2026'if current_approval.exists()else'inspected; design-review visual acceptance pending'))
    rec['status']='accepted21Oct2026'if current_approval.exists()else'inspected; design-review visual acceptance pending'
   if view in('N','S'):
    for leg in rec['limbs']:assert leg['hip'][0]==leg['knee'][0]==leg['ankle'][0],('Lateral knee drift',view,k)
   checks.append(dict(view=view,stage=k,sha256=sha(p),bounds=im.getbbox(),components=components,blue_pixels=len(blue),blue_excluded_from_player_paint=True))
  assert len(unique)==6,(view,'Missing distinct stage')
 assert len(palette)<=32
 board=Image.new('RGB',(960,8*200),(25,28,33));d=ImageDraw.Draw(board)
 for vi,view in enumerate(VIEWS):
  for ci,color in enumerate(((180,40,35),(45,100,215))):
   for k in range(6):
    p=art/f'{view}-run-{k}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_name(p.stem+'-player-mask.png'))
    for y in range(78):
     for x in range(100):
      if m.getpixel((x,y)):
       r,g,b,alpha=im.getpixel((x,y));lum=(r*.3+g*.6+b*.1)/155;im.putpixel((x,y),tuple(min(255,round(c*lum))for c in color)+(alpha,))
    tile=im.crop((39,12,67,44)).resize((84,96),Image.Resampling.NEAREST);board.paste(tile,(k*160+ci*76,vi*200+20),tile)
  d.text((5,vi*200+2),view+' / red and blue player paint',fill='white')
 board.save(art/'player-colour-preview.png')
 (art/'personal-frame-review.json').write_text(json.dumps(dict(date='2026-10-21',kind='36 hash-bound observations; accepted21Oct2026'if current_approval.exists()else'36 hash-bound observations; acceptance pending',accepted_cells='12 exact E/SE v23 images + masks/roles unchanged',frames=manual),indent=2)+'\n')
 records['status']='All48run cells accepted21Oct2026;12 exact earlier E/SE preserved.'if current_approval.exists()else'36 new cameras inspected; design-review visual acceptance pending.12 exact accepted E/SE preserved.';(art/'frames.json').write_text(json.dumps(records,indent=2)+'\n')
 result=dict(kind='technical evidence only; no artistic verdict',palette_size=len(palette),cells=checks,accepted_native_preserved=12,source_fps=7.5,stages=6,anchor=[54,40],protected_E1=PROTECTED)
 (art/'technical-checks.json').write_text(json.dumps(result,indent=2)+'\n')
 print(f'48 connected cells;6 distinct stages/view;{len(palette)} colours;36 observations;12 exact approved images/masks/roles intact;E1 intact')
if __name__=='__main__':main()
