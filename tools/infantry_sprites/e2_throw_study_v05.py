#!/usr/bin/env python3
"""Source-action-first Grenadier throw, using the accepted v23/v28 identity.

Editable native garment/limb painting. DOS pixels enter only the comparison
writer, after all new art is complete. No old v17 head/torso or bitmap warping.
"""
from pathlib import Path
import argparse, hashlib, json, shutil
from PIL import Image, ImageDraw, ImageFont
from e2_run_study_v23 import Native, ROOT, TORSO
from e2_run_study_v15 import px

# New overhand choreography in native character coordinates. Far foot stays
# behind; the near foot steps forward while the hips and upper body load.
# bob, lean, dip, elbow, wrist, balance wrist, front ankle, rear ankle, hip y.
KEYS=[
 (0,0,0,(9,10),(12,11),(11,10),9,6,14),
 (1,0,0,(9,10),(12,11),(11,11),9,6,15),
 (0,0,0,(9,9),(12,10),(11,10),9,6,14),
 (0,0,0,(9,10),(12,11),(10,11),9,6,14),
 (0,0,0,(9,9),(12,10),(11,10),9,6,14),
 (1,0,0,(9,9),(11,10),(10,11),9,6,15),
 (0,0,0,(9,10),(11,11),(9,11),9,6,14),
 (0,0,0,(9,9),(11,10),(9,10),9,6,14),
 (0,0,0,(9,9),(10,10),(8,11),10,5,14),
 (1,0,0,(8,9),(9,9),(7,12),11,4,15),
 (1,-1,0,(8,8),(7,6),(5,12),12,3,15),
 (0,-1,0,(5,7),(1,4),(4,13),12,3,14),
 (0,-1,0,(5,5),(3,1),(4,13),12,3,14),
 (0,-1,0,(6,3),(7,-2),(4,13),12,3,14),
 (0,0,0,(9,3),(11,-2),(4,13),12,3,14),
 (1,1,0,(12,5),(15,1),(5,12),12,3,15),
 (0,3,2,(12,9),(14,10),(5,11),12,3,14),
 (0,1,1,(11,10),(12,12),(6,13),11,4,14),
 (0,0,0,(9,11),(10,13),(6,12),10,5,14),
 (0,0,0,(9,10),(11,11),(7,11),9,6,14),
]
assert KEYS[2]==KEYS[4]  # Exact original hold, not missing native motion.
PHASES=['prepare']*8+['gather','step/load','draw back','cock','cock','raise',
 'release','extend','follow through','rise','recover','ready']
LOAD=[(4,6,'65g431'),(3,7,'65g44g31'),(3,8,'54g44g31'),
 (3,9,'54g44g31'),(4,10,'43g4g31'),(4,11,'3g33g31'),
 (4,12,'1hhhi1'),(4,13,'hihhh1'),(5,14,'365431')]

def head(s,bob,lean,dip):
 # Exact head/collar cluster of the approved E run, translated together about
 # the chest hinge. Forearms and held grenade are deliberately foreground.
 h=Image.open(ROOT/'art/sprites/minigunner-snes-v01/animation-v06/native-parts/E-complete-master.png').convert('RGBA')
 paint={tuple(bytes.fromhex(v))for v in('4b371f','6e502a','967438','b78e43','cca856','e3bf73','f2d794','fff1ad')}
 for y in range(6):
  for x in range(h.width):
   c=h.getpixel((x,y))
   if c[3]:
    p=(45+x+lean,16+y+bob+dip);s.im.putpixel(p,c)
    s.mask.putpixel(p,255 if c[:3]in paint else 0);s.roles.putpixel(p,4 if c[:3]in paint else 0)

def frame(stage,ready=False):
 s=Native();bob,lean,dip,elbow,wrist,free,front,rear,hy=KEYS[stage]
 if ready:bob=lean=dip=0;elbow,wrist,free=(9,10),(12,11),(11,10)
 # Complete pose keys, compact short legs. Raised heel on extension/follow
 # through conveys weight transfer without translating the actor/canvas.
 rear_y=21 if stage in(14,15,16)and not ready else 22
 near=((8,hy),(8+(front-8)*.45,18 if hy==14 else 18.5),(front,22))
 far=((7,hy),(7+(rear-7)*.45,18 if hy==14 else 18.5),(rear,rear_y))
 s.leg_key(far,'E',True,stage<9);s.leg_key(near,'E',False,stage>=9)
 sx=lambda y:px(lean*(14-y)/8)
 sy=lambda y:bob+px(dip*(14-y)/8)
 def arm_point(p):return(p[0]+sx(7),p[1]+sy(7))
 s.arm(*map(arm_point,((4,7),(4,10),free)),far=True)
 # Rear pack, waist-mounted blue pouch, belt and chest use the same hinge.
 # Broad coherent native panels carry the accepted run's material/scale.
 for y,row in enumerate(['hih1','iih1','ihh1','ihh1','opn1','pqn1','pon1','oon1'],6):
  s.rows(1+sx(y),y+sy(y),[row])
 for x,y,row in LOAD if 9<=stage<=13 and not ready else TORSO['E']:
  s.rows(x+sx(y),y+sy(y),[row])
 for y,row in enumerate(['opn','pqn','pon','onn'],11):s.rows(2+sx(y),y+sy(y),[row])
 head(s,bob,lean,dip)
 shoulder=(9 if 9<=stage<=13 and not ready else 8,7)
 s.arm(*map(arm_point,(shoulder,elbow,wrist)))
 visible=stage<14 or ready
 if visible:
  gx,gy=arm_point(wrist);s.rows(gx-1,gy-2,['.m.','wvu','.u.'])
 s.trace.append(dict(part='throw',stage=stage,phase='stand_ready'if ready else PHASES[stage],
  wrist=arm_point(wrist),grenade_held=visible,release=stage==14 and not ready,
  bob=bob,lean=lean,dip=dip,actor_anchor=[54,40]))
 return s

