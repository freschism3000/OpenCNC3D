#!/usr/bin/env python3
"""Bind written SE observations; preserve approved East/Run/E1."""
from pathlib import Path
import argparse,hashlib,json
from math import hypot
from PIL import Image,ImageDraw
from verify_e2_throw_v33 import components,sha,PROTECTED

ROOT=Path(__file__).resolve().parents[2]
NOTES=[
 'Standing SE preparation uses the accepted front-right head/chest planes and lower screen-left near shoulder/hip. The bent near arm carries the small grenade across the body; blue waist gear remains behind at screen-left.',
 'One-pixel compression brings the clasped hand against the chest. The held item is occluded rather than drawn over the cloth, permitting the exact source1/19hold without inventing a post-release grenade. Both feet remain in their own quarter-view depth.',
 'The near forearm rises slightly and the held grenade becomes visible. Fitted helmet, face and collar stay together; the bright near trouser and darker far trouser remain complete short forms.',
 'Preparing elbow and hand move a pixel forward/right while the far hand balances. Chest still has two diagonal planes and the face points SE; this is a visible arm preparation change, not a new body alignment.',
 'Exact stage2hold, matching the original2/4repeat. The same visible hand/grenade, collar, belt and legs are deliberate source timing rather than absent animation.',
 'Second small compression draws the hand closer to chest. Helmet follows its collar and the cobalt pouch stays attached to the rear waist, outside player paint.',
 'Near elbow lowers and the hand crosses closer to the waist while the far hand stays behind the chest. Two standing leg shapes remain separate, with the near foot lower in screen depth.',
 'The near hand gathers at chest height. Fixed profile/quarter-view head fit, broad cloth planes and blue outer pouch remain readable; the soldier has not yet dropped to the throwing knee.',
 'Exact stage7hold as in the original7/8repeat. It intentionally delays the descent rather than copying the East gather key, preserving this camera\'s source pacing.',
 'The pelvis/chest lower and the legs begin folding into support. Forward foot moves right/down while the rear leg folds behind/up-left. The head and waist descend together at the fixed actor origin.',
 'Kneeling load: front thigh projects right/down into an upright shin and planted toe; rear knee is visibly separate to the left/up, with the calf trailing farther left/up. Near/far hip depth agrees with the SE chest and head.',
 'The cocked forearm emerges to the right of the helmet and the grenade is held beside/above it. Foreground forearm painting preserves a visible hand path while the crown/face stay fitted. Rear knee and forward foot stay planted.',
 'The grenade raises farther above/right. The bent sleeve/forearm chain remains joined to its near shoulder; the same low knee support and trailing shin stay intact below the torso turn.',
 'High anticipation raises the grenade above the crown. The arm occupies the right edge of the head rather than being erased by it. The SE helmet/face, lowered belt and blue back detail remain one character.',
 'Release14: empty hand rises along the near/left side of the helmet in the fast overhand swing. Rear knee and front foot remain planted in their distinct ground-plane depths. No native grenade/projectile or muzzleflash remains in the hand.',
 'The empty forearm extends high/forward right from the kneeling body. Raised sleeve and hand remain visible outside the face, while the same front thigh/shin and rear knee/calf preserve support.',
 'Down-right follow-through keeps the knee grounded as the head/chest dip and near arm sweeps low. The outer blue pouch face is exposed one pixel toward the rear, still connected to the waist/pack instead of disappearing under the sleeve.',
 'Upper recovery raises the chest and returns the empty hand lower across the body while the knee remains grounded. Separate bent front leg and trailing rear shin continue to read as a supported kneel.',
 'Push-up lifts the rear knee and raises the pelvis/chest together while feet begin narrowing. The forward boot remains lower/right in the quarter ground plane, avoiding a copied East horizontal boot or a sudden full-body slide.',
 'Exact pose1recovery hold. Clasped hand contains no visible grenade after release; the same one-pixel compressed upright shape matches the source1/19repeat. Next ready0equips/shows the new grenade with a small hand change.',
]
CONTINUITY=[
 '0..8preparation retains exact1/19,2/4,7/8camera-specific holds and seventeen distinct visible poses. Occluded hand equipment at1permits the source recovery alias without painting a new projectile.',
 '9..10descent transfers support into a visible rear knee and planted forward foot. The pelvis lowers as a clothed body; no disconnected head, pants translation or crooked rear run leg is used.',
 '10..17lower support stays fixed: rear grounded knee/calf trails up-left, front thigh/foot leads down-right. Equal profile y coordinates would flatten this ground plane, so compare the camera depth as well as contacts.',
 '11..15cock/raise/release/extension has a visible forearm/hand path around the fitted helmet. Whole-head occlusion erased it in privatev34; foreground forearm restores the source action without shifting the helmet off the collar.',
 '16..19dip, upper recovery, push-up and pose1hold return toward ready. Native1x/2x/silhouette and the completion-ready-entry board retain the same SE character, distinct from the unchanged accepted East profile.',
]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--protected-root',type=Path,required=True);a=ap.parse_args()
 art=ROOT/'art/sprites/infantry-snes-v01/motion-study-v37-e2-throw';east=ROOT/'art/sprites/infantry-snes-v01/motion-study-v33-e2-throw';run=ROOT/'art/sprites/infantry-snes-v01/motion-study-v28-e2-run'
 for file,h in PROTECTED.items():assert sha(a.protected_root/file)==h,('Protected E1 changed',file)
 for folder in(east,run):
  accepted=json.loads((folder/'approval-2026-10-21.json').read_text())
  for group in('accepted','paired_mask_hashes','paired_role_hashes'):
   for file,h in accepted[group].items():
    assert sha(folder/file)==h,('Accepted art changed',file)
    if folder==east:assert sha(art/file)==h,('East copy changed',file)
 current=art/'approval-2026-10-21.json'
 if current.exists():
  receipt=json.loads(current.read_text())
  for group in('accepted','paired_mask_hashes','paired_role_hashes','presentation'):
   for file,h in receipt[group].items():assert sha(art/file)==h,('Accepted E/SEthrow changed',file)
 state='accepted21Oct2026'if current.exists()else'SE acceptance pending'
 catalog=json.loads((art/'frames.json').read_text());palette=set();unique={};reviews=[];checks=[]
 for r in catalog['keys']:
  view,k=r['view'],r['stage'];p=art/f'{view}-throw-{k:02}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_stem(p.stem+'-player-mask'));roles=Image.open(p.with_stem(p.stem+'-paint-roles'))
  assert im.size==m.size==roles.size==(100,78);assert set(im.getchannel('A').get_flattened_data())<={0,255};assert set(m.get_flattened_data())<={0,255};assert sha(p)==r['sha256']
  pts={(x,y)for y in range(78)for x in range(100)if im.getpixel((x,y))[3]};assert len(components(pts))==1;assert all(30<x<77 and 8<y<44 for x,y in pts)
  assert all(im.getpixel((x,y))[3]or not m.getpixel((x,y))for y in range(78)for x in range(100))
  palette|={im.getpixel(p)for p in pts};blue=[p for p in pts if(lambda c:c[2]>120 and c[2]>c[0]*1.4)(im.getpixel(p))];assert len(blue)>=7;assert all(m.getpixel(p)==0 for p in blue)
  if view=='E':continue
  unique.setdefault(hashlib.sha256(im.tobytes()).hexdigest(),[]).append(k)
  t=r['parts'][-1];assert t['kneeling']==(10<=k<=17);assert t['release']==(k==14);assert t['grenade_held']==(k<14);assert t['grenade_visible']==(k<14 and k!=1)
  for limb in r['parts'][:2]:
   hip,knee,ankle=limb['hip'],limb['knee'],limb['ankle'];assert max(hypot(knee[0]-hip[0],knee[1]-hip[1]),hypot(ankle[0]-knee[0],ankle[1]-knee[1]))<=4.8
  if 10<=k<=17:
   far,near=r['parts'][:2];assert far['hip']==[9,17]and far['knee']==[6,20.5]and far['ankle']==[2.5,19.5]
   assert near['hip']==[6,18]and near['knee']==[10.5,18.7]and near['ankle']==[11,23.3]
   assert far['support_kind']=='grounded knee'and near['support_kind']=='forward planted foot'
  r['review']='inspected; '+state;reviews.append(dict(view='SE',stage=k,sha256=sha(p),observation=NOTES[k],evidence=f'SE-five-frame-audit-{k//5}.png',status='viewed; '+state));checks.append(dict(view=view,stage=k,sha256=sha(p),bounds=im.getbbox(),blue_pixels=len(blue),connected=True))
 assert len(palette)<=32;assert len(unique)==17;assert[x for x in unique.values()if len(x)>1]==[[1,19],[2,4],[7,8]]
 for suffix in('','-player-mask','-paint-roles'):assert(art/f'SE-ready{suffix}.png').read_bytes()==(art/f'SE-throw-00{suffix}.png').read_bytes()
 board=Image.new('RGB',(1000,440),(25,28,33));d=ImageDraw.Draw(board)
 for k in range(20):
  for ci,color in enumerate(((180,40,35),(45,100,215))):
   p=art/f'SE-throw-{k:02}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_stem(p.stem+'-player-mask'))
   for y in range(78):
    for x in range(100):
     if m.getpixel((x,y)):
      r,g,b,alpha=im.getpixel((x,y));lum=(r*.3+g*.6+b*.1)/155;im.putpixel((x,y),tuple(min(255,round(c*lum))for c in color)+(alpha,))
   tile=im.crop((32,6,76,48)).resize((88,84),Image.Resampling.NEAREST);board.paste(tile,(k%5*200+ci*95+5,k//5*110+20),tile)
  d.text((k%5*200+5,k//5*110+3),str(k)+' / red + blue',fill='white')
 board.save(art/'SE-player-colour-preview.png')
 (art/'personal-frame-review.json').write_text(json.dumps(dict(date='2026-10-21',scope='20SEthrow cells audited; '+state+'. Exact accepted20East cells/ready preserved separately.',frames=reviews,continuity=CONTINUITY),indent=2)+'\n')
 catalog['scope']='Exact accepted East20 plus inspected SE20; '+state;(art/'frames.json').write_text(json.dumps(catalog,indent=2)+'\n')
 (art/'technical-checks.json').write_text(json.dumps(dict(kind='Technical checks do not confer design-review artistic acceptance',new_SE_cells=20,SE_unique=17,SE_exact_holds=[[1,19],[2,4],[7,8]],fps=15,release_stage=14,anchor=[54,40],palette_size=len(palette),approved_East_preserved='21image/mask/role triplets',approved_run_preserved=48,protected_E1=PROTECTED,SE_frames=checks),indent=2)+'\n')
 print('20SE cells connected;17unique/three source holds;27colours;20personal observations;accepted E21/run48/E1unchanged')

if __name__=='__main__':main()
