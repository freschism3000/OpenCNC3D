#!/usr/bin/env python3
"""Six new Grenadier run cameras, retaining the exact approved E/SE v23 files.

Read the versioned whole-action brief before drawing. All camera/limb keys below
are new semantic poses, not fitted DOS joints or mirrored/reduced bitmap art.
"""
from pathlib import Path
from math import hypot
import argparse,hashlib,json,shutil
from PIL import Image,ImageDraw,ImageFont
from e2_run_study_v23 import Native,PHASES,ROOT,px

VIEWS=('N','NW','W','SW','S','SE','E','NE')
NEW=('N','NW','W','SW','S','NE')
# Stable local crop/origin for an editable HEAD component, not frame-centering.
# West-side E1 masters have canvas offsets from their former leftward rifle.
HEAD_X={'N':4,'NW':3,'W':3,'SW':3,'S':3,'NE':3}
TORSO={
 'N':[(3,6,'65g6654g31'),(3,7,'65g6654g31'),(3,8,'54g5543g31'),
      (4,9,'4g5443g31'),(4,10,'3g4332g31'),(4,11,'3g3322g31'),
      (4,12,'1hihihh1'),(4,13,'hihhhih1'),(4,14,'36545431')],
 'NW':[(4,6,'65g6654g1'),(3,7,'65g6554g31'),(3,8,'54g5543g31'),
       (3,9,'54g5443g31'),(4,10,'4g4332g31'),(4,11,'3g3322g31'),
       (4,12,'1hihihh1'),(4,13,'hihhhih1'),(4,14,'36545431')],
 'W':[(6,6,'6yg21'),(5,7,'65g321'),(5,8,'54g321'),
      (5,9,'54g431'),(5,10,'43g431'),(5,11,'32g431'),
      (5,12,'1hhhi1'),(5,13,'hihhh1'),(5,14,'365431')],
 'SW':[(4,6,'6yg431'),(3,7,'65g443g31'),(3,8,'54g444g431'),
       (3,9,'54g443g431'),(4,10,'4g343g431'),(4,11,'3g33g431'),
       (4,12,'1hhhihh1'),(4,13,'hihhhih1'),(4,14,'36545431')],
 'S':[(3,6,'6yg33g541'),(3,7,'65g44g431'),(3,8,'54g44g431'),
      (3,9,'54g43g431'),(3,10,'43g33g431'),(4,11,'3g33g431'),
      (4,12,'1hhhiih1'),(4,13,'hihhhih1'),(4,14,'36545431')],
 'NE':[(4,6,'65g6654g1'),(3,7,'65g6554g31'),(3,8,'54g5543g31'),
       (3,9,'54g5443g31'),(3,10,'43g4332g31'),(4,11,'3g3322g31'),
       (4,12,'1hihihh1'),(4,13,'hihhhih1'),(4,14,'36545431')]
}
LEGS={
 'W':[
  (((7,14),(5,18),(3,22)),((8,14),(11,16),(12,19))),
  (((7,15),(6,18),(6,22)),((8,15),(7,17),(9,19))),
  (((7,14),(8,18),(10,22)),((8,14),(5,16),(4,19))),
  (((7,14),(10,16),(11,19)),((8,14),(6,18),(4,22))),
  (((7,15),(6,17),(8,19)),((8,15),(7,18),(7,22))),
  (((7,14),(4,16),(3,19)),((8,14),(9,18),(11,22)))],
 'SW':[
  (((9,15),(7,18),(5,22)),((6,14),(8,16),(9,18))),
  (((9,16),(8,19),(8,22)),((6,15),(5,17),(6,19))),
  (((9,15),(10,18),(12,20)),((6,14),(4,16),(3,19))),
  (((9,15),(11,17),(11,19)),((6,14),(4,18),(3,21))),
  (((9,16),(8,18),(8,20)),((6,15),(6,18),(6,21))),
  (((9,15),(7,17),(6,20)),((6,14),(7,17),(9,20)))],
 'NE':[
  (((9,15),(11,18),(12,21)),((6,14),(4,16),(4,19))),
  (((9,16),(10,19),(10,22)),((6,15),(7,17),(7,19))),
  (((9,15),(8,19),(7,23)),((6,14),(8,16),(9,18))),
  (((9,15),(7,17),(7,20)),((6,14),(8,17),(9,20))),
  (((9,16),(10,18),(10,20)),((6,15),(6,18),(6,21))),
  (((9,15),(11,17),(12,19)),((6,14),(5,18),(3,22)))],
 'NW':[
  (((6,15),(4,18),(3,21)),((9,14),(11,16),(11,19))),
  (((6,16),(5,19),(5,22)),((9,15),(8,17),(8,19))),
  (((6,15),(7,19),(8,23)),((9,14),(7,16),(6,18))),
  (((6,15),(8,17),(8,20)),((9,14),(7,17),(6,20))),
  (((6,16),(5,18),(5,20)),((9,15),(9,18),(9,21))),
  (((6,15),(4,17),(3,19)),((9,14),(10,18),(12,22)))],
 'N':[
  (((6,15),(6,18),(6,22)),((10,15),(10,16),(10,17))),
  (((6,16),(6,19),(6,22)),((10,16),(10,17),(10,19))),
  (((6,15),(6,18),(6,21)),((10,15),(10,17),(10,20))),
  (((6,15),(6,16),(6,17)),((10,15),(10,18),(10,22))),
  (((6,16),(6,17),(6,19)),((10,16),(10,19),(10,22))),
  (((6,15),(6,17),(6,20)),((10,15),(10,18),(10,21)))],
 'S':[
  (((6,15),(6,18),(6,22)),((10,15),(10,16),(10,17))),
  (((6,16),(6,19),(6,22)),((10,16),(10,17),(10,19))),
  (((6,15),(6,18),(6,21)),((10,15),(10,18),(10,20))),
  (((6,15),(6,16),(6,17)),((10,15),(10,18),(10,22))),
  (((6,16),(6,17),(6,19)),((10,16),(10,19),(10,22))),
  (((6,15),(6,18),(6,20)),((10,15),(10,18),(10,21)))]
}
FREE_W=[((11,7),(12,10),(11,12)),((11,8),(11,10),(10,12)),
        ((11,7),(10,9),(8,10)),((11,7),(9,9),(7,10)),
        ((11,8),(10,10),(9,12)),((11,7),(12,10),(11,12))]
