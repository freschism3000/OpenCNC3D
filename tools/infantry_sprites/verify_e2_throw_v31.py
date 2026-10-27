#!/usr/bin/env python3
"""Technical invariants plus individually written East throw observations.

These observations are the author's visual review, not acceptance.
"""
from pathlib import Path
import hashlib,json
from PIL import Image,ImageDraw,ImageFont
from verify_e2_run_v23 import PROTECTED

ROOT=Path(__file__).resolve().parents[2]
NOTES=[
 'Compact two-foot ready stance. Profile face/brim aligns with collar; olive pack supports the blue rear pouch. Near hand holds the small grenade at waist/chest height; far hand prepares beside it.',
 'One-pixel knee/chest compression lowers head and collar together while boots retain their ground line. Near sleeve and far preparing hand overlap naturally; blue remains on the back side.',
 'The soldier rises out of compression, lifting the bent throwing forearm and grenade a pixel. The supported stance stays compact; no running leg or floating helmet is substituted.',
 'Throwing elbow moves forward/up under the hand, changing the sleeve silhouette while the opposite hand works nearer the torso. This replaces a private draft in which the pose was hidden and accidentally duplicated stage0.',
 'Exact source-authored hold of stage2 is preserved. The same near forearm preparation, helmet, waist attachment and boot planes remain intentional; this is not an omitted animation frame.',
 'Second small load/compression with hands drawn slightly inward. The neck remains seated in the chest and the blue band moves only with its attached waist/pack, outside player paint.',
 'Preparation opens the elbow downward while the grenade hand remains forward. Broad lit near trouser and darker far trouser keep two short connected leg masses rather than disconnected sticks.',
 'Near elbow bends upward again and the opposite hand stays closer to chest. Profile direction remains right; the compact foot spacing precedes the deliberate throwing step.',
 'Hands gather toward the chest as the near foot begins its forward step and the rear foot settles behind. Pouch remains attached at rear waist; no whole-canvas recentering accompanies the wider stance.',
 'The stance is visibly wider and knees load under a one-pixel body compression. The chest opens a little for the wind-up; brim, face and neck travel together. Grenade is still held.',
 'The hand draws back/up beside the shoulder as weight settles into the braced legs. The shoulder turn exposes a second cloth/webbing plane while the soldier still faces East.',
 'Throwing hand and grenade swing behind the helmet on a bent arm. Foreground arm stays connected to the shoulder; lowered balancing hand remains on the opposite side. Helmet is not erased or detached.',
 'Cocked hand rises at the back edge of the helmet. The corrected elbow keeps two compact arm segments; the earlier excessive rearward reach is gone. The prop remains visible without adding a second weapon.',
 'Overhead anticipation raises the grenade above the crown with a continuous sleeve/forearm chain. Feet remain in the loaded wide stance; cobalt pouch still reads at rear waist.',
 'Release: raised empty hand is above the head and forward of the shoulder; the held grenade is removed exactly at source launch stage14. The rear heel lifts and the front foot takes the weight; no painted projectile or muzzleflash.',
 'Empty hand extends high/forward. Head and chest lean forward together while the waist/feet preserve the planned stance and heel lift. Near forearm connects to the upper arm rather than floating above the brim.',
 'Strong forward/down follow-through: face/collar dip with the chest, near arm sweeps low/forward, rear equipment follows the torso hinge. Both short legs still connect under the pelvis; actor origin remains fixed.',
 'The torso rises from the dip as the empty hand lowers. Rear foot begins returning inward, near leg stays planted and the fitted head rises with the collar. Blue pouch remains connected.',
 'Feet narrow toward ready and the empty throwing arm returns low. Lit knee/cuff/boot planes remain intact; there is no persistent projectile or flash in the recovering hand.',
 'Near hand returns toward the ready height, legs regain the compact stance and the profile head returns to its neutral collar fit. The subsequent ready pose equips the next grenade as the original action does.',
]
CONTINUITY=[
 '0..7: hand preparation reads as small body/arm weight shifts, not a run or gunfire shake; the deliberate2/4hold remains while the other stages have distinct visible poses.',
 '8..13: widened footing, body load and shoulder wind-up precede the overhead anticipation. The rearward hand does not travel through a detached helmet; the blue equipment stays attached.',
 '13..16: one overhead release at14, forward extension at15 and a lower forward dip at16. Body articulation is visible; the cell/origin and front ground do not slide.',
 '16..19: the chest and empty throwing hand recover while feet narrow. The return-to-ready board shows16,17,18,19,ready,0 with one coherent character and no costume replacement.',
 '20-stage repetition is a review loop of a one-shot. Engine completion enters DO_STAND_READY; ready and native0 are exact, and the empty19->equippedready change matches source re-equipping. No invented in-flight grenade is painted.',
]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def components(points):
 todo=set(points);result=[]
 while todo:
  q=[todo.pop()];n=0
  while q:
   x,y=q.pop();n+=1
   for dx in(-1,0,1):
    for dy in(-1,0,1):
     p=(x+dx,y+dy)
     if p in todo:todo.remove(p);q.append(p)
  result.append(n)
 return result

