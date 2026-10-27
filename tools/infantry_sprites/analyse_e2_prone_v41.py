from pathlib import Path
import json,hashlib
from PIL import Image,ImageDraw,ImageFont
r=Path(__file__).resolve().parents[2];out=r/'art/sprites/infantry-snes-v01/motion-study-v41-e2-prone-reference';out.mkdir(exist_ok=False);source=r/'art/sprites/infantry-snes-v01/source-study-v02/E2';im=Image.open(source/'original.png').convert('RGBA');views=['N','NW','W','SW','S','SE','E','NE'];font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',15);manifest={}
for vi,v in enumerate(views):
 board=Image.new('RGB',(1440,500),(25,28,33));d=ImageDraw.Draw(board);actions={}
 for row,(name,start,count,jump,fps)in enumerate([('fire_prone',288,8,12,15),('crawl',240,4,4,7.5),('lie_down',224,2,2,7.5),('get_up',272,2,2,5)]):
  hashes={};cells=[]
  for k in range(count):
   n=start+vi*jump+k;crop=im.crop((n%20*50,n//20*39,n%20*50+50,n//20*39+39));tile=crop.crop((12,9,38,34)).resize((130,125),Image.Resampling.NEAREST);x=k*180+20 if row==0 else (sum([4,2,2][:max(0,row-1)])+k)*180+20;y=50 if row==0 else 300
   board.paste(tile,(x,y),tile);d.text((x,y-24),f'{name} {k} / DOS{n}',font=font,fill='#e7bd70');h=hashlib.sha256(crop.tobytes()).hexdigest();hashes.setdefault(h,[]).append(k);cells.append({'stage':k,'source_index':n,'rgba_sha256':h})
  actions[name]={'count':count,'fps':fps,'source_start':start+vi*jump,'source_jump':jump,'unique':len(hashes),'exact_holds':[ks for ks in hashes.values()if len(ks)>1],'cells':cells}
 d.text((20,12),v+' / Authentic whole prone throw plus crawl and entry/exit / no native remake yet',font=font,fill='#dce3e7');board.save(out/f'{v}-source-action-board.png');manifest[v]=actions
(out/'source-action-brief.json').write_text(json.dumps({'date':'2026-10-21','before_new_art':True,'scope':'Source-only prone action analysis before any native prone art. No acceptance or runtime delivery implied.','source_original_sha256':hashlib.sha256((source/'original.png').read_bytes()).hexdigest(),'source_table':'brain/vanilla/tiberiandawn/idata.cpp GrenadierDos and E2type','engine_completion':'brain/vanilla/tiberiandawn/infantry.cpp: default completion returns stationary prone soldier to DO_PRONE, not upright ready; launch uses Class->ProneLaunch when IsProne.','prone_throw':{'count':8,'stored_per_heading':12,'unused_local_stages':[8,9,10,11],'fps':15,'release_stage':6,'next_stationary_state':'DO_PRONE held first throw cell'},'other_rates':{'lie_down':7.5,'crawl':7.5,'get_up':5},'source_camera_records':manifest,'review_status':'Boards generated for whole-action visual analysis; camera observations follow before native authoring.'},indent=2)+'\n');print(out)
