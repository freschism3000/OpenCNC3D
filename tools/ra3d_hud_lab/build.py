#!/usr/bin/env python3
"""RA3D HUD review kit. Reuses approved RA menu source art; no runtime mutation."""
import argparse,configparser,copy,hashlib,importlib.util,json,sys
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--out',type=Path,required=True);a=p.parse_args();root=a.source;out=a.out;out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('ra_menu',root/'menu/bake_ra_enhanced_menu.py');ra=importlib.util.module_from_spec(spec);spec.loader.exec_module(ra);theme=copy.deepcopy(ra.THEMES['blue'])
# User amendment: the HUD is neutral like the original sidebar, not the blue dialog.
# Keep original rail values/brass; neutralize blue in UI chrome only. Content keeps its palette.
def neutral(c):
 v=round(c[0]*.2126+c[1]*.7152+c[2]*.0722)
 return (v,v,v) if c[2]>c[0]*1.12 and c[2]>c[1]*1.05 else tuple(c)
theme.P=[neutral(c) for c in theme.P]
theme.field=(18,20,19);theme.line=(28,31,29)
theme.ramp=[(216,218,213),(174,178,174),(134,140,135),(104,110,104),(82,88,82),(61,67,61),(40,45,40),(17,21,18)]
assets={}
def save(name,im):
 f=name+'.png';im.save(out/f);assets[name]={'file':f,'w':im.width,'h':im.height,'sha256':hashlib.sha256((out/f).read_bytes()).hexdigest()}
