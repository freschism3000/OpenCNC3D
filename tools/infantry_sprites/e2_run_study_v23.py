#!/usr/bin/env python3
"""Grenadier direction reconstruction. Native E/SE keys, not a v17 patch.

The accepted E1 six-row fitted military heads establish shared native craft.
Each camera has its own torso, arm overlap, pelvis and deliberately drawn keys.
DOS images are read by the evidence compositor only, after new art is drawn.
No DOS joint fitting, raster warping, whole-sprite mirroring or generated reduction.
"""
from pathlib import Path
import argparse, hashlib, json, shutil
from math import hypot
from PIL import Image, ImageDraw, ImageFont
from e2_run_study_v15 import Surface, px

ROOT=Path(__file__).resolve().parents[2]
PALETTE_EXTRA={'8':'f2d794','y':'967438','z':'f2d794','o':'3434ca','p':'6669f4','q':'aaaaff'}
# Left light, broad cloth planes, dark straps. Different actual camera drawing.
TORSO={
 'E':[(4,6,'6yg21'),(4,7,'65g321'),(4,8,'54g321'),
      (4,9,'54g431'),(4,10,'43g431'),(4,11,'32g431'),
      (4,12,'1hhhi1'),(4,13,'hihhh1'),(5,14,'365431')],
 'SE':[(6,6,'65g431'),(4,7,'6yg443g31'),(3,8,'65g444g431'),
       (3,9,'54g443g431'),(3,10,'43g43g431'),(4,11,'3g33g431'),
       (4,12,'1hhhihh1'),(4,13,'hihhhih1'),(4,14,'36545431')]
}
# Anatomical A is always near, B always far. Six complete keys. Hip/knee/ankle
# are screen-space artistic poses; foreshortening is painted, not IK guessing.
# E: narrow silhouette, level hip depth and horizontal boot/toe profile.
# SE: anatomical right = screen-left/down (near); left = screen-right/up (far).
# Camera-right is NOT anatomical right. Reversing that assignment gave v22 a
# SW-oriented pelvis under a SE head. Both axes are now explicit and coherent.
LEGS={
 'E':[
  (((8,14),(10,18),(12,22)),((7,14),(4,16),(3,19))),
  (((8,15),(9,18),(9,22)),((7,15),(8,17),(6,19))),
  (((8,14),(7,18),(5,22)),((7,14),(10,16),(11,19))),
  (((8,14),(5,16),(4,19)),((7,14),(9,18),(11,22))),
  (((8,15),(9,17),(7,19)),((7,15),(8,18),(8,22))),
  (((8,14),(11,16),(12,19)),((7,14),(6,18),(4,22)))],
 'SE':[
  (((6,15),(8,18),(10,22)),((9,14),(7,16),(6,18))),
  (((6,16),(7,19),(7,22)),((9,15),(10,17),(9,19))),
  (((6,15),(5,18),(3,20)),((9,14),(11,16),(12,19))),
  (((6,15),(4,17),(4,19)),((9,14),(11,18),(12,21))),
  (((6,16),(7,18),(7,20)),((9,15),(9,18),(9,21))),
  (((6,15),(8,17),(9,20)),((9,14),(8,17),(6,20)))]
}
PHASES=('A contact / B recovery','A compression / B passing',
        'A push-off / B forward swing','B contact / A recovery',
        'B compression / A passing','B push-off / A forward swing')
FREE_E=[((4,7),(3,10),(4,12)),((4,8),(4,10),(5,12)),
        ((4,7),(5,9),(7,10)),((4,7),(6,9),(8,10)),
        ((4,8),(5,10),(6,12)),((4,7),(3,10),(4,12))]
FREE_SE=[((10,7),(11,9),(10,11)),((10,8),(11,10),(11,12)),
         ((10,7),(12,9),(13,10)),((10,7),(12,9),(13,10)),
         ((10,8),(11,10),(11,12)),((10,7),(11,9),(10,11))]

class Native(Surface):
 def __init__(self):
  super().__init__();self.pal.update(PALETTE_EXTRA)
 def put(self,x,y,c):
  super().put(x,y,c)
  if c in '8yz':
   self.mask.putpixel((44+px(x),16+px(y)),255)
   self.roles.putpixel((44+px(x),16+px(y)),5)
 def rows(self,x,y,rows):
  for dy,row in enumerate(rows):
   for dx,c in enumerate(row):
    if c!='.':self.put(x+dx,y+dy,c)
 def boot(self,x,y,view,far):
  # A SE toe must point down-right, never retain an E horizontal sole bar.
  rows=(['sr','tsr','.rr'] if view=='SE' else ['stss','rrrr'])
  if far:rows=[r.replace('t','s') for r in rows]
  self.rows(px(x)-1,px(y)-1,rows)
 def leg_key(self,points,view,far,support):
  hip,knee,ankle=points
  self.cloth(hip,knee,3.6,far,('6752','6752','5642','7863'))
  self.cloth(knee,(ankle[0],ankle[1]-1),2.8,far,('5642','5642','4532','3421'))
  self.boot(*ankle,view,far)
  self.trace.append({'leg':'B' if far else 'A','hip':hip,'knee':knee,
                     'ankle':ankle,'support':support,'depth':'far' if far else 'near',
                     'projected_segments':[hypot(knee[0]-hip[0],knee[1]-hip[1]),hypot(ankle[0]-knee[0],ankle[1]-knee[1])]})
 def fitted_head(self,view,bob):
  # Exact accepted E1 head pixels as a shared military uniform component.
  # Six rows include neck. No former rifle/forearm pixels are present.
  head=Image.open(ROOT/f'art/sprites/minigunner-snes-v01/animation-v06/native-parts/{view}-complete-master.png').convert('RGBA')
  paint={tuple(bytes.fromhex(x)) for x in ('4b371f','6e502a','967438','b78e43','cca856','e3bf73','f2d794','fff1ad')}
  for y in range(6):
   for x in range(head.width):
    c=head.getpixel((x,y))
    if c[3]:
     pos=(45+x,16+y+bob);self.im.putpixel(pos,c)
     # Deliberate head-region material membership, not whole-art hue inference.
     active=c[:3] in paint;self.mask.putpixel(pos,255 if active else 0)
     self.roles.putpixel(pos,4 if active else 0)

