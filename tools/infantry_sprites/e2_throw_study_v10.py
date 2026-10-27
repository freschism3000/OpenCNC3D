#!/usr/bin/env python3
"""Independent SE kneeling choreography plus exact accepted East throw copies.

The whole original action/support/holds are read before drawing. Original DOS
pixels enter evidence only. No DOS joint fitting, raster warps or sprite flips.
"""
from pathlib import Path
import argparse,hashlib,json,shutil
from PIL import Image,ImageDraw,ImageFont
from e2_run_study_v23 import Native,ROOT,TORSO
from e2_run_study_v15 import px

# Drop, lean, dip, near elbow, near wrist, balancing hand. Independent quarter
# camera keys, not a projection/flip of the East output bitmap.
KEYS=[
 (0,0,0,(5,11),(10,12),(11,10)),
 (1,0,0,(6,10),(9,10),(10,10)),
 (0,0,0,(5,10),(10,11),(11,10)),
 (0,0,0,(6,10),(11,11),(11,11)),
 (0,0,0,(5,10),(10,11),(11,10)),
 (1,0,0,(5,10),(9,11),(10,11)),
 (0,0,0,(4,11),(8,12),(11,11)),
 (0,0,0,(5,10),(9,10),(11,12)),
 (0,0,0,(5,10),(9,10),(11,12)),
 (2,0,0,(6,10),(9,9),(11,13)),
 (3,-1,0,(6,7),(10,6),(11,14)),
 (3,-1,0,(7,6),(11,3),(11,14)),
 (3,-1,0,(8,5),(11,2),(11,14)),
 (3,-1,0,(7,4),(10,-1),(11,14)),
 (3,0,0,(1,4),(1,-1),(11,14)),
 (3,1,0,(8,5),(12,2),(11,13)),
 (3,2,2,(8,11),(11,14),(11,12)),
 (3,1,1,(6,11),(9,13),(10,12)),
 (2,0,0,(5,11),(9,12),(10,11)),
 (1,0,0,(6,10),(9,10),(10,10)),
]
assert KEYS[1]==KEYS[19]and KEYS[2]==KEYS[4]and KEYS[7]==KEYS[8]
PHASES=['prepare']*9+['lower / brace','draw back / knee down','cock / knee down',
 'cock / knee down','raise / knee down','release / knee down','extend / knee down',
 'follow through / knee down','upper recovery / knee down','push up','recover']
KNEEL_NEAR=((6,18),(10.5,18.7),(11,23.3))
KNEEL_FAR=((9,17),(6,20.5),(2.5,19.5))
WINDUP=[(5,6,'65g4321'),(3,7,'6yg443g31'),(3,8,'65g444g431'),
 (3,9,'54g443g431'),(3,10,'43g43g431'),(4,11,'3g33g431'),
 (4,12,'1hhhihh1'),(4,13,'hihhhih1'),(4,14,'36545431')]

def fitted_head(s,drop,lean,dip):
 h=Image.open(ROOT/'art/sprites/minigunner-snes-v01/animation-v06/native-parts/SE-complete-master.png').convert('RGBA')
 paint={tuple(bytes.fromhex(v))for v in('4b371f','6e502a','967438','b78e43','cca856','e3bf73','f2d794','fff1ad')}
 for y in range(6):
  for x in range(h.width):
   c=h.getpixel((x,y))
   if c[3]:
    p=(45+x+lean,16+y+drop+dip);s.im.putpixel(p,c)
    s.mask.putpixel(p,255 if c[:3]in paint else 0);s.roles.putpixel(p,4 if c[:3]in paint else 0)

