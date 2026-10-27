#!/usr/bin/env python3
"""New North overhand gesture; DOS enters evidence only after native painting."""
from pathlib import Path
import argparse,hashlib,json,shutil
from PIL import Image,ImageDraw,ImageFont
from e2_run_study_v28 import Camera,TORSO,ROOT,px
from e2_throw_study_v20 import head,HOLDS,DROPS,representative
VIEWS=('N','NW','W','SW','S','SE','E','NE');NEW=('N',)
STAND=(((6,15),(6,19),(6,23)),((10,15),(10,19),(10,23)))
KNEEL=(((6,18),(4,17.5),(3.5,20)),((10,18),(11,21),(9,23.5)))
# Newly keyed physical motion, not tiny DOS skin-pixel coordinates. The right
# elbow stays outside its shoulder in the wind-up; hand passes ABOVE the head
# then disappears beyond it on the away throw. The left arm balances below.
RIGHT=[((10,7),(11,10),(9,11)),((10,7),(11,9),(9,10)),
       ((10,7),(11,8),(9,9)),((10,7),(12,8),(10,9)),((10,7),(11,9),(9,11)),
       ((10,7),(11,10),(9,11)),((10,7),(11,10),(9,11)),((10,7),(11,10),(9,11)),((10,7),(11,10),(9,11)),((10,7),(11,10),(9,11)),
       ((10,7),(12,7),(12,3)),((10,7),(12,5),(12,0)),((10,7),(12,5),(14,1)),((10,7),(12,5),(12,0)),
       ((11,7),(12,3),(11,-2)),((11,7),(11,3),(8,-1)),((11,7),(10,5),(8,8)),((11,7),(11,9),(9,11)),
       ((11,7),(11,10),(9,12)),((10,7),(11,9),(9,10))]
LEFT=[(3,11),(3,10),(2,10),(2,9),(2,11),(3,12),(3,12),(3,12),(3,12),(3,12),
      (2,12),(1,12),(0,11),(1,12),(1,11),(2,12),(3,12),(3,12),(3,11),(3,10)]
BEND={1:[(4,7,'65g6654g1'),(4,8,'54g5543g1'),(4,9,'4g5443g31'),(4,10,'3g4332g31'),(4,11,'3g3322g31'),(4,12,'1hihihh1'),(4,13,'hihhhih1'),(4,14,'36545431')],
      2:[(4,8,'65g6654g1'),(4,9,'54g5543g1'),(4,10,'4g4432g31'),(4,11,'3g3322g31'),(4,12,'1hihihh1'),(4,13,'hihhhih1'),(4,14,'36545431')]}
class North(Camera):
 def boot(self,x,y,view,far):
  if self.kneeling and far:self.rows(px(x)-1,px(y)-1,['sr','tr','rr'])
  else:super().boot(x,y,view,far)

def frame(stage,ready=False):
 i=0 if ready else representative('N',stage);drop=DROPS['N'][i];dip=2 if i==16 else 1 if i in(15,17)else 0;turn=10<=i<=13;heading='NW'if turn else'N';s=North();s.kneeling=drop==3;t=drop/3
 legs=tuple(tuple(tuple(a+(b-a)*t for a,b in zip(pa,pb))for pa,pb in zip(la,lb))for la,lb in zip(STAND,KNEEL))
 for leg in(1,0):
  s.leg_key(legs[leg],'N',leg==1,True);s.trace[-1]['support_kind']='grounded knee'if leg==1 and drop==3 else'forward planted foot'if leg==0 else'foot / lowering'
 shift=lambda p:(p[0],p[1]+drop+dip)
 left=((3,8)if turn else(4,7),(2,10)if turn else(2,9),LEFT[i]);right=RIGHT[i]
 # Hands are painted with their actual limbs, before head/body occlusion. Never
 # paste a hand over the final helmet to make its source pixel visible.
 s.arm(*map(shift,left),far=False);s.arm(*map(shift,right),far=True)
 for x,y,row in (BEND[dip]if dip else TORSO[heading]):s.rows(x,y+drop,[row])
 s.rows(4,12+drop,['nopppqon','noppopon'])
 active_exposed=10<=i<=15
 if active_exposed:
  shoulder,elbow,wrist=map(shift,right);s.cloth(shoulder,elbow,3.5,True,('6753','6753','5642','4532'));s.cloth(elbow,wrist,3.0,True)
  s.put(*wrist,'b');s.put(wrist[0]+1,wrist[1],'c');s.put(wrist[0]+1,wrist[1]+1,'a')
 held=stage<14 or ready;prop=held and 10<=i<=13
 if prop:
  gx,gy=shift(right[-1]);s.rows(gx-1,gy-2,['.m.','wvu','.u.'])
 head(s,heading,drop,0,dip)
 s.trace.append(dict(part='throw',view='N',stage=stage,pose_index=i,drop=drop,dip=dip,head_camera=heading,torso_camera=heading,kneeling=drop==3,release=stage==14 and not ready,grenade_held=held,grenade_visible=prop,actor_anchor=[54,40],anatomical_throw_arm='right',anatomical_balance_arm='left',right_chain=list(map(shift,right)),left_chain=list(map(shift,left)),hand_layer='before head; no forced skin pixels on helmet',active_arm_exposed=active_exposed))
 return s

