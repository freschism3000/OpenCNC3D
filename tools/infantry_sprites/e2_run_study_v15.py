#!/usr/bin/env python3
"""Grenadier E/SE run v15: connected helmet/head and distinct quarter view.

DOS inputs are used ONLY by the comparison compositor after native drawing.
Persistent A/B legs, independent new keys and complete painted garment masses.
This private study has no publisher or implicit art-review verdict generator.
"""
from pathlib import Path
from math import floor,hypot,sqrt
import argparse,hashlib,json,shutil
from PIL import Image,ImageDraw,ImageFont
# Self-contained native material table; no previous design module or DOS input
# participates in drawing this character.
PALETTE = {
 '0':'111c24','1':'241c15','2':'4b371f','3':'6e502a',
 '4':'b78e43','5':'cca856','6':'e3bf73','7':'fff1ad',
 'a':'8d502e','b':'dda066','c':'ebbc7d','d':'2a201b','e':'542f20','f':'87613b',
 'g':'30271b','h':'504d37','i':'74674a','j':'111c24','k':'283943','l':'475864','m':'78838a',
 'n':'1c2c50','o':'354b89','p':'647bc0','q':'98abd8',
 'r':'241c15','s':'4b371f','t':'74674a','u':'283b3b','v':'485b48','w':'87966e','x':'bcc2a0',
}


def px(x):return floor(x+.5)
class Surface:
 def __init__(self):
  self.im=Image.new('RGBA',(100,78));self.mask=Image.new('L',(100,78));self.roles=Image.new('L',(100,78));self.pal=PALETTE.copy();self.trace=[]
 def put(self,x,y,c):
  xx,yy=44+px(x),16+px(y)
  if not 0<=xx<100 or not 0<=yy<78:raise ValueError(('Native pixel outside cell',xx,yy))
  self.im.putpixel((xx,yy),tuple(bytes.fromhex(self.pal[c]))+(255,));paint=c in'234567';self.mask.putpixel((xx,yy),255 if paint else 0);self.roles.putpixel((xx,yy),int(c)+1 if paint else 0)
 def line(self,a,b,c,width=1):
  n=max(1,px(hypot(b[0]-a[0],b[1]-a[1])*3))
  for k in range(n+1):
   x=a[0]+(b[0]-a[0])*k/n;y=a[1]+(b[1]-a[1])*k/n
   for j in range(width):self.put(x+j-(width-1)/2,y,c)
 def cloth(self,a,b,width=4,far=False,material=('5642','5642','4532','3421')):
  dx,dy=b[0]-a[0],b[1]-a[1];length=hypot(dx,dy)
  if length<.05:return
  ux,uy=dx/length,dy/length
  for y in range(floor(min(a[1],b[1])-width),px(max(a[1],b[1])+width)):
   for x in range(floor(min(a[0],b[0])-width),px(max(a[0],b[0])+width)):
    t=((x-a[0])*ux+(y-a[1])*uy)/length;cross=((x-a[0])*uy-(y-a[1])*ux)/width+.5
    if -.12<=t<=1.12 and 0<=cross<1:
     row=material[min(len(material)-1,max(0,px(t*(len(material)-1))))]
     c=row[min(3,int(cross*4))]
     if far:c=str(max(1,int(c)-1))
     self.put(x,y,c)
 def leg(self,label,hip,knee,foot,far,support):
  # Segment positions are deliberate new pixel keys, never extracted DOS joints.
  self.cloth(hip,knee,3.5,far,('5642','5642','4542','6753'));self.cloth(knee,(foot[0],foot[1]-1),3,far,('5642','4542','4542','3421'))
  x,y=foot;self.line((x-1,y-1),(x+1,y-1),'s');self.line((x-1,y),(x+2,y),'r');self.put(x+1,y-1,'s'if far else't');self.put(x-1,y-1,'s'if far else't')
  self.trace.append({'limb':label,'hip':hip,'knee':knee,'foot':foot,'depth':'far'if far else'near','support':support})
 def arm(self,shoulder,elbow,wrist,far=False):
  self.cloth(shoulder,elbow,3.2 if far else 3.8,far);self.cloth(elbow,wrist,2.8 if far else 3.3,far)
  self.put(wrist[0],wrist[1],'a'if far else'b');self.put(wrist[0]+1,wrist[1],'b'if far else'c');self.put(wrist[0]+1,wrist[1]+1,'a')