def frame_se(stage,ready=False):
 s=Native();drop,lean,dip,elbow,wrist,free=KEYS[0 if ready else stage]
 near=((6,15+drop),(6.3,19),(6.5,23));far=((9,14+drop),(9.5,18),(10,22))
 if not ready:
  if stage==9:near,far=((6,17),(8,19),(8.5,23)),((9,16),(8.5,20),(6,20.5))
  elif 10<=stage<=17:near,far=KNEEL_NEAR,KNEEL_FAR
  elif stage==18:near,far=((6,17),(8,19),(8.5,23)),((9,16),(8,19),(7,21))
 kneeling=10<=stage<=17 and not ready
 s.leg_key(far,'SE',True,True);s.trace[-1]['support_kind']='grounded knee'if kneeling else'foot / lowering'
 s.leg_key(near,'SE',False,True);s.trace[-1]['support_kind']='forward planted foot'
 sx=lambda y:px(lean*(14-y)/8)
 sy=lambda y:drop+px(dip*(14-y)/8)
 arm=lambda p:(p[0]+sx(7),p[1]+sy(7))
 # Far sleeve/hand remains behind the chest. Rear gear overlaps the torso,
 # preserving the accepted saturated blue outer face at the screen-left waist.
 s.arm(*map(arm,((10,7),(12,10),free)),far=True)
 for y,row in enumerate(['hih1','iih1','ihh1','ihh1','opn1','pqn1','pon1','oon1'],6):s.rows(2+sx(y),y+sy(y),[row])
 for x,y,row in WINDUP if 10<=stage<=13 and not ready else TORSO['SE']:s.rows(x+sx(y),y+sy(y),[row])
 for y,row in enumerate(['opn','pqn','pon','onn'],11):s.rows(1+sx(y),y+sy(y),[row])
 s.arm(*map(arm,((4,8),elbow,wrist)))
 # The head is in front of the sleeve as it passes behind the helmet. The
 # overhead prop is painted after the head so it cannot disappear in that layer.
 fitted_head(s,drop,lean,dip)
 if 11<=stage<=16 and not ready:
  s.cloth(arm(elbow),arm(wrist),3.3)
  wx,wy=arm(wrist);s.put(wx,wy,'b');s.put(wx+1,wy,'c');s.put(wx+1,wy+1,'a')
 held=stage<14 or ready
 visible=held and(stage!=1 or ready)
 if visible:
  gx,gy=arm(wrist);s.rows(gx-1,gy-2,['.m.','wvu','.u.'])
 s.trace.append(dict(part='throw',view='SE',stage=stage,phase='ready'if ready else PHASES[stage],
  wrist=arm(wrist),grenade_held=held,grenade_visible=visible,release=stage==14 and not ready,
  drop=drop,lean=lean,dip=dip,kneeling=kneeling,actor_anchor=[54,40],
  ground_support='far knee/up-left plane and near forward foot/down-right plane'if kneeling else'standing / lowering / rising'))
 return s

