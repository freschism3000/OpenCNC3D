#!/usr/bin/env python3
"""Six independently painted throw cameras; preserve exact accepted E/SE art.

Whole action/support/occlusion/hold analysis is mandatory before this author.
Original raster enters the evidence compositor only after new drawing.
"""
from pathlib import Path
import argparse,hashlib,json,shutil
from PIL import Image,ImageDraw,ImageFont
from e2_run_study_v28 import Camera,TORSO,HEAD_X,ROOT,px

VIEWS=('N','NW','W','SW','S','SE','E','NE')
NEW=('N','NW','W','SW','S','NE')
HOLDS={'N':[[1,19],[5,6,7,8,9],[11,13]],'NW':[[2,4],[5,6,7,8,9]],
 'W':[[1,19],[2,4,10]],'SW':[[1,19],[2,4],[7,8]],
 'S':[[1,5,19],[6,7]],'NE':[[1,5,19],[2,4],[6,7,8]]}
DROPS={
 'N':[0,1,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,1],
 'NW':[0,1,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,2,0],
 'W':[0,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2],
 'SW':[0,1,0,0,0,1,0,0,0,2,3,3,3,3,3,3,3,3,2,1],
 'S':[0,1,0,0,0,1,0,0,1,2,3,3,3,3,3,3,3,3,2,1],
 'NE':[0,1,1,2,1,1,2,2,2,2,3,3,3,3,3,3,3,3,2,1],
}
# Explicit camera leg layouts. A is the forward-foot leg, B the grounded-knee
# leg. In quarter/rear cameras their contact screen-y follows ground depth.
STAND={
 'W':(((7,14),(7,18),(6,22)),((8,14),(9,18),(9,22))),
 'SW':(((9,15),(9,19),(9,23)),((6,14),(6,18),(6,22))),
 'NW':(((6,15),(6,19),(6,22.5)),((9,14),(9,18),(9,22))),
 'NE':(((9,15),(9,19),(9,22.5)),((6,14),(6,18),(6,22))),
 'N':(((6,15),(6,19),(6,23)),((10,15),(10,19),(10,23))),
 'S':(((6,15),(6,19),(6,23)),((10,15),(10,19),(10,23))),
}
KNEEL={
 'W':(((7,17),(3,17.5),(3,22)),((8,17),(10,21),(13.5,22))),
 'SW':(((9,18),(4.5,18.7),(4,23.3)),((6,17),(9,20.5),(12.5,19.5))),
 'NW':(((6,18),(2.5,18),(2,21.5)),((9,17),(10.5,21.5),(13.5,23))),
 'NE':(((9,18),(12.5,18),(12.5,21.5)),((6,17),(4.5,21.5),(1.5,23))),
 'N':(((6,18),(6,18.5),(6,20.5)),((10,18),(10,22),(10,24))),
 'S':(((6,18),(4,19.5),(3.5,23.5)),((10,18),(11.5,21.5),(10.5,18.5))),
}
SHOULDER={'W':(9,7),'SW':(10,8),'S':(4,7),'N':(11,7),'NW':(10,7),'NE':(10,8)}
FREE_SHOULDER={'W':(11,7),'SW':(4,7),'S':(11,7),'N':(4,7),'NW':(4,8),'NE':(4,7)}
# Elbow/wrist choreography, newly keyed for the actual camera silhouette.
PATH={
 'W':[((8,10),(4,11)),((8,9),(4,10)),((8,9),(4,10)),((7,9),(4,11)),((8,9),(4,10)),
      ((8,10),(5,11)),((9,10),(6,11)),((8,9),(5,10)),((8,8),(6,9)),((9,8),(7,7)),
      ((8,9),(4,10)),((11,7),(15,4)),((11,5),(13,1)),((10,3),(9,-2)),((7,3),(5,-2)),
      ((4,5),(1,1)),((4,9),(2,10)),((5,10),(4,12)),((7,11),(6,13)),((8,9),(4,10))],
 'SW':[((9,11),(4,12)),((8,10),(5,10)),((9,10),(4,11)),((8,10),(3,11)),((9,10),(4,11)),
       ((9,10),(5,11)),((10,11),(6,12)),((9,10),(5,10)),((9,10),(5,10)),((8,10),(5,9)),
       ((8,7),(4,6)),((7,6),(3,3)),((6,5),(3,2)),((7,4),(4,-1)),((13,4),(13,-1)),
       ((6,5),(2,2)),((6,11),(3,14)),((8,11),(5,13)),((9,11),(5,12)),((8,10),(5,10))],
 'S':[((4,10),(8,11)),((5,9),(8,10)),((4,10),(7,11)),((3,9),(6,10)),((4,9),(6,11)),
      ((5,9),(8,10)),((3,10),(5,10)),((3,10),(5,10)),((3,9),(5,8)),((4,8),(5,6)),
      ((3,7),(3,4)),((2,6),(1,3)),((2,5),(3,1)),((3,4),(3,-1)),((2,3),(2,-2)),
      ((4,5),(5,2)),((5,10),(7,13)),((4,11),(7,13)),((4,11),(8,12)),((5,9),(8,10))],
 'N':[((9,10),(7,11)),((9,9),(7,10)),((9,8),(6,9)),((8,8),(6,9)),((8,8),(6,9)),
      ((9,10),(7,11)),((9,10),(7,11)),((9,10),(7,11)),((9,10),(7,11)),((9,10),(7,11)),
      ((7,8),(3,8)),((7,5),(2,2)),((7,5),(1,3)),((7,5),(2,2)),((13,7),(16,7)),
      ((12,3),(12,-1)),((10,4),(7,0)),((9,9),(7,11)),((9,10),(7,12)),((9,9),(7,10))],
 'NW':[((8,10),(5,11)),((9,10),(6,10)),((8,10),(5,11)),((9,9),(6,10)),((8,10),(5,11)),
       ((9,10),(6,11)),((9,10),(6,11)),((9,10),(6,11)),((9,10),(6,11)),((9,10),(6,11)),
       ((7,9),(3,8)),((7,6),(2,4)),((6,5),(2,1)),((7,4),(3,0)),((13,7),(16,7)),
       ((12,4),(13,0)),((6,6),(4,4)),((8,10),(5,12)),((9,10),(6,12)),((10,9),(7,11))],
 'NE':[((11,10),(12,11)),((12,10),(12,10)),((12,9),(13,10)),((12,9),(12,9)),((12,9),(13,10)),
       ((12,10),(12,10)),((11,9),(10,10)),((11,9),(10,10)),((11,9),(10,10)),((11,8),(11,8)),
       ((11,8),(11,7)),((12,6),(13,4)),((13,5),(14,2)),((12,3),(14,0)),((6,7),(2,7)),
       ((6,3),(5,-1)),((12,6),(13,2)),((11,9),(10,11)),((11,10),(11,11)),((12,10),(12,10))],
}
FREE={
 'N':[(5,10),(4,10),(3,10),(2,10),(3,10),(5,11),(5,11),(5,11),(5,11),(5,11),(2,12),(1,13),(1,13),(1,13),(1,13),(1,13),(2,13),(3,12),(4,12),(4,10)],
 'NW':[(4,11),(5,11),(4,12),(4,11),(4,12),(5,12),(5,12),(5,12),(5,12),(5,12),(2,12),(2,13),(2,13),(2,13),(1,13),(1,13),(2,13),(3,12),(4,12),(1,10)],
 'NE':[(4,10),(3,10),(3,11),(2,11),(3,11),(3,10),(3,12),(3,12),(3,12),(2,12),(1,13),(1,13),(1,13),(1,13),(12,12),(12,12),(12,11),(11,12),(3,11),(3,10)],
 'W':[(10,10),(10,11),(10,10),(11,11),(10,10),(11,12),(11,13),(11,12),(11,12),(12,13),(10,10),(12,13),(12,13),(12,13),(12,13),(11,12),(11,11),(10,12),(10,12),(10,11)],
 'SW':[(3,10),(4,10),(3,10),(3,11),(3,10),(4,11),(3,11),(3,12),(3,12),(3,13),(3,14),(3,14),(3,14),(3,14),(3,14),(3,13),(3,12),(4,12),(4,11),(4,10)],
 'S':[(10,10),(10,11),(11,10),(12,11),(11,11),(10,11),(12,11),(12,11),(12,12),(13,12),(14,12),(14,11),(14,10),(14,10),(15,8),(14,10),(12,12),(11,12),(10,12),(10,11)],
}

