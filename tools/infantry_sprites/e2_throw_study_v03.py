#!/usr/bin/env python3
"""New overhand Grenadier throw, authored from the whole-action semantic brief.

Corrected native head/cloth/equipment are articulated around the waist. Original
DOS raster enters only the evidence compositor after drawing. Private study.
"""
from pathlib import Path
from math import hypot, sqrt
import argparse, hashlib, json, shutil
from PIL import Image, ImageDraw, ImageFont
from e2_run_study_v15 import Surface, HEAD, TORSO, TORSO_SE, px

# Hand paths are new overhand keys, not traced DOS coordinates. Near arm throws;
# free arm primes/balances. Each tuple: bob, torso lean, forward dip, elbow,
# wrist, free wrist, forward foot, rear foot, rear heel height.
KEYS = [
 (0,0,0,(10,10),(13,11),(12,10),10,5,23),
 (1,0,0,(10,10),(13,11),(12,10),10,5,23),
 (0,0,0,(10,9),(13,10),(11,10),10,5,23),
 (0,0,0,(10,10),(13,11),(11,11),10,5,23),
 (0,0,0,(10,9),(12,10),(11,10),10,5,23),
 (1,0,0,(10,9),(12,10),(11,11),10,5,23),
 (0,0,0,(10,10),(12,11),(10,11),10,5,23),
 (0,0,0,(10,9),(12,10),(10,10),10,5,23),
 (0,0,0,(9,9),(11,10),(8,11),10,5,23),
 (1,0,0,(9,9),(10,9),(7,11),11,4,23),
 (1,-1,0,(9,8),(8,6),(5,12),12,3.5,23),
 (0,-1,0,(6,7),(3,4),(5,13),12,3.5,23),
 (0,-1,0,(5,6),(3,2),(5,13),12,3.5,23),
 (0,-1,0,(7,4),(8,0),(5,13),12,3.5,23),
 (0,0,0,(10,3),(12,-1),(5,13),12,3.5,22),
 (1,1,0,(13,5),(16,2),(5,12),12,3.5,22),
 (0,3,2,(13,8),(15,9),(5,11),12,3.5,22),
 (0,1,1,(12,10),(13,11),(6,13),11,4.5,23),
 (0,0,0,(10,10),(11,12),(6,12),10,5,23),
 (0,0,0,(10,9),(11,11),(7,11),10,5,23),
]
# The original E throw deliberately holds exactly the same art at2/4.
# Preserve that hold/count/timing explicitly, rather than padding missing motion.
KEYS[4] = KEYS[2]
PHASES = ['prepare']*8 + ['gather','step/load','draw back','cock','cock','raise','release','extend','follow through','rise','recover','ready']