# Complete contact / compression / recovery, then opposite support, from scratch.
# Every key retains A-near/B-far identity. The upper body compresses only one pixel.
KEYS=[
 {'phase':'A contact / B folded recovery','bob':0,'A':((8,15),(10,19),(11,23),True),'B':((7,15),(4,17),(4,20),False),'far_elbow':(4,10),'far_wrist':(5,12)},
 {'phase':'A compression / B passing','bob':1,'A':((8,16),(9,19),(8,23),True),'B':((7,16),(9,18),(7,20),False),'far_elbow':(4,10),'far_wrist':(6,11)},
 {'phase':'A trailing support / B forward recovery','bob':0,'A':((8,15),(6,19),(5,23),True),'B':((7,15),(11,17),(12,20),False),'far_elbow':(5,10),'far_wrist':(7,10)},
 {'phase':'B contact / A folded recovery','bob':0,'A':((8,15),(5,17),(5,20),False),'B':((7,15),(9,19),(10,22),True),'far_elbow':(6,10),'far_wrist':(8,10)},
 {'phase':'B compression / A passing','bob':1,'A':((8,16),(10,18),(8,20),False),'B':((7,16),(8,19),(7,22),True),'far_elbow':(5,10),'far_wrist':(7,11)},
 {'phase':'B trailing support / A forward recovery','bob':0,'A':((8,15),(12,17),(13,20),False),'B':((7,15),(5,19),(4,22),True),'far_elbow':(4,10),'far_wrist':(5,12)},
]
TORSO=['....654g1......','....6543g1.....','....5433g41....','....5434gg1....','....4324gg1....','....3424gg1....','....1ghhhh1....','....hihhh1.....','....365431.....']

HEAD={
 'E':['......6542.....','.....765431....','.....654321....',
      '....33333641...','......a1ba.....','......abc1.....'],
 'SE':['.....65421.....','....7654321....','....6543321....',
       '...133333641...','.....a1b1a1....','......bcba1....'],
}
TORSO_SE=['...654ggg4561..','..654g43g34541.','..54g443g34431.',
          '..54g443g34431.','..34g43gg34431.','...4g43gh4341..',
          '...1hihhhh11...','...hihhhih1....','...3654g5431...']