def evidence(out,study):
 source=Image.open(study/'E2/original.png').convert('RGBA')
 font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',15);gifs=[]
 for group in range(4):
  board=Image.new('RGB',(1200,610),(25,28,33));d=ImageDraw.Draw(board)
  for col,i in enumerate(range(group*5,group*5+5)):
   n=184+i;old=source.crop((n%20*50,n//20*39,n%20*50+50,n//20*39+39)).resize((100,78),Image.Resampling.NEAREST)
   new=Image.open(out/f'E-throw-{i:02}.png').convert('RGBA')
   pair=Image.new('RGB',(800,410),(25,28,33));pd=ImageDraw.Draw(pair)
   for row,im in enumerate((old,new)):
    tile=im.crop((36,8,74,44)).resize((228,216),Image.Resampling.NEAREST)
    board.paste(tile,(col*240+6,row*300+38),tile)
    d.text((col*240+8,row*300+10),f'{i:02} / '+('DOS'if row==0 else'NATIVE30')+' / '+PHASES[i],font=font,fill='#e7bd70')
    tile=im.crop((32,6,76,48)).resize((308,294),Image.Resampling.NEAREST)
    pair.paste(tile,(row*400+44,45),tile);pd.text((row*400+20,15),'1995 DOS'if row==0 else'NEW NATIVE THROW',font=font,fill='#e7bd70')
   pd.text((20,365),f'{i+1}/20 / {PHASES[i]} / release at14',font=font,fill='#aeb8bf');gifs.append(pair)
  board.save(out/f'five-frame-audit-{group}.png')
 gifs[0].save(out/'east-throw-comparison.gif',save_all=True,append_images=gifs[1:],duration=[70,60,70]*6+[70,60],loop=0,disposal=2)
 board=Image.new('RGB',(1200,400),(25,28,33));d=ImageDraw.Draw(board)
 for i in range(20):
  im=Image.open(out/f'E-throw-{i:02}.png').convert('RGBA').crop((36,8,74,44))
  for scale,y in((1,30),(2,100)):
   tile=im.resize((im.width*scale,im.height*scale),Image.Resampling.NEAREST);board.paste(tile,(i%10*120+10,i//10*200+y),tile)
  d.text((i%10*120+10,i//10*200+5),str(i),font=font,fill='#aeb8bf')
 board.save(out/'native-distance-check.png')
 # One-shot completion is not a same-frame20->0 loop. Explicit ready pose.
 board=Image.new('RGB',(1320,310),(25,28,33));d=ImageDraw.Draw(board)
 for col,(file,label)in enumerate([(f'E-throw-{i:02}.png',str(i))for i in(16,17,18,19)]+[('E-ready.png','READY'),('E-throw-00.png','0')]):
  im=Image.open(out/file).convert('RGBA').crop((36,8,74,44)).resize((190,180),Image.Resampling.NEAREST);board.paste(im,(col*220+15,55),im);d.text((col*220+15,20),label,font=font,fill='#e7bd70')
 board.save(out/'completion-ready-entry-audit.png')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);ap.add_argument('--analysis',type=Path,required=True);a=ap.parse_args()
 brief=json.loads(a.analysis.read_text());assert brief['observed_before_new_art']and brief['release_stage']==14
 if a.out.exists():raise RuntimeError('Versioned private output required')
 a.out.mkdir(parents=True);records=[]
 for stage in range(20):
  s=frame(stage);p=a.out/f'E-throw-{stage:02}.png';s.im.save(p);s.mask.save(p.with_stem(p.stem+'-player-mask'));s.roles.save(p.with_stem(p.stem+'-paint-roles'))
  records.append(dict(stage=stage,phase=PHASES[stage],sha256=hashlib.sha256(p.read_bytes()).hexdigest(),parts=s.trace,review='UNREVIEWED'))
 s=frame(0,ready=True);s.im.save(a.out/'E-ready.png');s.mask.save(a.out/'E-ready-player-mask.png');s.roles.save(a.out/'E-ready-paint-roles.png')
 (a.out/'frames.json').write_text(json.dumps(dict(scope='Private East20-stage native throw; unreviewed',anchor=[54,40],fps=15,source_start=184,release_stage=14,keys=records),indent=2)+'\n')
 shutil.copy2(a.analysis,a.out/'source-action-brief.json');evidence(a.out,a.study)
 author=a.out/'authoring-source';author.mkdir()
 for file in ('e2_throw_study_v05.py','e2_run_study_v23.py','e2_run_study_v15.py'):shutil.copy2(Path(__file__).with_name(file),author/file)
 print(a.out)

if __name__=='__main__':main()
