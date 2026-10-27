#!/usr/bin/env python3
"""Technical checks and bindings for actual inspected v40 cells."""
from pathlib import Path
import argparse,json,hashlib
from PIL import Image,ImageDraw
from verify_e2_throw_v33 import components,sha,PROTECTED
from e2_throw_study_v20 import HOLDS,DROPS,representative
NEW=('N','NE')
ROOT=Path(__file__).resolve().parents[2]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--protected-root',type=Path,required=True);a=ap.parse_args()
 art=ROOT/'art/sprites/infantry-snes-v01/motion-study-v49-e2-throw'
 accepted=ROOT/'art/sprites/infantry-snes-v01/motion-study-v37-e2-throw'
 for file,h in PROTECTED.items():assert sha(a.protected_root/file)==h
 for folder in(accepted,ROOT/'art/sprites/infantry-snes-v01/motion-study-v28-e2-run'):
  receipt=json.loads((folder/'approval-2026-10-21.json').read_text())
  for group in('accepted','paired_mask_hashes','paired_role_hashes'):
   for file,h in receipt[group].items():
    assert sha(folder/file)==h
    if folder==accepted:assert sha(art/file)==h
 notes=json.loads((art/'personal-observations-input.json').read_text());catalog=json.loads((art/'frames.json').read_text());reviews=[];checks=[];palette=set();unique={v:{}for v in NEW}
 for r in catalog['keys']:
  v,k=r['view'],r['stage'];p=art/f'{v}-throw-{k:02}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_stem(p.stem+'-player-mask'));roles=Image.open(p.with_stem(p.stem+'-paint-roles'))
  assert im.size==m.size==roles.size==(100,78);assert sha(p)==r['sha256'];assert set(im.getchannel('A').get_flattened_data())<={0,255};assert set(m.get_flattened_data())<={0,255}
  pts={(x,y)for y in range(78)for x in range(100)if im.getpixel((x,y))[3]};assert len(components(pts))==1,(v,k,'disconnected');assert all(30<x<77 and 8<y<45 for x,y in pts)
  assert all(im.getpixel((x,y))[3]or not m.getpixel((x,y))for y in range(78)for x in range(100));palette|={im.getpixel(p)for p in pts}
  blue=[p for p in pts if(lambda c:c[2]>120 and c[2]>c[0]*1.4)(im.getpixel(p))];assert len(blue)>=7,(v,k,len(blue));assert all(m.getpixel(p)==0 for p in blue)
  if v not in NEW:continue
  unique[v].setdefault(hashlib.sha256(im.tobytes()).hexdigest(),[]).append(k);t=r['parts'][-1];assert t['kneeling']==(DROPS[v][representative(v,k)]==3);assert t['release']==(k==14);assert t['grenade_held']==(k<14);assert t['actor_anchor']==[54,40]
  assert all(max(l['projected_segments'])<=4.8 for l in r['parts'][:2])
  r['review']='inspected; acceptance pending';reviews.append(dict(view=v,stage=k,sha256=sha(p),observation=notes[v][k],evidence=f'{v}-five-frame-audit-{k//5}.png',status='viewed; acceptance pending'));checks.append(dict(view=v,stage=k,bounds=im.getbbox(),blue_pixels=len(blue),connected=True))
 previous=ROOT/'art/sprites/infantry-snes-v01/motion-study-v46-e2-throw'
 for view in ('NW','W','SW','S','SE','E'):
  for name in [*[f'{view}-throw-{k:02}'for k in range(20)],f'{view}-ready']:
   for suffix in('','-player-mask','-paint-roles'):assert sha(art/(name+suffix+'.png'))==sha(previous/(name+suffix+'.png'))
 for r in catalog['keys']:
  if r['view']in NEW:
   t=r['parts'][-1];assert t['anatomical_throw_arm']=='right'and t['anatomical_balance_arm']=='left'
   if r['stage']==14:assert t['wrist'][0]==(16 if r['view']=='N'else 2) and t['throwing_shoulder'][0]==(11 if r['view']=='N'else 10) and t['balance_shoulder'][0]==4
 for v in NEW:
  holds=[s for s in unique[v].values()if len(s)>1];assert holds==HOLDS[v],(v,holds)
  for suffix in('','-player-mask','-paint-roles'):assert(art/f'{v}-ready{suffix}.png').read_bytes()==(art/f'{v}-throw-00{suffix}.png').read_bytes()
  board=Image.new('RGB',(1000,440),(25,28,33));d=ImageDraw.Draw(board)
  for k in range(20):
   for ci,col in enumerate(((180,40,35),(45,100,215))):
    p=art/f'{v}-throw-{k:02}.png';im=Image.open(p).convert('RGBA');m=Image.open(p.with_stem(p.stem+'-player-mask'))
    for y in range(78):
     for x in range(100):
      if m.getpixel((x,y)):
       r,g,b,alpha=im.getpixel((x,y));lum=(r*.3+g*.6+b*.1)/155;im.putpixel((x,y),tuple(min(255,round(c*lum))for c in col)+(alpha,))
    tile=im.crop((32,6,76,48)).resize((88,84),Image.Resampling.NEAREST);board.paste(tile,(k%5*200+ci*95+5,k//5*110+20),tile)
   d.text((k%5*200+5,k//5*110+3),str(k)+' / red + blue',fill='white')
  board.save(art/f'{v}-player-colour-preview.png')
  b=Image.new('RGB',(1056,275),(25,28,33));d=ImageDraw.Draw(b)
  for col,k in enumerate((16,17,18,19,'ready',0)):
   im=Image.open(art/(f'{v}-ready.png'if k=='ready'else f'{v}-throw-{k:02}.png')).convert('RGBA').crop((32,6,76,48)).resize((176,168),Image.Resampling.NEAREST);b.paste(im,(col*176,60),im);d.text((col*176+12,20),f'{v} / {k}',fill='white')
  b.save(art/f'{v}-completion-ready-entry-audit.png')
 assert len(palette)<=32
 (art/'personal-frame-review.json').write_text(json.dumps(dict(date='2026-10-21',scope='40 corrected N/NEcells viewed; six other views exact v46copies, E/SEaccepted frozen',frames=reviews,continuity={v:['Preparation holds checked against whole source strip and exact repetition groups.','Descent follows camera-specific timing; knee/front-foot support stays fixed through release.','Head, collar, chest and blue equipment inspected during raised-arm occlusion and body dip.','Release14 has empty hand; projectile belongs to engine.','Follow-through, knee rise and next-ready entry inspected at native scale.']for v in NEW}),indent=2)+'\n')
 (art/'frames.json').write_text(json.dumps(catalog,indent=2)+'\n')
 (art/'technical-checks.json').write_text(json.dumps(dict(new_cells=40,palette_size=len(palette),distinct={v:len(unique[v])for v in NEW},exact_holds=HOLDS,anchor=[54,40],fps=15,release_stage=14,accepted_E_SE_preserved=42,accepted_run_preserved=48,protected_E1=PROTECTED,frames=checks,artistic_acceptance='N/NEcorrections pending; NW46review pending; W/SW/Spending; E/SEaccepted'),indent=2)+'\n')
 print('40N/NEcells verified; source holds exact; six v46cameras, E/SE42,run48,E1 preserved; palette',len(palette))
if __name__=='__main__':main()
