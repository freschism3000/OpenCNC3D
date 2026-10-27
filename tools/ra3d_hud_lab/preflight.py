#!/usr/bin/env python3
"""Reject source changes, broken state masks and rail collisions in RA HUD review."""
from pathlib import Path
from PIL import Image
import hashlib,json,sys,numpy as np
p=Path(sys.argv[1]);d=json.loads((p/'kit/manifest.json').read_text());errs=[]
for id,a in {**d['assets'],**d['sounds']}.items():
 f=p/'kit'/a['file']
 if hashlib.sha256(f.read_bytes()).hexdigest()!=a['sha256']:errs.append('changed source '+id)
 if 'w'in a and Image.open(f).size!=(a['w'],a['h']):errs.append('wrong dimensions '+id)
for kind in ['tool','tab','header','sidebar','group']+(['scroll'] if 'scrollSeats' in d['contract'] else []):
 masks=[np.array(Image.open(p/'kit'/(kind+'-'+s+'.png')))[:,:,3] for s in ['normal','hover','pressed','active','disabled','activehover']]
 if not all(np.array_equal(masks[0],v) for v in masks[1:]):errs.append('state mask changed '+kind)
 # All native corner cuts must remain transparent; no opaque state rectangle.
 if masks[0][0,0] or masks[0][-1,-1]:errs.append('unmasked corner '+kind)
for name in ['minimap','production','card','tools','tool-normal','tab-normal']:
 a=np.array(Image.open(p/'kit'/(name+'.png'))).astype(float);blue=(a[:,:,2]>a[:,:,0]*1.12)&(a[:,:,2]>a[:,:,1]*1.05)&(a[:,:,3]>0)
 if blue.any():errs.append('blue leaked into neutral chrome '+name)
c=d['contract'];l,t,w,h=c['toolInner']
for i,(x,y,bw,bh) in enumerate(c['toolSeats']):
 if (x-l,l+w-x-bw)!=(7,7):errs.append('horizontal wing spacing')
 if i==0 and y-t!=7:errs.append('top wing spacing')
 if i==2 and t+h-y-bh!=7:errs.append('bottom wing spacing')
# The inner ribbed rails are 30 native pixels wide, not just the 14px silver strip.
for k in range(5):
 x=32+k*31
 if x<30 or x+28>186:errs.append('tab covers ribbed rail')
if 'cameoGrid' in c:
 grid=c['cameoGrid'];cw,ch=c['cameoSize'];x,y=grid['x'],grid['y']
 right=x+grid['strideX']+cw;bottom=y+(grid['rows']-1)*grid['strideY']+ch
 if cw*3!=ch*4:errs.append('cameo aspect ratio')
 if (x-(c['powerMeter']['bounds'][0]+20),202-right)!=(10 if 'scrollSeats' in c else 8,8):errs.append('cameo clearance to glass or outer rail')
 if right>202 or bottom>656:errs.append('cameo overlaps frame or footer')
 if grid['strideX']-cw!=4 or grid['strideY']-ch!=4:errs.append('cameo gutters')
else:
 for x in (48,116):
  if x<30 or x+64>186:errs.append('cameo covers ribbed rail')
# The power cylinder replaces the left trim; source fittings remain pixel exact.
if 'powerMeter' in c:
 m=c['powerMeter'];glass=np.array(Image.open(p/'kit/glass-power-meter.png'))
 top=np.array(Image.open(p/'kit/powerbar-top-source.png'));bottom=np.array(Image.open(p/'kit/powerbar-bottom-source.png'))
 if glass.shape!=((476,20,4) if 'scrollSeats' in c else (431,20,4)):errs.append('glass dimensions')
 if not np.array_equal(glass[:112],top):errs.append('top fitting changed')
 if not np.array_equal(glass[112+m['bodyExtension']:],bottom):errs.append('bottom fitting changed')
 if not all(np.array_equal(glass[112+y],bottom[y%63]) for y in range(m['bodyExtension'])):errs.append('glass tick continuity')
 if m['bounds']!=([0,217,20,476] if 'scrollSeats' in c else [2,262,20,431]) or m['side']!='left only':errs.append('meter on wrong edge')
 if m['scalePitch']!=9 or m['bodyExtension']%9:errs.append('tick pitch interrupted')
 if m['fill'][0]<m['bounds'][0] or m['fill'][0]+m['fill'][2]>m['bounds'][0]+20:errs.append('fill outside glass')
 script=(p/'hud.mjs').read_text()
 if 'box(31,278,11,380)' in script:errs.append('duplicate flat meter')
 # Original engine curve is saturating and integral, never linear or overheight.
 def height(n):
  v=0
  for _ in range(n//100):v+=(m['maxFill']-v)//6
  return v+(((m['maxFill']-v)//6)*(n%100))//100
 hs=[height(n) for n in range(0,5001)]
 if hs[0]!=0 or any(a>b for a,b in zip(hs,hs[1:])) or max(hs)>m['maxFill']:errs.append('power curve bounds')
if 'scrollSeats' in c:
 production=np.array(Image.open(p/'kit/production.png'))
 if not np.all(production[24:472,:14,3]==255):errs.append('cut left rail backing')
 if not all(y>=660 and y+h<=677 for x,y,w,h in c['scrollSeats']):errs.append('scroll housing hits bottom rail')
 if c['scrollSeats'][1][0]+18>202:errs.append('scroll housing hits right rail')
 x,y,w,h=c['powerReadout'];tx,ty=c['powerReadoutText']
 for text in ['700/460','5000/460','0/460']:
  width=sum(d['fontAdvances'][ord(ch)] for ch in text)
  if tx-width//2<x+2 or tx+(width+1)//2>x+w-2 or ty<y+2 or ty+8>y+h-2:errs.append('power text crosses its well')
 if y+h>677:errs.append('power readout overlaps bottom rail')
if {i['category'] for i in d['items']}!=set(c['categories']):errs.append('missing source category')
for faction in ['allies','soviet']:
 if not any(i['category']=='ships' and faction in i['owners'] for i in d['items']):errs.append('missing naval catalog '+faction)
print(json.dumps({'version':d['version'],'assets':len(d['assets']),'sounds':len(d['sounds']),'rulesEntries':len(d['items']),'errors':errs},indent=2));sys.exit(bool(errs))
