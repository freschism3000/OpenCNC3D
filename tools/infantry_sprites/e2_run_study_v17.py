#!/usr/bin/env python3
"""Eight-view Grenadier run v17, after complete source-action interpretation.

E/SE are exact v15 native frames. Six other views have newly typed head/costume
planes and independently projected human gait. Original raster is read only by
the final evidence compositor. No bitmap mirroring, DOS joint fitting or warping.
"""
from pathlib import Path
from math import sqrt, hypot
import argparse, hashlib, json, shutil
from PIL import Image, ImageDraw, ImageFont
from e2_run_study_v15 import Surface, KEYS, frame as previous_frame, px

VIEWS = ('N','NW','W','SW','S','SE','E','NE')
Q = sqrt(.5)
VECTORS = dict(zip(VIEWS,((0,-1),(-Q,-Q),(-1,0),(-Q,Q),(0,1),(Q,Q),(1,0),(Q,-Q))))
HEAD = {
 'N':['......6542.....','.....765431....','.....654321....','....13333321...','.....333321....','......aab1.....'],
 'NW':['.....65421.....','....7654321....','....6543321....','...76333321....','.....a3321.....','......a21......'],
 'W':['......6542.....','.....765431....','.....654321....','....76533331...','......ab1a.....','......1cba.....'],
 'SW':['.....65421.....','....7654321....','....6543321....','...765333321...','....1a1b1a.....','.....1abcb.....'],
 'S':['......6542.....','.....765431....','.....654321....','....13333321...','.....a1bb1a....','......bcba.....'],
 'NE':['.....65421.....','....7654321....','....6543321....','...133333321...','.....332a1.....','......32a1.....'],
}
FRONT = ['..654gg3gg456..','..54g43gg34541.','..54g443g34431.',
         '..54g443g34431.','..34g43gg34431.','...4g43gg4341..',
         '...1hihhhh11...','...hihhhih1....','...3654g5431...']
BACK = ['..654gggg4561..','..65g6654g541..','..54g5543g431..',
        '..54g5543g431..','..43g4432g331..','...3g3322g31...',
        '...1g2222g11...','...hihhhih1....','...3654g5431...']
WEST = ['.....g654h.....','....1g654h.....','...14g543h.....',
        '...1gg453h.....','...1gg432h.....','...1gg432h.....',
        '....1hhhhg1....','....1hhhih.....','....134563.....']
SW = ['...654ggg4561..','..654g43g34541.','..54g443g34431.',
      '..54g443g34431.','..34g43gg34431.','...4g43gh4341..',
      '...1hihhhh11...','...hihhhih1....','...3654g5431...']
QUARTER_BACK = ['..654g6654g1...','..654g6554g41..','..543g5543g31..',
                '..543g5443g31..','..432g4332g31..','...32g3222g1...',
                '...1hhhhhhh1...','...hihhhih1....','...3654g5431...']
TORSO = {'N':BACK,'NW':QUARTER_BACK,'W':WEST,'SW':SW,'S':FRONT,'NE':QUARTER_BACK}

class ViewSurface(Surface):
 def leg_for_view(self,label,hip,knee,foot,far,support,view):
  self.cloth(hip,knee,3.5,far,('5642','5642','4542','6753'))
  self.cloth(knee,(foot[0],foot[1]-1),3,far,('5642','4542','4542','3421'))
  vx,vy=VECTORS[view]
  offsets=range(-2,2) if vx<-.1 else range(-1,3) if vx>.1 else range(-1,2)
  for x in offsets:
   self.put(foot[0]+x,foot[1]-1,'s' if far or x in(min(offsets),max(offsets)) else 't')
   self.put(foot[0]+x,foot[1],'r')
  self.trace.append({'limb':label,'hip':hip,'knee':knee,'foot':foot,'depth':'far' if far else 'near','support':support})