def frame(stage,view):
 s=Native();bob=int(stage%3==1);support=0 if stage<3 else 1
 a,b=LEGS[view][stage]
 s.leg_key(b,view,True,support==1);s.leg_key(a,view,False,support==0)
 # Both camera drawings fit the same head/neck. E is profile; SE has a diagonal
 # shoulder line, two chest planes, lower screen-left near shoulder/hip.
 s.arm(*(FREE_E if view=='E' else FREE_SE)[stage],far=True)
 s.rows(1 if view=='E' else 2,6+bob,
        ['hih1','iih1','ihh1','ihh1','opn1','pqn1','pon1','oon1'])
 for x,y,row in TORSO[view]:s.rows(x,y+bob,[row])
 # Conspicuous, connected cobalt waist gear is a coherent fabric cluster,
 # never a small desaturated steel fleck. Source identity stays out of paint.
 s.rows(2 if view=='E' else 1,11+bob,['opn','pqn','pon','onn'])
 if view=='E':
  arm=((8,7+bob),(9,10+bob),(12,11+bob));gx,gy=13,10+bob
 else:
  arm=((4,8+bob),(5,11+bob),(10,12+bob));gx,gy=11,12+bob
 s.arm(*arm)
 s.rows(gx-1,gy-1,['.m.','wvu','.u.'])
 s.fitted_head(view,bob)
 return s

def evidence(out,study):
 original=Image.open(study/'E2/original.png').convert('RGBA')
 font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
 board=Image.new('RGB',(1320,1120),(25,28,33));d=ImageDraw.Draw(board);gifs=[]
 for k in range(6):
  pair=Image.new('RGB',(840,640),(25,28,33));pd=ImageDraw.Draw(pair)
  for row,view in enumerate(('E','SE')):
   idx=(52 if view=='E' else 46)+k
   before=original.crop((idx%20*50,idx//20*39,idx%20*50+50,idx//20*39+39)).resize((100,78),Image.Resampling.NEAREST)
   after=Image.open(out/f'{view}-run-{k}.png').convert('RGBA')
   for col,im in enumerate((before,after)):
    tile=im.crop((39,12,67,44)).resize((196,224),Image.Resampling.NEAREST)
    board.paste(tile,(k*220+8,(row*2+col)*280+36),tile)
    d.text((k*220+8,(row*2+col)*280+8),f'{view} '+('DOS' if col==0 else 'native23')+f' / {k}',font=font,fill='#e7bd70')
    tile=im.crop((36,10,71,45)).resize((280,280),Image.Resampling.NEAREST)
    pair.paste(tile,(col*420+65,row*320+30),tile)
    pd.text((col*420+15,row*320+5),view+' / '+('DOS' if col==0 else 'native23')+f' / {k}',font=font,fill='#e7bd70')
  gifs.append(pair)
 board.save(out/'twelve-frame-audit.png')
 gifs[0].save(out/'east-southeast-run-comparison.gif',save_all=True,append_images=gifs[1:],duration=[130,140,130,130,140,130],loop=0,disposal=2)
 # Actual native density, 2x and silhouette. No headings in the sprite field.
 board=Image.new('RGB',(840,300),(25,28,33));d=ImageDraw.Draw(board)
 for v,view in enumerate(('E','SE')):
  for k in range(6):
   im=Image.open(out/f'{view}-run-{k}.png').convert('RGBA').crop((39,12,67,44))
   for scale,y in ((1,8),(2,90)):
    tile=im.resize((im.width*scale,im.height*scale),Image.Resampling.NEAREST);board.paste(tile,(k*135+5+v*50,y),tile)
   tile=Image.new('RGBA',im.size,(235,235,225));tile.putalpha(im.getchannel('A'));tile=tile.resize((56,64),Image.Resampling.NEAREST)
   board.paste(tile,(k*135+5+v*50,210),tile)
 board.save(out/'unlabelled-distance-check.png')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);a=ap.parse_args()
 if a.out.exists():raise RuntimeError('Versioned output required')
 a.out.mkdir(parents=True);records=[]
 for view in ('E','SE'):
  for k in range(6):
   s=frame(k,view);p=a.out/f'{view}-run-{k}.png';s.im.save(p)
   s.mask.save(a.out/f'{view}-run-{k}-player-mask.png');s.roles.save(a.out/f'{view}-run-{k}-paint-roles.png')
   records.append({'view':view,'stage':k,'phase':PHASES[k],'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'limbs':s.trace,'review':'UNREVIEWED'})
 (a.out/'frames.json').write_text(json.dumps({'status':'private direction reconstruction; no visual acceptance','anchor':[54,40],'fps':7.5,'keys':records},indent=2)+'\n')
 evidence(a.out,a.study)
 (a.out/'authoring-source').mkdir();shutil.copy2(__file__,a.out/'authoring-source'/Path(__file__).name)
 print(a.out)

if __name__=='__main__':main()