# Exact original frame profiles at their native density, independently fitted sizes.
for name,size in [('minimap',(216,187)),('card',(272,166))]:save(name,ra.frame(*size,theme))
# Fit the production housing from independent field/rail/corner components.
# No rectangle-cut left rail and no residual mirrored ribbing at either end.
def housing(w,h,left=True,right=True,corners=(0,1,2,3)):
 im=Image.fromarray(ra.field(w,h,theme,w//2-312,h//2-192)).convert('RGBA')
 for side,xx,on in [('DD-LEFT',0,left),('DD-RIGHT',w-14,right)]:
  if on:
   strip=ra.piece(side,0,theme)
   for yy in range(-200,h,200):im.alpha_composite(strip,(xx,yy))
 for side,yy in [('DD-TOP',0),('DD-BOTM',h-16)]:
  strip=ra.piece(side,0,theme)
  for xx in range(w//2-320,w,320):im.alpha_composite(strip,(xx,yy))
 for i,(xx,yy) in enumerate([(0,0),(w-23,0),(0,h-24),(w-23,h-24)]):
  if i in corners:im.alpha_composite(ra.piece('DD-CRNR',i,theme),(xx,yy))
 return im
production=housing(216,488,corners=(0,1,3))
# The meter mounts over a continuous silver backing, sharing its outer x=0 edge.
# Native transparent cap pixels reveal backing metal, never terrain or a square cut.
save('production',production)
save('production-top-left-fitting',ra.piece('DD-CRNR',0,theme))
def hud_shape(name,frame):
 sh=ra.Shape(ra.HIRES.read(name+'.SHP'));ix=np.frombuffer(bytes(sh.frame(frame)),np.uint8).reshape(sh.height,sh.width)
 rgba=np.zeros(ix.shape+(4,),np.uint8);rgba[:,:,:3]=np.array(ra.PAL['blue'])[ix];rgba[:,:,3]=np.where(ix==0,0,255)
 return Image.fromarray(rgba)
top=hud_shape('POWERBAR',0);bottom=hud_shape('POWERBAR',1)
save('powerbar-top-source',top);save('powerbar-bottom-source',bottom);save('power-demand-marker',hud_shape('POWER',0))
# Preserve both complete native112px frames; insert only full9px scale periods.
# The first63 rows of frame1 are the clear cylinder, without a cap or corner.
glass=Image.new('RGBA',(20,476));glass.alpha_composite(top,(0,0))
for y in range(252):glass.paste(bottom.crop((0,y%63,20,y%63+1)),(0,112+y))
glass.alpha_composite(bottom,(0,364));save('glass-power-meter',glass)

# Open wing: only left/top/bottom profiles exist. Never create a hidden right rail.
wing=housing(85,165,right=False,corners=(0,2))
# User amendment: omit the16px DD-EDGE ribbing and shorten the wing by16px.
# Button seats stay fixed; plain14px rail +7px gap now meets them directly.
b=np.array(wing)
for j,f in enumerate((.95,.87,.76,.62,.47,.33,.22)):
 for y in list(range(16))+list(range(149,165)):b[y,56+j,:3]=(b[y,56+j,:3]*f).astype('uint8')
save('tools',Image.fromarray(b))
states=['normal','hover','pressed','active','disabled','activehover']
for name,size in [('tool',(35,35)),('tab',(28,30)),('header',(160,17)),('sidebar',(216,17)),('group',(19,19)),('scroll',(18,16))]:
 for st in states:save(name+'-'+st,ra.button(*size,st,theme))
font,adv=ra.font_assets(); save('font',font)
for key,color in [('warm',(230,222,206)),('gold',(231,167,0)),('muted',(146,148,137))]:
 b=np.array(font);mask=b[:,:,0]>0;b[mask,:3]=color;save('font-'+key,Image.fromarray(b))
advances=[int(x) for x in np.array(adv)[0,:,0]]
# Exact 19x19 TD semantic tool/category symbols reused independently of the RA housings.
td=root/'tools/art/remaster/viewer/cnc-interface'
for n in ['repair','sell','power']:save('icon-'+n,Image.open(td/'buttons'/('icon-'+n+'.png')).convert('RGBA'))
# Shared semantic category stencils are already separate transparent19x19 assets.
for n in ['structures','infantry','vehicles','aircraft']:save('symbol-'+n,Image.open(td/'tabs'/(n+'.png')).convert('RGBA'))
im=Image.new('RGBA',(19,19));d=ImageDraw.Draw(im);c=(214,211,199)
d.polygon([(1,12),(17,12),(13,17),(5,17)],fill=c);d.rectangle((6,8,12,11),fill=c);d.line((9,3,9,8),fill=c,width=2);d.line((9,4,14,4),fill=c);save('symbol-ships',im)
# Original rules and cameos; no TD/modern portrait substitution.
rules=configparser.ConfigParser(interpolation=None,strict=False); rules.read_string(ra.LOCAL.read('RULES.INI').decode('latin1'))
ids={'structures':['POWR','APWR','PROC','SILO','TENT','BARR','WEAP','DOME','FIX','HPAD','AFLD','SYRD','SPEN','ATEK','STEK','GAP','PBOX','HBOX','GUN','AGUN','FTUR','TSLA','SAM','IRON','PDOX','MSLO','SBAG','FENC','BRIK'],'infantry':['E1','E2','E3','E4','E6','DOG','MEDI','SPY','THF','E7'],'vehicles':['JEEP','1TNK','2TNK','3TNK','4TNK','V2RL','APC','ARTY','HARV','MCV','MNLY','MRJ','MGG','TRUK'],'aircraft':['HELI','HIND','TRAN','YAK','MIG'],'ships':['DD','CA','SS','LST','PT']}
labels={'POWR':'Power Plant','APWR':'Advanced Power','PROC':'Ore Refinery','SILO':'Ore Silo','TENT':'Allied Barracks','BARR':'Soviet Barracks','WEAP':'War Factory','DOME':'Radar Dome','FIX':'Service Depot','HPAD':'Helipad','AFLD':'Airfield','SYRD':'Naval Yard','SPEN':'Sub Pen','ATEK':'Allied Tech Center','STEK':'Soviet Tech Center','GAP':'Gap Generator','PBOX':'Pillbox','HBOX':'Camo Pillbox','GUN':'Gun Turret','AGUN':'AA Gun','FTUR':'Flame Tower','TSLA':'Tesla Coil','SAM':'SAM Site','IRON':'Iron Curtain','PDOX':'Chronosphere','MSLO':'Missile Silo','SBAG':'Sandbags','FENC':'Fence','BRIK':'Concrete Wall','E1':'Rifle Infantry','E2':'Grenadier','E3':'Rocket Soldier','E4':'Flamethrower','E6':'Engineer','DOG':'Attack Dog','MEDI':'Medic','SPY':'Spy','THF':'Thief','E7':'Tanya','JEEP':'Ranger','1TNK':'Light Tank','2TNK':'Medium Tank','3TNK':'Heavy Tank','4TNK':'Mammoth Tank','V2RL':'V2 Launcher','APC':'APC','ARTY':'Artillery','HARV':'Ore Truck','MCV':'MCV','MNLY':'Minelayer','MRJ':'Radar Jammer','MGG':'Mobile Gap','TRUK':'Supply Truck','HELI':'Longbow','HIND':'Hind','TRAN':'Transport','YAK':'Yak','MIG':'MiG','DD':'Destroyer','CA':'Cruiser','SS':'Submarine','LST':'Transport','PT':'Gunboat'}
items=[]
for cat,ls in ids.items():
 for id in ls:
  raw=ra.HIRES.read(id+'ICON.SHP')
  if not raw or id not in rules:continue
  rule=rules[id];owners=rule.get('Owner','').lower();level=int(rule.get('TechLevel','-1').split(';')[0]);
  if level<0:continue
  sh=ra.Shape(raw);ix=np.frombuffer(bytes(sh.frame(0)),np.uint8).reshape(sh.height,sh.width);rgba=np.zeros(ix.shape+(4,),np.uint8);rgba[:,:,:3]=np.array(ra.PAL['blue'])[ix];rgba[:,:,3]=255;save('cameo-'+id,Image.fromarray(rgba))
  items.append({'id':id,'name':labels.get(id,id),'category':cat,'cost':int(rule.get('Cost','0').split(';')[0]),'level':level,'owners':owners.split(','),'requires':[v.strip().upper() for v in rule.get('Prerequisite','').split(',') if v.strip()]})
for id in ['NATORADR','USSRRADR']:
 sh=ra.Shape(ra.HIRES.read(id+'.SHP'));ix=np.frombuffer(bytes(sh.frame(0)),np.uint8).reshape(sh.height,sh.width);rgba=np.zeros(ix.shape+(4,),np.uint8);rgba[:,:,:3]=np.array(ra.PAL['blue'])[ix];rgba[:,:,3]=255;save(id,Image.fromarray(rgba))
# A real saved Enhanced RA frame. Browser crops the old sidebar away; fixture only.
import shutil
bg=root/'tools/art/remaster/viewer/interface/spatial/reference/ra-enhanced-capture.png';shutil.copy2(bg,out/'battlefield.png');assets['battlefield']={'file':'battlefield.png','sha256':hashlib.sha256(bg.read_bytes()).hexdigest()}
sounds={}
for event,file in [('hover','hud_click.wav'),('click','hud_mouseover.wav')]:
 src=root/'playable/ambience'/file;shutil.copy2(src,out/file);sounds[event]={'file':file,'sha256':hashlib.sha256(src.read_bytes()).hexdigest()}
sourcefiles=['menu/bake_ra_enhanced_menu.py','art/interface/ra-enhanced-menu-v01/README.md','tools/art/remaster/viewer/cnc-interface/housing/manifest.json','game/hud640_layout.h','brain/vanilla/redalert/power.cpp']
manifest={'version':'ra3d-enhanced-hud-v06-plain-tool-wing','status':'browser review; not a runtime HUD','sourceRoot':'.','spriteSources':{name:hashlib.sha256(ra.HIRES.read(name+'.SHP')).hexdigest() for name in ['POWERBAR','POWER']},'nativeHeight':693,'fontAdvances':advances,'assets':assets,'sounds':sounds,'items':items,'sources':{f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sourcefiles},'contract':{'radar':[30,34,156,151],'production':[0,205,216,488],'tools':[-63,27,85,165],'toolSeats':[[-42,50,35,35],[-42,92,35,35],[-42,134,35,35]],'toolInner':[-49,43,49,133],'underlap':22,'toolRail':'plain14px; DD-EDGE ribbing omitted','categories':['structures','infantry','vehicles','aircraft','ships'],'tabWidth':28,'tabStride':31,'rows':6,'cameoSize':[80,60],'cameoGrid':{'x':30,'y':276,'strideX':84,'strideY':64,'rows':6,'columns':2,'bounds':[30,276,164,380],'safeClearance':8},'scrollSeats':[[152,660,18,16],[176,660,18,16]],'powerReadout':[30,660,114,16],'powerReadoutText':[87,664],'sampleTiming':'Lab fixture; not engine production','powerAction':'Lab fixture; RA engine capability unverified','powerMeter':{'bounds':[0,217,20,476],'fill':[10,239,4,406],'baseline':644,'maxFill':405,'sourceFrames':[112,112],'bodyExtension':252,'scalePitch':9,'markerOffset':[2,-4],'side':'left only','rightSide':'plain rail; no mirrored inner ribbing','mount':'continuous left rail backing, shared x=0 silhouette; top fitting underlaps original corner by12px','palettePairs':{'green':[ra.PAL['blue'][4],ra.PAL['blue'][3]],'yellow':[ra.PAL['blue'][211],ra.PAL['blue'][214]],'red':[ra.PAL['blue'][230],ra.PAL['blue'][235]]}}}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Saved',len(assets),'assets and',len(items),'source-rule entries to',out)
