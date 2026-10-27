#!/usr/bin/env python3
"""Pack the exact approved RA HUD v06 sources; no art regeneration or recoloring."""
from pathlib import Path
import argparse,hashlib,json,struct
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('out',type=Path);p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--header',type=Path);a=p.parse_args()
kit=a.source/'art/interface/ra-enhanced-hud-v06-plain-tool-wing/kit';m=json.loads((kit/'manifest.json').read_text());assets=[]
def read(n):
 meta=m['assets'][n];f=kit/meta['file'];assert hashlib.sha256(f.read_bytes()).hexdigest()==meta['sha256'],n
 return Image.open(f).convert('RGBA')
for n,key in [('minimap','map'),('production','body'),('tools','wing'),('card','card'),('glass-power-meter','glass'),('power-demand-marker','demand'),('production-top-left-fitting','corner'),('font-warm','font'),('font-gold','gold'),('font-muted','muted'),('NATORADR','allies'),('USSRRADR','soviet')]:assets.append((key,read(n),1))
for n in ['tool','tab','header','sidebar','group','scroll']:
 ims=[read(n+'-'+s) for s in ['normal','hover','pressed','active','disabled','activehover']];im=Image.new('RGBA',(ims[0].width*6,ims[0].height))
 for k,part in enumerate(ims):im.paste(part,(k*part.width,0))
 assets.append((n,im,6))
for n in ['repair','sell','power']:assets.append((n,read('icon-'+n),1))
for n in m['contract']['categories']:assets.append((n,read('symbol-'+n),1))
# Compact windows preserve source caps/rails and repeat only whole glass tick periods.
# Standard six-row assets remain byte-identical to the approved source.
body=read('production');glass=read('glass-power-meter')
for rows in range(2,6):
 height=max(450,309+rows*64);bh=height-205
 b=body.crop((0,0,216,bh));b.paste(body.crop((0,472,216,488)),(0,bh-16))
 # Move the original lower-right fitting, retaining its shaped transparency.
 corner=body.crop((193,464,216,488));mask=read('production-top-left-fitting').getchannel('A').transpose(Image.Transpose.ROTATE_180);corner.putalpha(mask);b.alpha_composite(corner,(193,bh-24))
 assets.append(('body'+str(rows),b,1))
 extension=max(0,((height-217-224)//9)*9);g=Image.new('RGBA',(20,224+extension));g.paste(glass.crop((0,0,20,112)),(0,0))
 for y in range(extension):g.paste(glass.crop((0,112+y%63,20,113+y%63)),(0,112+y))
 g.paste(glass.crop((0,364,20,476)),(0,112+extension));assets.append(('glass'+str(rows),g,1))
ims=[]
for state in ['normal','hover','pressed','active','disabled','activehover']:
 im=read('header-'+state);small=Image.new('RGBA',(132,17));small.paste(im.crop((0,0,66,17)),(0,0));small.paste(im.crop((94,0,160,17)),(66,0));ims.append(small)
small=Image.new('RGBA',(132*6,17))
for k,im in enumerate(ims):small.paste(im,(132*k,0))
assets.append(('header132',small,6))
blob=bytearray(b'CNCHUD01'+struct.pack('<II',1,len(assets)))
for key,im,frames in assets:
 assert len(key)<16;blob+=key.encode().ljust(16,b'\0')+struct.pack('<III',im.width//frames,im.height,frames)+im.tobytes()
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_bytes(blob)
if a.header:
 c=m['contract'];lines=['/* Generated from approved RA HUD v06. Re-run tools/ra3d/bake-enhanced-hud.py. */','#ifndef RA_HUD_LAYOUT_H','#define RA_HUD_LAYOUT_H','#define RAH_W 216','#define RAH_H 693','#define RAH_ROWS 6','#define RAH_CW 80','#define RAH_CH 60','#define RAH_GRID_X 30','#define RAH_GRID_Y 276','#define RAH_DX 84','#define RAH_DY 64','#define RAH_RADAR_X 30','#define RAH_RADAR_Y 34','#define RAH_RADAR_W 156','#define RAH_RADAR_H 151','#define RAH_WING_X (-63)','#define RAH_WING_Y 27','#define RAH_WING_W 85','#define RAH_WING_H 165','static const unsigned char rah_adv[]={'+','.join(map(str,m['fontAdvances']))+'};','static const EH_Catalog rah_catalog[]={']
 for it in m['items']:
  for f,owner in enumerate(['allies','soviet']):
   if owner in it['owners']:lines.append('{'+json.dumps(it['id'])+f",{f},{c['categories'].index(it['category'])},{it['cost']},{it['level']},"+json.dumps(','.join(it['requires']))+'},')
 lines+=['};','struct RAH_Title {const char *id,*title;};','static const RAH_Title rah_titles[]={']
 lines+=['{'+json.dumps(it['id'])+','+json.dumps(it['name'])+'},' for it in m['items']];lines+=['};','struct RAH_AssetSpec {const char *name;int w,h,frames;};','static const RAH_AssetSpec rah_asset_specs[]={']
 lines+=['{'+json.dumps(name)+f',{im.width//frames},{im.height},{frames}'+'},' for name,im,frames in assets];lines+=['};','#endif']
 data='\n'.join(lines)+'\n'
 if not a.header.exists() or a.header.read_text()!=data:a.header.write_text(data)
a.out.with_suffix('.json').write_text(json.dumps({'approved':m['version'],'sourceManifest':hashlib.sha256((kit/'manifest.json').read_bytes()).hexdigest(),'pack':hashlib.sha256(blob).hexdigest(),'assets':len(assets)},indent=2)+'\n')
print('RAHUD|packed|approved-v06|assets='+str(len(assets)))