def frame(stage):
 s=Surface();bob,lean,dip,elbow,wrist,free,front,rear,rear_y=KEYS[stage]
 for leg,foot in ((1,(rear,rear_y)),(0,(front,23))):
  hip=(8 if leg==0 else 7,15+bob);dx,dy=foot[0]-hip[0],foot[1]-hip[1];dist=hypot(dx,dy)
  if not 0<dist<=9:raise ValueError(('Planned short leg reach',stage,leg,dist))
  bone=max(4.2,dist/2+.03);h=sqrt(max(0,bone**2-(dist/2)**2));knee=(hip[0]+dx/2+dy/dist*h,hip[1]+dy/2-dx/dist*h)
  s.leg('AB'[leg],hip,knee,foot,leg==1,True)
 xshift=lambda y:(1 if y<10 else 0)+px(lean*(14-y)/8)
 yshift=lambda y:bob+px(dip*(14-y)/8)
 arm_shift=(xshift(7),yshift(7))
 arm=lambda p:(p[0]+arm_shift[0],p[1]+arm_shift[1])
 s.arm(arm((5,7)),arm((5,10)),arm(free),True)
 # Repaint the editable native garment planes around a waist hinge, preserving
 # the original native material clusters. The feet/actor root never translate.
 pack=['..hih1','..iih1','..ihh1','.hihh1','.opnh1','.pqnh1','.opnh1','..nnh1']
 for py,row in enumerate(pack,6):
  for x,c in enumerate(row):
   if c!='.':s.put(x+px(lean*(14-py)/8),py+yshift(py),c)
 core=list(TORSO)
 if 9<=stage<=12:core[:6]=TORSO_SE[:6]
 elif stage in(8,13):core[:3]=TORSO_SE[:3]
 elif stage==14:core[:1]=TORSO_SE[:1]
 for y,row in enumerate(core,6):
  for x,c in enumerate(row):
   if c!='.':s.put(x+xshift(y),y+yshift(y),c)
 # Crown/brim, eye/nose, jaw and neck always move together, matching chest hinge.
 for y,row in enumerate(HEAD['E']):
  for x,c in enumerate(row):
   if c!='.':s.put(x+1+lean,y+bob+dip,c)
 # Near arm and held grenade are foreground layers. Drawing the head last
 # erased the cocked grenade; keep the raised wrist and prop readable.
 s.arm(arm((10 if 9<=stage<=12 else 9,7)),arm(elbow),arm(wrist))
 if stage<14:
  gx,gy=arm(wrist);s.put(gx,gy-1,'m');s.put(gx-1,gy,'w');s.put(gx,gy,'v');s.put(gx+1,gy,'u');s.put(gx,gy+1,'u')
 s.trace.append({'part':'throwing_hand','wrist':arm(wrist),'grenade_visible':stage<14,'projectile_release':stage==14,'torso_lean':lean,'dip':dip})
 return s

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);ap.add_argument('--analysis',type=Path,required=True);a=ap.parse_args()
 analysis=json.loads(a.analysis.read_text());assert analysis['release_stage']==14 and len(analysis['views'])==8
 if a.out.exists():raise RuntimeError('Versioned private study required')
 a.out.mkdir(parents=True);records=[];drawn=[]
 for i in range(20):
  s=frame(i);p=a.out/f'E-throw-{i:02}.png';s.im.save(p);s.mask.save(a.out/f'E-throw-{i:02}-player-mask.png');s.roles.save(a.out/f'E-throw-{i:02}-paint-roles.png');drawn.append(s.im)
  records.append({'stage':i,'phase':PHASES[i],'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'parts':s.trace,'review':'UNREVIEWED'})
 # Evidence only: source pixels were not read while drawing any new frame.
 original=Image.open(a.study/'E2/original.png').convert('RGBA');font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16);gif=[]
 for group in range(4):
  board=Image.new('RGB',(1200,660),(25,28,33));d=ImageDraw.Draw(board)
  for col,i in enumerate(range(group*5,group*5+5)):
   n=184+i;old=original.crop((n%20*50,n//20*39,n%20*50+50,n//20*39+39)).resize((100,78),Image.Resampling.NEAREST)
   pair=Image.new('RGB',(680,430),(25,28,33));pd=ImageDraw.Draw(pair)
   for row,im in enumerate((old,drawn[i])):
    tile=im.crop((36,10,72,44)).resize((216,204),Image.Resampling.NEAREST);board.paste(tile,(col*240+12,row*300+50),tile);d.text((col*240+10,row*300+14),f'{i} · '+('DOS' if row==0 else 'NATIVE')+' · '+PHASES[i],font=font,fill='#e7bd70')
    tile=im.crop((32,6,76,48)).resize((308,294),Image.Resampling.NEAREST);pair.paste(tile,(row*340+16,60),tile);pd.text((row*340+16,20),('DOS' if row==0 else 'NEW THROW STUDY'),font=font,fill='#e7bd70')
   pd.text((16,390),f'{i+1}/20 · '+PHASES[i]+' · release14',font=font,fill='#aeb8bf');gif.append(pair)
  board.save(a.out/f'five-frame-audit-{group}.png')
 gif[0].save(a.out/'east-throw-comparison.gif',save_all=True,append_images=gif[1:],duration=[70,60,70]*6+[70,60],loop=0)
 (a.out/'frames.json').write_text(json.dumps({'scope':'Private E2E20stage throw; not audited or published','source_start':184,'source_fps':15,'release_stage':14,'anchor':[54,40],'keys':records},indent=2)+'\n');shutil.copy2(a.analysis,a.out/'source-analysis.json');(a.out/'authoring-source').mkdir()
 for name in ('e2_throw_study_v03.py','e2_run_study_v15.py'):shutil.copy2(Path(__file__).with_name(name),a.out/'authoring-source'/name)
 print('20private throw frames and4source/native audit boards:',a.out)

if __name__=='__main__':main()
