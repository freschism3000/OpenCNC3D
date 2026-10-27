#!/usr/bin/env python3
"""Validate new North art separately from personal and design-review visual review."""
from pathlib import Path
import argparse,json,hashlib
from math import hypot
from PIL import Image,ImageDraw
from verify_e2_throw_v33 import components,sha,PROTECTED
from e2_run_study_v28 import HEAD_X
from e2_throw_study_v22 import KNEEL,HOLDS,ROOT
NOTES=[
'North upright ready has fitted rear helmet, broad pack planes and full blue waist band. Right hand is hidden in front of the torso; near left arm gathers at the side.',
'One-pixel compression keeps head, neck, belt and short legs together; hands obey body occlusion rather than skin pasted onto the back.',
'Lowering advances the away/front foot left-up while right knee folds below its hip. Both trouser forms stay joined to the pelvis.',
'Same descent depth as2 with a different balancing-hand gather; no invented source hold or duplicate missing-motion frame.',
'Full knee support is established. Away/front thigh angles left-up into a short shin/foot, right knee grounds lower-right and its trailing calf folds down-inward to the visible sole.',
'Kneeling gathering holds the same contacts with left balancing hand close to the waist and right hand hidden beyond torso. Blue equipment spans the waist.',
'Exact stage5 hold as original5..9pause; unchanged grounded knee, front foot, body and head are intentional timing.',
'Exact stage5 hold; rear boot remains a folded/trailing sole rather than a diagonal standing leg.',
'Exact stage5 hold; no arm or helmet jitter added to fill the source pause.',
'Exact stage5 hold completes the original preparation pause before the new physical overhand gesture.',
'Head and upper torso turn slightly NW together during the cock, retaining rear planes. Right elbow opens outside its own shoulder; hand/held grenade rise beside the right crown with a visible bent arm.',
'Right forearm raises above-right of helmet with elbow still outside shoulder. Left hand stays low for balance. Grenade is connected to the hand, separate from helmet highlights.',
'Anticipation draws the right hand outward/back a little, preserving compact upper/forearm lengths and the same shoulder. Helmet/neck fit and grounded supports remain intact.',
'Exact stage11 source hold: raised right arm and quarter-turned rear torso repeat together; no screen-side swap or alternate throwing hand.',
'Empty right hand passes over the crown at release14, body turns back North. Hand/head occlusion is real: no final skin pixel is pasted through the helmet. Same knee/foot contacts stay grounded.',
'Right forearm projects over/away beyond helmet during follow-through. Torso uses a deliberately compressed one-pixel pose, head stays fitted, and the helmet naturally hides part of the away reach.',
'Forward body fold uses explicit compact back rows. Right forearm/hand disappear beyond head/torso rather than forming a horizontal bar through them. Blue band remains full height; knee and front foot stay fixed.',
'Upper recovery unfolds one pixel while knee remains down. Right empty hand is occluded in front of body, left hand balances low; no detached forearm reappears across pack.',
'Push-up raises pelvis, back and head together as both leg shapes narrow toward the standing stance. Rear sole remains connected to its calf through the transfer.',
'Exact stage1 compression/recovery alias as in original1/19. Right hand is empty and occluded; next ready returns to upright without a visible post-release grenade or body slide.'
]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--protected-root',type=Path,required=True);a=ap.parse_args();art=ROOT/'art/sprites/infantry-snes-v01/motion-study-v52-e2-throw';old=ROOT/'art/sprites/infantry-snes-v01/motion-study-v49-e2-throw'
 for p,h in PROTECTED.items():assert sha(a.protected_root/p)==h
 for folder in(ROOT/'art/sprites/infantry-snes-v01/motion-study-v37-e2-throw',ROOT/'art/sprites/infantry-snes-v01/motion-study-v28-e2-run'):
  receipt=json.loads((folder/'approval-2026-10-21.json').read_text())
  for group in('accepted','paired_mask_hashes','paired_role_hashes'):
   for p,h in receipt[group].items():
    assert sha(folder/p)==h
    if 'v37' in str(folder):assert sha(art/p)==h
 for v in('NW','W','SW','S','SE','E','NE'):
  for name in [*[f'{v}-throw-{k:02}'for k in range(20)],f'{v}-ready']:
   for suffix in('','-player-mask','-paint-roles'):assert sha(art/(name+suffix+'.png'))==sha(old/(name+suffix+'.png'))
 catalog=json.loads((art/'frames.json').read_text());palette=set();unique={};reviews=[];checks=[]
 for r in catalog['keys']:
  v,k=r['view'],r['stage'];p=art/f'{v}-throw-{k:02}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_stem(p.stem+'-player-mask'));roles=Image.open(p.with_stem(p.stem+'-paint-roles'));assert im.size==m.size==roles.size==(100,78);assert sha(p)==r['sha256'];assert set(im.getchannel('A').get_flattened_data())<={0,255};assert set(m.get_flattened_data())<={0,255}
  pts={(x,y)for y in range(78)for x in range(100)if im.getpixel((x,y))[3]};palette|={im.getpixel(p)for p in pts}
  if v!='N':continue
  assert len(components(pts))==1;assert all(30<x<77 and 8<y<45 for x,y in pts);assert all(im.getpixel((x,y))[3]or not(m.getpixel((x,y))or roles.getpixel((x,y)))for y in range(78)for x in range(100))
  blue=[p for p in pts if(lambda c:c[2]>120 and c[2]>c[0]*1.4)(im.getpixel(p))];assert len(blue)>=10;assert all(not m.getpixel(p)for p in blue)
  t=r['parts'][-1];assert t['actor_anchor']==[54,40]and t['release']==(k==14)and t['grenade_held']==(k<14);assert t['kneeling']==(4<=k<=17);assert t['head_camera']==('NW'if 10<=k<=13 else'N')
  assert all(max(l['projected_segments'])<=4.8 for l in r['parts'][:2]);assert all(hypot(a[0]-b[0],a[1]-b[1])<=5.2 for a,b in zip(t['right_chain'],t['right_chain'][1:]));assert t['right_chain'][0][0]>=10 and t['left_chain'][0][0]<=4
  if 4<=k<=17:
   for j,l in enumerate(r['parts'][:2]):
    expected=KNEEL[1-j];assert l['hip']==list(expected[0])and l['knee']==list(expected[1])and l['ankle']==list(expected[2])
  # Match every opaque approved head-component pixel after final compositing.
  master=Image.open(ROOT/f"art/sprites/minigunner-snes-v01/animation-v06/native-parts/{t['head_camera']}-complete-master.png").convert('RGBA').crop((0,0,15,6));box=master.getbbox();h=master.crop((box[0],0,box[2],6))
  for y in range(6):
   for x in range(h.width):
    c=h.getpixel((x,y))
    if c[3]:assert im.getpixel((44+HEAD_X[t['head_camera']]+x,16+t['drop']+t['dip']+y))==c,('head overwritten',k,x,y)
  unique.setdefault(hashlib.sha256(im.tobytes()).hexdigest(),[]).append(k);r['review']='inspected; design-review review pending';reviews.append(dict(view='N',stage=k,sha256=sha(p),observation=NOTES[k],evidence=f'N-five-frame-audit-{k//5}.png',status='viewed; design-review review pending'));checks.append(dict(stage=k,bounds=im.getbbox(),blue_pixels=len(blue),head_component_exact=True,connected=True))
 assert len(palette)<=32 and len(unique)==14;assert[g for g in unique.values()if len(g)>1]==HOLDS['N']
 for suffix in('','-player-mask','-paint-roles'):assert sha(art/f'N-ready{suffix}.png')==sha(art/f'N-throw-00{suffix}.png')
 board=Image.new('RGB',(1000,440),(25,28,33));d=ImageDraw.Draw(board)
 for k in range(20):
  for ci,color in enumerate(((180,40,35),(45,100,215))):
   p=art/f'N-throw-{k:02}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_stem(p.stem+'-player-mask'))
   for y in range(78):
    for x in range(100):
     if m.getpixel((x,y)):
      rr,g,b,alpha=im.getpixel((x,y));lum=(rr*.3+g*.6+b*.1)/155;im.putpixel((x,y),tuple(min(255,round(c*lum))for c in color)+(alpha,))
   tile=im.crop((32,6,76,48)).resize((88,84),Image.Resampling.NEAREST);board.paste(tile,(k%5*200+ci*95+5,k//5*110+20),tile)
  d.text((k%5*200+5,k//5*110+3),str(k)+' / red + blue',fill='white')
 board.save(art/'N-player-colour-preview.png');b=Image.new('RGB',(1056,275),(25,28,33));d=ImageDraw.Draw(b)
 for col,k in enumerate((16,17,18,19,'ready',0)):
  im=Image.open(art/('N-ready.png'if k=='ready'else f'N-throw-{k:02}.png')).convert('RGBA').crop((32,6,76,48)).resize((176,168),Image.Resampling.NEAREST);b.paste(im,(col*176,60),im);d.text((col*176+12,20),f'N / {k}',fill='white')
 b.save(art/'N-completion-ready-entry-audit.png')
 (art/'personal-frame-review.json').write_text(json.dumps(dict(date='2026-10-21',scope='20 new North poses inspected; other147action/ready camera triplets exact v49copies',frames=reviews,continuity=['0..4descent establishes low contacts;5..9source hold retains them.','10..14quarter-turn, raised outside elbow, overhead empty release form a complete new overhand gesture rather than DOSskin-coordinate fitting.','15..17away reach passes above/beyond head; actual head occlusion hides hand and forearm instead of pasting them through helmet.','18..19and ready retain connected folded legs through push-up. Native1x/2x, silhouettes and two player colours checked.']),indent=2)+'\n');(art/'frames.json').write_text(json.dumps(catalog,indent=2)+'\n');(art/'technical-checks.json').write_text(json.dumps(dict(new_cells=20,palette_size=len(palette),distinct=14,exact_holds=HOLDS['N'],fps=15,release_stage=14,anchor=[54,40],other_camera_triplets_exact=147,accepted_E_SE_triplets=42,accepted_run_frames=48,protected_E1=PROTECTED,North_frames=checks,artistic_review='Pending design-review review; technical checks do not confer acceptance'),indent=2)+'\n');print('North20;14distinct;exact holds;fitted head pixels;fixed knee supports;other147triplets/ESE/run/E1 exact; palette',len(palette))
if __name__=='__main__':main()