FREE_SW=[((4,7),(3,9),(4,11)),((4,8),(3,10),(4,12)),
         ((4,7),(2,9),(1,10)),((4,7),(2,9),(1,10)),
         ((4,8),(3,10),(4,12)),((4,7),(3,9),(4,11))]

class Camera(Native):
 def boot(self,x,y,view,far):
  rows={'W':['ssts','rrrr'],'SW':['rs','rst','rr.'],
        'NE':['.sr','tsr','rr.'],'NW':['rs.','rst','.rr'],
        'N':['sss','rrr'],'S':['sts','rrr']}[view]
  if far:rows=[r.replace('t','s')for r in rows]
  self.rows(px(x)-(2 if view=='W'else 1),px(y)-(2 if view in('NW','NE')else 1),rows)
 def camera_head(self,view,bob):
  master=ROOT/f'art/sprites/minigunner-snes-v01/animation-v06/native-parts/{view}-complete-master.png'
  im=Image.open(master).convert('RGBA');head=im.crop((0,0,im.width,6));box=head.getbbox();head=head.crop((box[0],0,box[2],6))
  cloth={tuple(bytes.fromhex(c))for c in('4b371f','6e502a','967438','b78e43','cca856','e3bf73','f2d794','fff1ad')}
  for y in range(6):
   for x in range(head.width):
    c=head.getpixel((x,y))
    if not c[3]:continue
    p=(44+HEAD_X[view]+x,16+bob+y);self.im.putpixel(p,c)
    active=c[:3]in cloth;self.mask.putpixel(p,255 if active else 0);self.roles.putpixel(p,4 if active else 0)