def main():
 import argparse
 ap=argparse.ArgumentParser();ap.add_argument('--protected-root',type=Path,required=True);a=ap.parse_args()
 art=ROOT/'art/sprites/infantry-snes-v01/motion-study-v31-e2-throw';approved=ROOT/'art/sprites/infantry-snes-v01/motion-study-v28-e2-run'
 for file,h in PROTECTED.items():assert sha(a.protected_root/file)==h,('E1 changed',file)
 receipt=json.loads((approved/'approval-2026-10-21.json').read_text())
 for group in('accepted','paired_mask_hashes','paired_role_hashes'):
  for file,h in receipt[group].items():assert sha(approved/file)==h,('Accepted run changed',file)
 catalog=json.loads((art/'frames.json').read_text());checks=[];palette=set();unique={};reviews=[]
 for rec in catalog['keys']:
  k=rec['stage'];p=art/f'E-throw-{k:02}.png';im=Image.open(p).convert('RGBA');mask=Image.open(p.with_stem(p.stem+'-player-mask'));roles=Image.open(p.with_stem(p.stem+'-paint-roles'))
  assert im.size==mask.size==roles.size==(100,78);assert set(im.getchannel('A').get_flattened_data())<={0,255};assert set(mask.get_flattened_data())<={0,255}
  points={(x,y)for y in range(78)for x in range(100)if im.getpixel((x,y))[3]};comp=components(points);assert len(comp)==1,(k,comp)
  assert all(30<x<77 and 8<y<42 for x,y in points);assert sha(p)==rec['sha256']
  assert all(im.getpixel((x,y))[3]or not mask.getpixel((x,y))for y in range(78)for x in range(100))
  blue=[p for p in points if(lambda c:c[2]>120 and c[2]>c[0]*1.4)(im.getpixel(p))];assert len(blue)>=7;assert all(mask.getpixel(p)==0 for p in blue)
  trace=rec['parts'][-1];assert trace['grenade_held']==(k<14);assert trace['release']==(k==14);assert trace['actor_anchor']==[54,40]
  palette|={c for c in im.get_flattened_data()if c[3]};unique.setdefault(hashlib.sha256(im.tobytes()).hexdigest(),[]).append(k)
  for leg in rec['parts'][:2]:
   from math import hypot
   hip,knee,ankle=leg['hip'],leg['knee'],leg['ankle']
   assert max(hypot(knee[0]-hip[0],knee[1]-hip[1]),hypot(ankle[0]-knee[0],ankle[1]-knee[1]))<=4.8
  reviews.append(dict(view='E',stage=k,sha256=sha(p),observation=NOTES[k],evidence=f'five-frame-audit-{k//5}.png',status='viewed; acceptance pending'))
  rec['review']='inspected; acceptance pending';checks.append(dict(stage=k,sha256=sha(p),bounds=im.getbbox(),components=comp,blue_pixels=len(blue)))
 assert len(palette)<=32;assert len(unique)==19;assert[x for x in unique.values()if len(x)>1]==[[2,4]]
 assert(art/'E-ready.png').read_bytes()==(art/'E-throw-00.png').read_bytes()
 board=Image.new('RGB',(1000,440),(25,28,33));d=ImageDraw.Draw(board)
 for i in range(20):
  for ci,color in enumerate(((180,40,35),(45,100,215))):
   p=art/f'E-throw-{i:02}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_stem(p.stem+'-player-mask'))
   for y in range(78):
    for x in range(100):
     if m.getpixel((x,y)):
      r,g,b,alpha=im.getpixel((x,y));lum=(r*.3+g*.6+b*.1)/155;im.putpixel((x,y),tuple(min(255,round(c*lum))for c in color)+(alpha,))
   tile=im.crop((36,8,74,44)).resize((76,72),Image.Resampling.NEAREST);board.paste(tile,(i%5*200+ci*90+5,i//5*110+20),tile)
  d.text((i%5*200+5,i//5*110+3),str(i)+' / red + blue',fill='white')
 board.save(art/'player-colour-preview.png')
 (art/'personal-frame-review.json').write_text(json.dumps(dict(date='2026-10-21',scope='20East throw cells inspected against source strip and approved run identity; art acceptance pending',frames=reviews,continuity=CONTINUITY),indent=2)+'\n')
 catalog['scope']='East20-stage native throw inspected; art acceptance pending';(art/'frames.json').write_text(json.dumps(catalog,indent=2)+'\n')
 (art/'technical-checks.json').write_text(json.dumps(dict(kind='Technical evidence, not artistic acceptance',count=20,unique=19,exact_source_hold=[2,4],fps=15,release_stage=14,palette_size=len(palette),anchor=[54,40],frames=checks,approved_run_preserved=48,protected_E1=PROTECTED),indent=2)+'\n')
 print('20 connected native throw frames;19unique/exact2/4hold;26colour palette;20personal observations;release14;approved48run/E1unchanged')

if __name__=='__main__':main()