def evidence(out,study):
 original=Image.open(study/'E2/original.png').convert('RGBA');font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
 for view in NEW:
  gif=[]
  for group in range(4):
   board=Image.new('RGB',(1200,610),(25,28,33));d=ImageDraw.Draw(board)
   for col,k in enumerate(range(group*5,group*5+5)):
    n=64+VIEWS.index(view)*20+k;old=original.crop((n%20*50,n//20*39,n%20*50+50,n//20*39+39)).resize((100,78),Image.Resampling.NEAREST);new=Image.open(out/f'{view}-throw-{k:02}.png').convert('RGBA');pair=Image.new('RGB',(800,410),(25,28,33));pd=ImageDraw.Draw(pair)
    for row,im in enumerate((old,new)):
     tile=im.crop((32,6,76,48)).resize((220,210),Image.Resampling.NEAREST);board.paste(tile,(col*240+10,row*300+45),tile);d.text((col*240+8,row*300+12),f'{view} / {k:02} / '+('DOS'if row==0 else'NEW52'),font=font,fill='#e7bd70');tile=im.crop((32,6,76,48)).resize((308,294),Image.Resampling.NEAREST);pair.paste(tile,(row*400+44,45),tile);pd.text((row*400+20,15),view+' / '+('DOS'if row==0 else'NEW52')+f' / {k+1}:20',font=font,fill='#e7bd70')
    gif.append(pair)
   board.save(out/f'{view}-five-frame-audit-{group}.png')
  gif[0].save(out/f'{view}-throw-comparison.gif',save_all=True,append_images=gif[1:],duration=[70,60,70]*6+[70,60],loop=0,disposal=2)
  board=Image.new('RGB',(1200,570),(25,28,33));d=ImageDraw.Draw(board)
  for k in range(20):
   im=Image.open(out/f'{view}-throw-{k:02}.png').convert('RGBA').crop((32,6,76,48))
   for scale,y in((1,30),(2,95)):
    tile=im.resize((im.width*scale,im.height*scale),Image.Resampling.NEAREST);board.paste(tile,(k%10*120+10,k//10*270+y),tile)
   sil=Image.new('RGBA',im.size,(235,235,225));sil.putalpha(im.getchannel('A'));board.paste(sil,(k%10*120+10,k//10*270+205),sil);d.text((k%10*120+10,k//10*270+5),str(k),font=font,fill='#aeb8bf')
  board.save(out/f'{view}-native-distance-check.png')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);ap.add_argument('--analysis',type=Path,required=True);a=ap.parse_args();brief=json.loads(a.analysis.read_text());assert brief['before_new_art']
 if a.out.exists():raise RuntimeError('Versioned output required')
 a.out.mkdir(parents=True);previous=ROOT/'art/sprites/infantry-snes-v01/motion-study-v49-e2-throw'
 for p in previous.glob('*.png'):
  if p.name.startswith(tuple(v+'-'for v in VIEWS if v!='N')):shutil.copy2(p,a.out/p.name)
 records=[r for r in json.loads((previous/'frames.json').read_text())['keys']if r['view']!='N']
 for k in range(20):
  s=frame(k);p=a.out/f'N-throw-{k:02}.png';s.im.save(p);s.mask.save(p.with_stem(p.stem+'-player-mask'));s.roles.save(p.with_stem(p.stem+'-paint-roles'));records.append(dict(view='N',stage=k,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),parts=s.trace,review='UNREVIEWED'))
 s=frame(0,ready=True);s.im.save(a.out/'N-ready.png');s.mask.save(a.out/'N-ready-player-mask.png');s.roles.save(a.out/'N-ready-paint-roles.png')
 (a.out/'frames.json').write_text(json.dumps(dict(scope='Independent North whole-action rebuild; other147camera action/ready triplets exact v49copies',anchor=[54,40],fps=15,release_stage=14,keys=records),indent=2)+'\n');shutil.copy2(a.analysis,a.out/'source-action-brief.json');evidence(a.out,a.study)
 author=a.out/'authoring-source';author.mkdir()
 for name in('e2_throw_study_v22.py','e2_throw_study_v20.py','e2_run_study_v28.py','e2_run_study_v23.py','e2_run_study_v15.py'):shutil.copy2(Path(__file__).with_name(name),author/name)
 print(a.out)
if __name__=='__main__':main()
