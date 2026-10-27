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
 'Gather lowers the waist and chest a pixel while the front foot moves forward. The rear leg begins folding under the pelvis; this is the descent toward one-knee support rather than an extended two-foot lunge.',
 'The pelvis drops another pixel. Front shin is upright under its bent knee; rear knee approaches the floor and the rear ankle moves back. Head/collar and blue pack descend together without recentering the cell.',
 'Full kneeling load: rear knee cloth reaches the front-boot ground line, trailing rear shin runs back/left, and front thigh is near horizontal above an upright front shin. The grenade draws back beside the shoulder.',
 'Cocked throwing hand moves behind the helmet while the grounded knee and forward foot keep the same support. Blue pouch remains on the lowered rear waist and the sleeve/hand stays joined to its shoulder.',
 'The bent arm raises the held grenade at the back of the helmet. Both short arm segments remain compact. Rear knee stays down, rear calf trails and the opposite bent leg supplies a forward planted foot.',
 'The grenade rises above the crown. The full lower body is still the same kneeling support, without an accidental standing push-off; helmet, collar and lowered chest remain one fitted character.',
 'Release14 uses an empty raised hand above the helmet while the knee remains grounded. Front boot, bent front knee, rear knee and trailing shin are unchanged; the engine owns the projectile, with no muzzleflash.',
 'The empty hand extends high/forward from the kneeling stance. The chest leans with the fitted head while the grounded rear knee and forward foot continue supporting the throw; no rear-heel running pose.',
 'Forward/down follow-through is still kneeling: chest/head dip and the arm sweeps forward/low over the bent front leg. Rear knee/shin stay planted at the same ground, with the blue rear pouch attached to the waist.',
 'Upper body begins recovering from the follow-through while the knee remains grounded. The lowered throwing hand returns toward torso; the front shin and trailing rear calf preserve the low support.',
 'Push up: rear knee lifts off its contact, pelvis and chest rise together and rear foot comes inward. Front foot remains braced during this recovery rather than leaving the full throw in a standing lunge.',
 'Recovered upright ready stance returns after the kneeling throw. Empty hand is near ready height; the next ready pose equips the new grenade. Profile head/collar fit and blue back equipment remain consistent.',
]
CONTINUITY=[
 '0..7: the original small preparation shifts retain their pacing and exact2/4hold. The accepted character is still upright here, before the descent.',
 '8..10: waist, head and chest lower into one-knee support. Rear knee reaches the forward-boot ground line; forward thigh bends across the upright shin. This replaces the rejected standing lunge.',
 '10..17: grounded rear knee, trailing shin and planted forward foot remain a fixed lower-body support through cock, release14, extension15, dip16 and upper recovery17. The throw visibly happens from a knee.',
 '17..19: recover upper body, push up off the knee18, then regain upright ready19. Returning to the exact native ready0 equips a new grenade, matching source completion into DO_STAND_READY.',
 'Repeated20-stage playback and ready/entry are checked separately. Technical connectivity/short limbs did not detect the missing kneel in v31; the support state must first be understood and visibly matched to the source action.',

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
 art=ROOT/'art/sprites/infantry-snes-v01/motion-study-v33-e2-throw';approved=ROOT/'art/sprites/infantry-snes-v01/motion-study-v28-e2-run'
 for file,h in PROTECTED.items():assert sha(a.protected_root/file)==h,('E1 changed',file)
 receipt=json.loads((approved/'approval-2026-10-21.json').read_text())
 for group in('accepted','paired_mask_hashes','paired_role_hashes'):
  for file,h in receipt[group].items():assert sha(approved/file)==h,('Accepted run changed',file)
 current=art/'approval-2026-10-21.json'
 if current.exists():
  accepted=json.loads(current.read_text())
  for group in('accepted','paired_mask_hashes','paired_role_hashes','presentation'):
   for file,h in accepted[group].items():assert sha(art/file)==h,('Accepted East throw changed',file)
 state='accepted21Oct2026'if current.exists()else'acceptance pending'
 catalog=json.loads((art/'frames.json').read_text());checks=[];palette=set();unique={};reviews=[]
 for rec in catalog['keys']:
  k=rec['stage'];p=art/f'E-throw-{k:02}.png';im=Image.open(p).convert('RGBA');mask=Image.open(p.with_stem(p.stem+'-player-mask'));roles=Image.open(p.with_stem(p.stem+'-paint-roles'))
  assert im.size==mask.size==roles.size==(100,78);assert set(im.getchannel('A').get_flattened_data())<={0,255};assert set(mask.get_flattened_data())<={0,255}
  points={(x,y)for y in range(78)for x in range(100)if im.getpixel((x,y))[3]};comp=components(points);assert len(comp)==1,(k,comp)
  assert all(30<x<77 and 8<y<42 for x,y in points);assert sha(p)==rec['sha256']
  assert all(im.getpixel((x,y))[3]or not mask.getpixel((x,y))for y in range(78)for x in range(100))
  blue=[p for p in points if(lambda c:c[2]>120 and c[2]>c[0]*1.4)(im.getpixel(p))];assert len(blue)>=7;assert all(mask.getpixel(p)==0 for p in blue)
  trace=rec['parts'][-1];assert trace['grenade_held']==(k<14);assert trace['release']==(k==14);assert trace['actor_anchor']==[54,40]
  assert trace['kneeling']==(10<=k<=17)
  if 10<=k<=17:
   far,near=rec['parts'][:2];assert far['hip']==[7,17]and far['knee']==[5,21]and far['ankle']==[1.5,22]
   assert near['hip']==[8,17]and near['knee']==[12,17.5]and near['ankle']==[12,22]
   assert im.getpixel((49,38))[3]and im.getpixel((56,38))[3],('Lost knee/front-foot ground contact',k)
   assert trace['knee_ground_y']==22
  palette|={c for c in im.get_flattened_data()if c[3]};unique.setdefault(hashlib.sha256(im.tobytes()).hexdigest(),[]).append(k)
  for leg in rec['parts'][:2]:
   from math import hypot
   hip,knee,ankle=leg['hip'],leg['knee'],leg['ankle']
   assert max(hypot(knee[0]-hip[0],knee[1]-hip[1]),hypot(ankle[0]-knee[0],ankle[1]-knee[1]))<=4.8
  reviews.append(dict(view='E',stage=k,sha256=sha(p),observation=NOTES[k],evidence=f'five-frame-audit-{k//5}.png',status='viewed; '+state))
  rec['review']='inspected; '+state;checks.append(dict(stage=k,sha256=sha(p),bounds=im.getbbox(),components=comp,blue_pixels=len(blue)))
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
 (art/'personal-frame-review.json').write_text(json.dumps(dict(date='2026-10-21',scope='20East throw cells inspected against source strip and approved run identity; '+state,frames=reviews,continuity=CONTINUITY),indent=2)+'\n')
 catalog['scope']='East20-stage native throw inspected; '+state;(art/'frames.json').write_text(json.dumps(catalog,indent=2)+'\n')
 (art/'technical-checks.json').write_text(json.dumps(dict(kind='Technical evidence, not artistic acceptance',count=20,unique=19,exact_source_hold=[2,4],fps=15,release_stage=14,palette_size=len(palette),anchor=[54,40],frames=checks,approved_run_preserved=48,protected_E1=PROTECTED),indent=2)+'\n')
 print('20connected frames;19unique/hold2,4;grounded knee10..17;release14;20personal reviews;approved48run/E1unchanged')

if __name__=='__main__':main()