def before(source,view,stage):
 n=(184 if view=='E'else 164)+stage
 return source.crop((n%20*50,n//20*39,n%20*50+50,n//20*39+39)).resize((100,78),Image.Resampling.NEAREST)

def evidence(out,study):
 source=Image.open(study/'E2/original.png').convert('RGBA');font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',15);gifs=[]
 for group in range(4):
  board=Image.new('RGB',(1200,610),(25,28,33));d=ImageDraw.Draw(board)
  for col,k in enumerate(range(group*5,group*5+5)):
   for row,im in enumerate((before(source,'SE',k),Image.open(out/f'SE-throw-{k:02}.png').convert('RGBA'))):
    tile=im.crop((32,6,76,48)).resize((220,210),Image.Resampling.NEAREST);board.paste(tile,(col*240+10,row*300+45),tile)
    d.text((col*240+8,row*300+10),f'{k:02} / '+('DOS'if row==0 else'NEW SE')+' / '+PHASES[k].split(' / ')[0],font=font,fill='#e7bd70')
  board.save(out/f'SE-five-frame-audit-{group}.png')
 for k in range(20):
  pair=Image.new('RGB',(800,680),(25,28,33));d=ImageDraw.Draw(pair)
  for vi,view in enumerate(('E','SE')):
   for col,im in enumerate((before(source,view,k),Image.open(out/f'{view}-throw-{k:02}.png').convert('RGBA'))):
    tile=im.crop((32,6,76,48)).resize((264,252),Image.Resampling.NEAREST);pair.paste(tile,(col*400+68,vi*330+45),tile)
    label='DOS'if col==0 else'APPROVED E33'if view=='E'else'NEW SE36'
    d.text((col*400+20,vi*330+15),view+' / '+label+f' / {k+1}:20',font=font,fill='#e7bd70')
  gifs.append(pair)
 gifs[0].save(out/'east-southeast-throw-comparison.gif',save_all=True,append_images=gifs[1:],duration=[70,60,70]*6+[70,60],loop=0,disposal=2)
 board=Image.new('RGB',(1200,570),(25,28,33));d=ImageDraw.Draw(board)
 for i in range(20):
  im=Image.open(out/f'SE-throw-{i:02}.png').convert('RGBA').crop((32,6,76,48))
  for scale,y in((1,30),(2,95)):
   tile=im.resize((im.width*scale,im.height*scale),Image.Resampling.NEAREST);board.paste(tile,(i%10*120+10,i//10*270+y),tile)
  silhouette=Image.new('RGBA',im.size,(235,235,225));silhouette.putalpha(im.getchannel('A'));board.paste(silhouette,(i%10*120+10,i//10*270+205),silhouette)
  d.text((i%10*120+10,i//10*270+5),str(i),font=font,fill='#aeb8bf')
 board.save(out/'SE-native-distance-check.png')
 board=Image.new('RGB',(1320,310),(25,28,33));d=ImageDraw.Draw(board)
 for col,(file,label)in enumerate([(f'SE-throw-{i:02}.png',str(i))for i in(16,17,18,19)]+[('SE-ready.png','READY'),('SE-throw-00.png','0')]):
  im=Image.open(out/file).convert('RGBA').crop((32,6,76,48)).resize((198,189),Image.Resampling.NEAREST);board.paste(im,(col*220+10,55),im);d.text((col*220+15,20),label,font=font,fill='#e7bd70')
 board.save(out/'SE-completion-ready-entry-audit.png')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);ap.add_argument('--analysis',type=Path,required=True);a=ap.parse_args()
 brief=json.loads(a.analysis.read_text());assert brief['observed_before_new_art']and brief['source_exact_holds']==[[1,19],[2,4],[7,8]]
 accepted=ROOT/'art/sprites/infantry-snes-v01/motion-study-v33-e2-throw';approval=json.loads((accepted/'approval-2026-10-21.json').read_text())
 if a.out.exists():raise RuntimeError('New version required')
 a.out.mkdir(parents=True);records=[]
 for group in('accepted','paired_mask_hashes','paired_role_hashes'):
  for name,h in approval[group].items():
   assert hashlib.sha256((accepted/name).read_bytes()).hexdigest()==h;shutil.copy2(accepted/name,a.out/name)
 for stage in range(20):
  s=frame_se(stage);p=a.out/f'SE-throw-{stage:02}.png';s.im.save(p);s.mask.save(p.with_stem(p.stem+'-player-mask'));s.roles.save(p.with_stem(p.stem+'-paint-roles'))
  records.append(dict(view='SE',stage=stage,phase=PHASES[stage],sha256=hashlib.sha256(p.read_bytes()).hexdigest(),parts=s.trace,review='UNREVIEWED'))
 s=frame_se(0,ready=True);s.im.save(a.out/'SE-ready.png');s.mask.save(a.out/'SE-ready-player-mask.png');s.roles.save(a.out/'SE-ready-paint-roles.png')
 east=json.loads((accepted/'frames.json').read_text())['keys']
 (a.out/'frames.json').write_text(json.dumps(dict(scope='Exact accepted East20 plus private SE20; SE not reviewed',anchor=[54,40],fps=15,release_stage=14,source_start={'E':184,'SE':164},keys=[dict(r,view='E')for r in east]+records),indent=2)+'\n')
 shutil.copy2(a.analysis,a.out/'source-action-brief.json');evidence(a.out,a.study)
 author=a.out/'authoring-source';author.mkdir()
 for name in('e2_throw_study_v10.py','e2_run_study_v23.py','e2_run_study_v15.py'):shutil.copy2(Path(__file__).with_name(name),author/name)
 print(a.out)

if __name__=='__main__':main()
