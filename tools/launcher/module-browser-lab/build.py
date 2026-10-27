#!/usr/bin/env python3
"""Extract exact accepted UI pixels; this Lab variation never downloads a module."""
from pathlib import Path
import hashlib,json,re,shutil,struct
from PIL import Image
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'art/interface/launcher-module-browser-v01'
KIT=OUT/'kit';KIT.mkdir(parents=True,exist_ok=True)
keep={'visuals-frame','confirm-frame','settings-frame','font_menu'}|{'button-500-40-'+s for s in ['normal','hover','pressed','active','activehover','disabled']}
source=ROOT/'playable/enhanced-menu.pack';assets={}
with source.open('rb') as f:
 assert f.read(8)==b'ENMENU06';count=struct.unpack('<I',f.read(4))[0]
 for _ in range(count):
  name=f.read(64).split(b'\0')[0].decode();w,h,n=struct.unpack('<III',f.read(12));data=f.read(n)
  if name in keep:
   assert n==w*h*4;dst=KIT/(name+'.png');Image.frombytes('RGBA',(w,h),data).save(dst)
   assets[name]={'file':dst.name,'width':w,'height':h,'sha256':hashlib.sha256(dst.read_bytes()).hexdigest()}
assert set(assets)==keep
approved=ROOT/'art/interface/launcher-enhanced-v05'
for name,src in [('td','module-display-160x160.png'),('ra','ra-display-160x160.png'),('wordmark','wordmark-display-220x94.png')]:
 dst=KIT/(name+'.png');shutil.copy2(approved/src,dst);im=Image.open(dst)
 assets[name]={'file':dst.name,'width':im.width,'height':im.height,'sha256':hashlib.sha256(dst.read_bytes()).hexdigest(),'source':'art/interface/launcher-enhanced-v05/'+src}
sounds={}
for name in ['hud_click.wav','hud_mouseover.wav']:
 shutil.copy2(ROOT/'playable/ambience'/name,KIT/name)
 sounds[name]={'source':'playable/ambience/'+name,'sha256':hashlib.sha256((KIT/name).read_bytes()).hexdigest()}
adv=[int(v) for v in re.search(r'font_advance\[\]=\{([^}]+)',(ROOT/'launcher/lskin_font.h').read_text())[1].split(',')]
(KIT/'manifest.json').write_text(json.dumps({'version':'launcher-module-browser-v01','status':'interactive Lab design; not runtime delivery','size':[1016,636],'assets':assets,'sounds':sounds,'fontAdvances':adv,'sourcePackSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'geometry':{'frame':[0,0,1016,636],'catalogRows':[[58,184,900,166],[58,362,900,166]],'details':[170,148,676,340],'media':[170,26,676,584],'buttons':{'height':40,'cornerGuard':12},'logos':'approved 160x160 sources, unscaled'}},indent=2)+'\n')
for name in ['index.html','browser.mjs']:shutil.copy2(Path(__file__).parent/name,OUT/name)
revision=hashlib.sha256((OUT/'browser.mjs').read_bytes()).hexdigest()[:16]
page=OUT/'index.html';page.write_text(page.read_text().replace('src="browser.mjs"',f'src="browser.mjs?v={revision}"'))
link=ROOT/'tools/art/remaster/viewer/cnc-interface/module-browser-v01'
if not link.exists():link.symlink_to('../../../../../art/interface/launcher-module-browser-v01',target_is_directory=True)
print(OUT)