def representative(view,stage):
 return next((group[0]for group in HOLDS[view]if stage in group),stage)

def head(s,view,drop,lx,dip):
 im=Image.open(ROOT/f'art/sprites/minigunner-snes-v01/animation-v06/native-parts/{view}-complete-master.png').convert('RGBA');h=im.crop((0,0,im.width,6));box=h.getbbox();h=h.crop((box[0],0,box[2],6))
 cloth={tuple(bytes.fromhex(c))for c in('4b371f','6e502a','967438','b78e43','cca856','e3bf73','f2d794','fff1ad')}
 for y in range(6):
  for x in range(h.width):
   c=h.getpixel((x,y))
   if c[3]:
    p=(44+HEAD_X[view]+x+lx,16+y+drop+dip);s.im.putpixel(p,c);active=c[:3]in cloth;s.mask.putpixel(p,255 if active else 0);s.roles.putpixel(p,4 if active else 0)

class ThrowCamera(Camera):
 def boot(self,x,y,view,far):
  # The front-facing kneel's folded rear foot is behind the thigh, not a
  # floating dark boot on the waist. The rear knee remains fully visible.
  if not (view=='S' and far and self.hide_rear_boot):super().boot(x,y,view,far)

def frame(stage,view,ready=False):
 i=0 if ready else representative(view,stage);drop=DROPS[view][i];s=ThrowCamera();s.hide_rear_boot=drop==3;t=drop/3
 legs=tuple(tuple(tuple(a+(b-a)*t for a,b in zip(pa,pb))for pa,pb in zip(la,lb))for la,lb in zip(STAND[view],KNEEL[view]))
 for leg in(1,0):
  s.leg_key(legs[leg],view,leg==1,True);s.trace[-1]['support_kind']='grounded knee'if leg==1 and drop==3 else'forward planted foot'if leg==0 else'foot / lowering'
 direction=-1 if view in('W','NW','SW')else 1 if view=='NE'else 0
 lean=direction*(-1 if 10<=i<=13 else 1 if i in(15,17)else 3 if i==16 and view=='W'else 2 if i==16 else 0)
 dip=(1 if view=='N'else 2)if i==16 else 1 if i==17 else 0
 sx=lambda y:px(lean*(14-y)/8);sy=lambda y:drop+px(dip*(14-y)/8);arm=lambda p:(p[0]+sx(7),p[1]+sy(7))
 fs=FREE_SHOULDER[view];fw=FREE[view][i];fe=(fs[0]+(-2 if fs[0]<7 else 2),9)
 
 if view=='NE' and 14<=i<=17:fe=(7,10)
 s.arm(*map(arm,(fs,fe,fw)),far=view not in('N','NW'))
 elbow,wrist=PATH[view][i];rear=view in('N','NW','NE')
 if rear:s.arm(*map(arm,(SHOULDER[view],elbow,wrist)),far=view in('N','NW'))
 if view=='W':
  for y,row in enumerate(['1hih','1iih','1ihh','1ihh','1npo','1nqp','1npo','1nno'],6):s.rows(10+sx(y),y+sy(y),[row])
 elif view=='SW':
  for y,row in enumerate(['hi1','ih1','ih1'],8):s.rows(11+sx(y),y+sy(y),[row])
 elif view in('NW','NE'):
  for y,row in enumerate(['hih1','iih1','ihh1','ihh1','opn1','pqn1','pon1','oon1'],6):s.rows((9 if view=='NW'else 2)+sx(y),y+sy(y),[row])
 for x,y,row in TORSO[view]:s.rows(x+sx(y),y+sy(y),[row])
 if view=='N':
  s.rows(4+sx(12),12+drop,['nopppqon','noppopon'])
 elif view=='S':
  pouches=((2,['opn','pqn','onn']),(11,['npo','nqp','nno'])) if dip else ((3,['opn','pqn']),(10,['npo','nqp']))
  for x,rows in pouches:s.rows(x+sx(12),12+drop,rows)
 else:
  s.rows((11 if view=='SW'else 10 if view in('W','NW')else 1)+sx(12),11+drop,['npo','nqp','npo','nno']if view in('W','NW','SW')else['opn','pqn','pon','onn'])
 if view in('N','NW','NE') and 14<=i<=16:
  s.cloth(arm(SHOULDER[view]),arm(elbow),3.8,far=view!='NE',material=('6753','5643','4532','3421'));s.cloth(arm(elbow),arm(wrist),3.0,far=view!='NE')
 if view in('N','NW') and 10<=i<=13:s.cloth(arm(elbow),arm(wrist),3.0,far=True)
 if not rear:s.arm(*map(arm,(SHOULDER[view],elbow,wrist)))
 head(s,view,drop,lean,dip)
 exposed_hand=not rear or view in('NW','NE')or i in(1,2,4,10,11,12,14,15,16)
 if 11<=i<=16 and not rear:s.cloth(arm(elbow),arm(wrist),3.3)
 if view in('N','NW'):exposed_hand=10<=i<=16
 if exposed_hand:
  wx,wy=arm(wrist);s.put(wx,wy,'b');s.put(wx+1,wy,'c');s.put(wx+1,wy+1,'a')
 held=stage<14 or ready;visible=held and i!=1 and(view not in('N','NW','NE')or view in('NW','NE')and 10<=i<=13)
 if visible:
  gx,gy=arm(wrist);s.rows(gx-1,gy-2,['.m.','wvu','.u.'])
 s.trace.append(dict(part='throw',view=view,stage=stage,pose_index=i,drop=drop,lean=lean,dip=dip,kneeling=drop==3,release=stage==14 and not ready,grenade_held=held,grenade_visible=visible,exposed_hand=exposed_hand,actor_anchor=[54,40],throwing_shoulder=arm(SHOULDER[view]),balance_shoulder=arm(FREE_SHOULDER[view]),anatomical_throw_arm='right',anatomical_balance_arm='left',wrist=arm(wrist)))
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
     tile=im.crop((32,6,76,48)).resize((220,210),Image.Resampling.NEAREST);board.paste(tile,(col*240+10,row*300+45),tile);d.text((col*240+8,row*300+12),f'{view} / {k:02} / '+('DOS'if row==0 else'NEW49'),font=font,fill='#e7bd70');tile=im.crop((32,6,76,48)).resize((308,294),Image.Resampling.NEAREST);pair.paste(tile,(row*400+44,45),tile);pd.text((row*400+20,15),view+' / '+('DOS'if row==0 else'NEW49')+f' / {k+1}:20',font=font,fill='#e7bd70')
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
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--study',type=Path,required=True);ap.add_argument('--analysis',type=Path,required=True);a=ap.parse_args();brief=json.loads(a.analysis.read_text());assert brief['before_new_art']and'native_plan_before_drawing'in brief
 if a.out.exists():raise RuntimeError('Versioned output required')
 a.out.mkdir(parents=True);previous=ROOT/'art/sprites/infantry-snes-v01/motion-study-v46-e2-throw'
 for p in previous.glob('*.png'):
  if (p.name.startswith(tuple(v+'-'for v in VIEWS if v not in('N','NE'))) and ('-throw-'in p.name or '-ready'in p.name)) and 'audit'not in p.name:shutil.copy2(p,a.out/p.name)
 records=[r for r in json.loads((previous/'frames.json').read_text())['keys']if r['view']not in('N','NE')]
 global NEW
 NEW=('N','NE')
 for view in NEW:
  for k in range(20):
   s=frame(k,view);p=a.out/f'{view}-throw-{k:02}.png';s.im.save(p);s.mask.save(p.with_stem(p.stem+'-player-mask'));s.roles.save(p.with_stem(p.stem+'-paint-roles'));records.append(dict(view=view,stage=k,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),parts=s.trace,review='UNREVIEWED'))
  s=frame(0,view,ready=True);s.im.save(a.out/f'{view}-ready.png');s.mask.save(a.out/f'{view}-ready-player-mask.png');s.roles.save(a.out/f'{view}-ready-paint-roles.png')
 (a.out/'frames.json').write_text(json.dumps(dict(scope='N/NE arm corrections only; six other views exact v46copies',anchor=[54,40],fps=15,release_stage=14,keys=records),indent=2)+'\n');shutil.copy2(a.analysis,a.out/'source-action-brief.json');evidence(a.out,a.study)
 author=a.out/'authoring-source';author.mkdir()
 for name in('e2_throw_study_v20.py','e2_run_study_v28.py','e2_run_study_v23.py','e2_run_study_v15.py'):shutil.copy2(Path(__file__).with_name(name),author/name)
 print(a.out)

if __name__=='__main__':main()