def frame(stage,view):
 if view in ('E','SE'):
  return previous_frame(stage,view)
 s=ViewSurface();bob=KEYS[stage]['bob'];vx,vy=VECTORS[view];rx,ry=-vy,vx
 phase=stage%3;support=0 if stage<3 else 1
 sides={0:1,1:-1};depth={leg:sides[leg]*ry for leg in sides}
 order=sorted((1,0),key=lambda leg:depth[leg])
 for leg in order:
  side=sides[leg];sx=side*rx*1.8
  if abs(vy)<.1:sx=-side*.5
  sy=side*ry*.2
  hip=(7.5+sx,15+bob+sy)
  along=(3.5,0,-3.5)[phase] if leg==support else (-3,-.5,2.5)[phase]
  lift=0 if leg==support else (4,3,3)[phase]
  ground=23 if abs(vy)<.1 else 22.5
  drop=ground-(15+bob)-lift;distance=hypot(along,drop)
  if not 0<distance<=9:raise ValueError(('Native limb reach',view,stage,leg,distance))
  bend=sqrt(max(0,4.5**2-(distance/2)**2));ka=along/2+drop/distance*bend;kd=drop/2-along/distance*bend
  knee=(hip[0]+vx*ka,hip[1]+kd+vy*ka*.22)
  foot=(hip[0]+vx*along,ground+sy+vy*along*.22-lift)
  s.leg_for_view('AB'[leg],hip,knee,foot,leg==order[0],leg==support,view)
 lean=1 if vx>.1 else -1 if vx<-.1 else 0
 shifted=lambda p:(p[0]+lean,p[1]+bob)
 # Arms have camera-specific overlap; N hides its forward carry behind the body.
 if view=='N':
  s.arm(shifted((3,7)),shifted((2,10)),shifted((4,10)),True)
  s.arm(shifted((11,7)),shifted((12,10)),shifted((10,10)),True)
 elif view in ('NW','NE'):
  left=view=='NW';a=-1 if left else 1
  s.arm(shifted((7+a*3,7)),shifted((7+a*4,10)),shifted((7+a*3,12)),True)
 else:
  # Free arm counter swings. The hand remains attached through every stage.
  ex,ey=((2,10),(2,9.5),(3,9),(3,9),(2,10),(2,10.5))[stage]
  wx,wy=((3,12),(3,11),(4,10),(4,10),(3,11),(3,12))[stage]
  if view=='W':ex,wx=14-ex,14-wx
  s.arm(shifted((11,7) if view=='W' else (3,7)),shifted((ex,ey)),shifted((wx,wy)),True)
 for y,row in enumerate(TORSO[view],6):
  for x,c in enumerate(row):
   if c!='.':s.put(x+(lean if y<10 else 0),y+bob,c)
 # Blue belongs to the waist/back equipment visible in the original action.
 # Its exact item name is unresolved, so no invented accessory is asserted.
 blues={
  'N':[(4,12,'noppqn'),(4,13,'nooonn')],
  'NW':[(10,10,'npn'),(10,11,'opn'),(10,12,'pqn'),(10,13,'non')],
  'W':[(10,10,'npo'),(10,11,'nqp'),(10,12,'npo'),(10,13,'nn')],
  'SW':[(11,11,'npn'),(11,12,'pqn'),(11,13,'non')],
  'S':[(3,12,'np'),(3,13,'op'),(11,12,'pn'),(11,13,'on')],
  'NE':[(2,10,'opn'),(2,11,'pqn'),(2,12,'opn'),(2,13,'nn')],
 }
 for x,y,row in blues[view]:
  for dx,c in enumerate(row):s.put(x+dx,y+bob,c)
 if view=='S':
  s.arm(shifted((11,7)),shifted((11,10)),shifted((8,11)))
  gx,gy=shifted((8,10))
 elif view=='SW':
  s.arm(shifted((5,7)),shifted((4,10)),shifted((3,12)))
  gx,gy=shifted((2,11))
 elif view=='W':
  s.arm(shifted((6,7)),shifted((6,10)),shifted((3,11)))
  gx,gy=shifted((2,10))
 elif view in ('NW','NE'):
  # Only a forward skin sliver is visible; the carried grenade is occluded.
  a=-1 if view=='NW' else 1
  counter=(1,0,-1,-1,0,1)[stage]
  s.arm(shifted((7+a*3,7)),shifted((7+a*4,9+counter*.5)),shifted((7+a*5,10+counter)))
  gx=gy=None
 else:gx=gy=None
 if gx is not None:
  s.put(gx,gy-1,'m');s.put(gx-1,gy,'w');s.put(gx,gy,'v');s.put(gx+1,gy,'u');s.put(gx,gy+1,'u')
 for y,row in enumerate(HEAD[view]):
  for x,c in enumerate(row):
   if c!='.':s.put(x+lean,y+bob,c)
 return s

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);ap.add_argument('--analysis',type=Path,required=True);a=ap.parse_args()
 analysis=json.loads(a.analysis.read_text())
 if set(analysis['views'])!=set(VIEWS):raise RuntimeError('Read all eight original actions first')
 if a.out.exists():raise RuntimeError('Versioned output required')
 a.out.mkdir(parents=True);drawn={};records=[]
 for view in VIEWS:
  drawn[view]=[]
  for i in range(6):
   s=frame(i,view);p=a.out/f'{view}-run-{i}.png';s.im.save(p);s.mask.save(a.out/f'{view}-run-{i}-player-mask.png');s.roles.save(a.out/f'{view}-run-{i}-paint-roles.png');drawn[view].append(s.im)
   records.append({'facing':view,'stage':i,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'phase':KEYS[i]['phase'],'limbs':s.trace,'review':'UNREVIEWED'})
 # Original pixels enter here only, after all new native drawing is complete.
 original=Image.open(a.study/'E2/original.png').convert('RGBA');font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
 for view in VIEWS:
  board=Image.new('RGB',(1320,560),(25,28,33));d=ImageDraw.Draw(board);gif=[]
  for i in range(6):
   n=16+VIEWS.index(view)*6+i;before=original.crop((n%20*50,n//20*39,n%20*50+50,n//20*39+39)).resize((100,78),Image.Resampling.NEAREST)
   pair=Image.new('RGB',(560,330),(25,28,33));pd=ImageDraw.Draw(pair)
   for row,im in enumerate((before,drawn[view][i])):
    tile=im.crop((38,12,68,44)).resize((240,256),Image.Resampling.NEAREST);pair.paste(tile,(row*280+20,45),tile);pd.text((row*280+20,12),view+' · '+('DOS' if row==0 else 'NATIVE v17'),font=font,fill='#e7bd70')
    tile=im.crop((40,14,64,42)).resize((192,224),Image.Resampling.NEAREST);board.paste(tile,(i*220+12,row*280+36),tile);d.text((i*220+8,row*280+8),f'{view} · '+('DOS' if row==0 else 'v17')+f' · {i}',font=font,fill='#e7bd70')
   gif.append(pair)
  board.save(a.out/f'{view}-six-frame-audit.png');gif[0].save(a.out/f'{view}-run-comparison.gif',save_all=True,append_images=gif[1:],duration=[130,140,130,130,140,130],loop=0)
 (a.out/'frames.json').write_text(json.dumps({'scope':'All eight Grenadier run views; native visual acceptance pending','anchor':[54,40],'source_fps':7.5,'views':VIEWS,'keys':records},indent=2)+'\n')
 shutil.copy2(a.analysis,a.out/'source-analysis.json');(a.out/'authoring-source').mkdir()
 for name in ('e2_run_study_v17.py','e2_run_study_v15.py'):shutil.copy2(Path(__file__).with_name(name),a.out/'authoring-source'/name)
 print('48 native run frames, eight original/native boards:',a.out)

if __name__=='__main__':main()