def frame(stage,view):
 s=Camera();bob=int(stage%3==1);support=0 if stage<3 else 1;a,b=LEGS[view][stage]
 s.leg_key(b,view,True,support==1);s.leg_key(a,view,False,support==0)
 if view=='W':s.arm(*FREE_W[stage],far=True)
 elif view=='SW':s.arm(*FREE_SW[stage],far=True)
 elif view=='S':
  dy=(1,0,-1,-1,0,1)[stage];s.arm((3,7+bob),(2,10+bob+dy/2),(3,12+bob+dy),far=True)
 elif view=='N':
  # Hands are genuinely hidden beyond the rear torso, not painted skin spots.
  elbow=((2,10),(2,9),(3,8),(3,8),(2,9),(2,10))[stage]
  s.arm((4,7+bob),(elbow[0],elbow[1]+bob),(5,10+bob),far=True)
  s.arm((11,7+bob),(13,9+bob),(9,10+bob),far=True)
 elif view in ('NW','NE'):
  if view=='NW':s.arm((4,7+bob),(2,9+bob),(4,11+bob),far=True)
  else:s.arm((10,7+bob),(12,9+bob),(10,11+bob),far=True)
 if view=='W':s.rows(10,6+bob,['1hih','1iih','1ihh','1ihh','1npo','1nqp','1npo','1nno'])
 elif view=='SW':s.rows(11,8+bob,['hi1','ih1','ih1'])
 elif view in('NW','NE'):
  s.rows(9 if view=='NW'else 2,6+bob,['hih1','iih1','ihh1','ihh1','opn1','pqn1','pon1','oon1'])
 for x,y,row in TORSO[view]:s.rows(x,y+bob,[row])
 if view=='N':s.rows(4,12+bob,['nopppqon','noppopon'])
 elif view=='S':s.rows(3,12+bob,['opn','pqn']);s.rows(10,12+bob,['npo','nqp'])
 elif view in('W','NW','SW'):s.rows(11 if view=='SW'else 10,11+bob,['npo','nqp','npo','nno'])
 else:s.rows(1,11+bob,['opn','pqn','pon','onn'])
 # Foreground arm/prop overlap follows the camera; back views hide the grenade.
 if view=='W':s.arm((9,7+bob),(8,10+bob),(4,11+bob));gx,gy=3,10+bob
 elif view=='SW':s.arm((10,8+bob),(9,11+bob),(4,12+bob));gx,gy=3,12+bob
 elif view=='S':s.arm((11,7+bob),(11,10+bob),(8,11+bob));gx,gy=8,10+bob
 else:
  gx=gy=None
  if view in('NW','NE'):
   dy=(1,0,-1,-1,0,1)[stage]
   if view=='NW':s.arm((4,8+bob),(3,10+bob),(2,10+bob+dy))
   else:s.arm((10,8+bob),(11,10+bob),(12,10+bob+dy))
 if gx is not None:s.rows(gx-1,gy-1,['.m.','wvu','.u.'])
 s.camera_head(view,bob)
 return s