def frame(stage,facing='E'):
 key=KEYS[stage];s=Surface();bob=key['bob'];shift=lambda p:(p[0]+1,p[1]+bob)
 # Carry forward the accepted E1 human gait's4.5px bones, with this character's
 # own hip lanes/costume. It is shared human motion, never DOS anatomical fitting.
 # Equal support-ground height in pure profile removes the prior half-cycle bias.
 support=0 if stage<3 else 1;phase=stage%3
 for leg in(1,0):
  hip=((8 if leg==0 else 7,15+bob) if facing=='E' else (9 if leg==0 else 6.5,14.5+bob if leg==0 else 14+bob));along=(3.5,0,-3.5)[phase]if leg==support else(-3,-.5,2.5)[phase];drop=(8,7,8)[phase]if leg==support else(4,4,5)[phase];foot=(hip[0]+along,hip[1]+drop);dist=hypot(along,drop)
  if not 0<dist<=9:raise ValueError(('Designed stride exceeds character reach',stage,leg))
  height=sqrt(max(0,4.5**2-(dist/2)**2));knee=(hip[0]+along/2+drop/dist*height,hip[1]+drop/2-along/dist*height)
  if facing=='SE':
   # Project the same human gait into an actual diagonal view, with fixed hip
   # lanes and anatomical depth. This is not a shifted copy of the E pixels.
   project=lambda p:(hip[0]+(p[0]-hip[0])*.70,p[1]+(p[0]-hip[0])*.18)
   knee,foot=project(knee),project(foot)
  s.leg('AB'[leg],hip,knee,foot,leg==1,leg==support)
 s.arm(shift((5,7) if facing=='E' else (4,7)),shift(key['far_elbow']),shift(key['far_wrist']),True)
 # Rear equipment is a fixed torso attachment, not a free pixel or far wrist.
 # The original six DOS run stages all show a blue lower-back detail. Its exact
 # item identity is unresolved; preserve its visible colour/placement explicitly.
 pack=(['..hih1','..iih1','..ihh1','.hihh1','.opnh1','.pqnh1','.opnh1','..nnh1'] if facing=='E' else ['.hih1','.iih1','.ihh1','.ihh1','opnh1','pqnh1','opnh1','.nnh1'])
 for py,row in enumerate(pack,6):
  for x,c in enumerate(row):
   if c!='.':s.put(x,py+bob,c)
 for y,row in enumerate(TORSO if facing=='E' else TORSO_SE,6):
  lean=1 if y<10 else 0
  for x,c in enumerate(row):
   if c!='.':s.put(x+lean,y+bob,c)
 # New near-arm pose carries grenade; opposite arm counters the gait.
 s.arm(shift((9,7)),shift((9,10)),shift((12,11))) if facing=='E' else s.arm(shift((10,7)),shift((10,10)),shift((10,12)))
 x,y=shift((13,10) if facing=='E' else (11,11));s.put(x,y-1,'m');s.put(x-1,y,'w');s.put(x,y,'v');s.put(x+1,y,'u');s.put(x,y+1,'u')
 # Helmet, eyes, nose, jaw and neck are one deliberately fitted cluster.
 head=HEAD[facing]
 for y,row in enumerate(head):
  for x,c in enumerate(row):
   if c!='.':s.put(x+1,y+bob,c)
 return s

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);a=ap.parse_args();out=a.out
 if out.exists():raise RuntimeError('Versioned new motion study required')
 out.mkdir(parents=True);font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16);record=[];drawn={}
 # Native drawing is complete before any original raster is read.
 for facing in ('E','SE'):
  drawn[facing]=[]
  for i in range(6):
   s=frame(i,facing);p=out/f'{facing}-run-{i}.png';s.im.save(p);s.mask.save(out/f'{facing}-run-{i}-player-mask.png');s.roles.save(out/f'{facing}-run-{i}-paint-roles.png');drawn[facing].append(s.im)
   record.append({'facing':facing,'stage':i,'sha256':sha(p),'phase':KEYS[i]['phase'],'limbs':s.trace,'review':'UNREVIEWED'})
 original=Image.open(a.study/'E2/original.png').convert('RGBA');board=Image.new('RGB',(1320,1120),(25,28,33));d=ImageDraw.Draw(board);gif=[]
 for i in range(6):
  pair=Image.new('RGB',(800,660),(25,28,33));pd=ImageDraw.Draw(pair)
  for row,(facing,start)in enumerate((('E',52),('SE',46))):
   idx=start+i;before=original.crop((idx%20*50,idx//20*39,idx%20*50+50,idx//20*39+39)).resize((100,78),Image.Resampling.NEAREST)
   for col,im in enumerate((before,drawn[facing][i])):
    tile=im.crop((38,12,68,44)).resize((240,256),Image.Resampling.NEAREST);pair.paste(tile,(col*400+70,row*330+40),tile);pd.text((col*400+20,row*330+10),f'{facing} · '+('1995 DOS' if col==0 else 'REVISED NATIVE'),font=font,fill='#e7bd70')
    tile=im.crop((40,14,64,42)).resize((192,224),Image.Resampling.NEAREST);board.paste(tile,(i*220+12,(row*2+col)*280+36),tile);d.text((i*220+8,(row*2+col)*280+8),f'{facing} · '+('DOS' if col==0 else 'v15')+f' · stage{i}',font=font,fill='#e7bd70')
  gif.append(pair)
 board.save(out/'twelve-frame-audit.png');gif[0].save(out/'east-southeast-run-comparison.gif',save_all=True,append_images=gif[1:],duration=[130,140,130,130,140,130],loop=0)
 (out/'frames.json').write_text(json.dumps({'scope':'Revised E2 east and southeast run studies, native art awaiting visual acceptance','source_fps':7.5,'anchor':[54,40],'source_start':{'E':52,'SE':46},'keys':record},indent=2)+'\n')
 (out/'authoring-source').mkdir();shutil.copy2(__file__,out/'authoring-source'/Path(__file__).name)
 print('Twelve native stages and paired DOS evidence:',out)
if __name__=='__main__':main()