def evidence(out,study):
 original=Image.open(study/'E2/original.png').convert('RGBA');font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
 overview=Image.new('RGB',(1320,8*200),(25,28,33));d=ImageDraw.Draw(overview)
 for vi,view in enumerate(VIEWS):
  board=Image.new('RGB',(1320,560),(25,28,33));bd=ImageDraw.Draw(board);gif=[]
  for k in range(6):
   idx=16+vi*6+k;old=original.crop((idx%20*50,idx//20*39,idx%20*50+50,idx//20*39+39)).resize((100,78),Image.Resampling.NEAREST);new=Image.open(out/f'{view}-run-{k}.png').convert('RGBA')
   pair=Image.new('RGB',(560,300),(25,28,33));pd=ImageDraw.Draw(pair)
   for row,im in enumerate((old,new)):
    tile=im.crop((38,12,68,44)).resize((210,224),Image.Resampling.NEAREST);board.paste(tile,(k*220+5,row*280+36),tile);bd.text((k*220+5,row*280+8),f'{view} / '+('DOS'if row==0 else'v28')+f' / {k}',font=font,fill='#e7bd70');pair.paste(tile,(row*280+30,40),tile);pd.text((row*280+10,10),view+' / '+('DOS'if row==0 else'new'),font=font,fill='#e7bd70')
   gif.append(pair)
   tile=new.crop((38,12,68,44)).resize((150,160),Image.Resampling.NEAREST);overview.paste(tile,(k*220+35,vi*200+25),tile);d.text((k*220+8,vi*200+4),view+f' / {k}',font=font,fill='#e7bd70')
  board.save(out/f'{view}-six-frame-audit.png');gif[0].save(out/f'{view}-run-comparison.gif',save_all=True,append_images=gif[1:],duration=[130,140,130,130,140,130],loop=0,disposal=2)
 overview.save(out/'all-camera-native-audit.png')
 small=Image.new('RGB',(960,8*140),(25,28,33));d=ImageDraw.Draw(small)
 for vi,view in enumerate(VIEWS):
  for k in range(6):
   im=Image.open(out/f'{view}-run-{k}.png').convert('RGBA').crop((39,12,67,44))
   for scale,x in((1,k*160+4),(2,k*160+40)):
    tile=im.resize((im.width*scale,im.height*scale),Image.Resampling.NEAREST);small.paste(tile,(x,vi*140+20),tile)
   sil=Image.new('RGBA',im.size,(235,235,225));sil.putalpha(im.getchannel('A'));small.paste(sil,(k*160+112,vi*140+20),sil)
 small.save(out/'unlabelled-distance-check.png')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);a=ap.parse_args()
 brief=ROOT/'art/sprites/infantry-snes-v01/motion-study-v24-e2-run-reference/source-action-brief.json';assert json.loads(brief.read_text())['before_authoring']
 if a.out.exists():raise RuntimeError('New versioned output required')
 accepted=ROOT/'art/sprites/infantry-snes-v01/motion-study-v23-e2-run';approval=json.loads((accepted/'approval-2026-10-21.json').read_text());a.out.mkdir(parents=True);records=[]
 for view in VIEWS:
  for k in range(6):
   p=a.out/f'{view}-run-{k}.png'
   if view in('E','SE'):
    assert hashlib.sha256((accepted/p.name).read_bytes()).hexdigest()==approval['accepted'][p.name]
    for suffix in('', '-player-mask','-paint-roles'):shutil.copy2(accepted/f'{view}-run-{k}{suffix}.png',a.out/f'{view}-run-{k}{suffix}.png')
    trace=next(r['limbs']for r in json.loads((accepted/'frames.json').read_text())['keys']if r['view']==view and r['stage']==k)
   else:
    s=frame(k,view);s.im.save(p);s.mask.save(p.with_name(p.stem+'-player-mask.png'));s.roles.save(p.with_name(p.stem+'-paint-roles.png'));trace=s.trace
   records.append(dict(view=view,stage=k,phase=PHASES[k],sha256=hashlib.sha256(p.read_bytes()).hexdigest(),limbs=trace,status='exact accepted v23'if view in('E','SE')else'UNREVIEWED new camera'))
 (a.out/'frames.json').write_text(json.dumps(dict(status='E/SE accepted; six new views need personal/frame and motion audit',anchor=[54,40],fps=7.5,keys=records),indent=2)+'\n');shutil.copy2(brief,a.out/'source-action-brief.json')
 (a.out/'authoring-source').mkdir();shutil.copy2(__file__,a.out/'authoring-source'/Path(__file__).name);evidence(a.out,a.study);print('36 new cells +12exact accepted cells:',a.out)
if __name__=='__main__':main()
