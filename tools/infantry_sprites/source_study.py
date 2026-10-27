#!/usr/bin/env python3
"""Authentic TD infantry source inventory/evidence. Never authors replacement pixels."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from PIL import Image,ImageDraw,ImageFont

NAMES=['N','NW','W','SW','S','SE','E','NE']
LABELS={'E1':'Minigunner','E2':'Grenadier','E3':'Rocket Soldier','E4':'Flamethrower','E5':'Chemical Warrior','E6':'Engineer','RMBO':'Commando','MOEBIUS':'Dr. Moebius','DELPHI':'Delphi','CHAN':'Dr. Chan'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();root=a.source_root;out=a.out;out.mkdir(parents=True,exist_ok=True)
 sys.path.insert(0,str(root/'menu/tools'));from mixshp import MixFile,Shape
 src=(root/'brain/vanilla/tiberiandawn/idata.cpp').read_text(encoding='latin1');control=(root/'brain/vanilla/tiberiandawn/infantry.cpp').read_text(encoding='latin1')
 rates={}
 for m in re.finditer(r'\{(?:true|false),\s*(?:true|false),\s*(?:true|false),\s*(\d+)\},?\s*// DO_(\w+)',control):rates[m[2].lower()]=int(m[1])
 mix=MixFile(str(root/'data/dosdata/CONQUER.MIX'));pal=bytes(min(252,v<<2)for v in MixFile(str(root/'data/dosdata/LOCAL.MIX')).read('TEMPERAT.PAL'))
 tables={m[1]:m[2]for m in re.finditer(r'int (\w+)\[DO_COUNT\]\[3\] = \{(.*?)\n\};',src,re.S)}
 inventory=[];font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',13)
 for m in re.finditer(r'static InfantryTypeClass const (\w+)\((.*?)\);',src,re.S):
  block=m[2];uid=re.search(r'"(\w+)"',block)[1]
  if uid=='E1TR':continue
  table=re.search(r'&(\w+)\[0\]\[0\]',block)[1];shp=Shape(mix.read(uid+'.SHP'));folder=out/uid;folder.mkdir(exist_ok=True)
  frames=[];atlas=Image.new('RGBA',(1000,39*((shp.frames+19)//20)))
  for n in range(shp.frames):
   raw=bytes(shp.frame(n));im=Image.frombytes('P',(50,39),raw);im.putpalette(pal);im=im.convert('RGBA');im.putalpha(Image.frombytes('L',(50,39),bytes(0 if x in (0,4)else 255 for x in raw)));frames.append(im);atlas.alpha_composite(im,((n%20)*50,(n//20)*39))
  atlas.save(folder/'original.png');actions=[];contexts={n:[]for n in range(shp.frames)};boards={}
  for enum,line in enumerate([l for l in tables[table].splitlines()if re.match(r'\s*\d',l)]):
   mat=re.match(r'\s*(\d+),\s*(\d+),\s*(\d+),?\s*//\s*DO_(\w+)(.*)',line);assert mat,line
   start,count,jump=map(int,mat.group(1,2,3));key=mat[4].lower();rate=rates.get(key,2);valid='N/A'not in mat[5] and count>0
   row=dict(enum=enum,id=key,start=start,count=count,jump=jump,applicable=valid,rate=rate,fps=15/rate if rate else 0)
   if valid:
    samples=[dict(index=start+f*jump+k,facing=NAMES[f]if jump else'fixed',stage=k)for f in range(8 if jump else 1)for k in range(count)]
    # Original Draw_It adds47 to any applicable IsBoxing action, including civilians.
    if 11<=enum<=21:
     row['boxing_camera_offset']=47;samples += [dict(index=start+47+k,facing='opposite boxing',stage=k)for k in range(count)]
    row['invalid_source_indices']=sorted({s['index']for s in samples if s['index']>=shp.frames})
    for s in samples:
     if s['index']<shp.frames:contexts[s['index']].append(dict(action=key,facing=s['facing'],stage=s['stage']))
    paths=[]
    for offset in range(0,len(samples),24):
     batch=samples[offset:offset+24];board=Image.new('RGB',(1296,45+184*((len(batch)+5)//6)),(25,28,33));d=ImageDraw.Draw(board);d.text((8,7),f'{uid} {key} · complete ordered original · {row["fps"]:g}fps · source table {table}',font=font,fill='#e7bd70')
     for i,s in enumerate(batch):
      x=i%6*216;y=45+i//6*184;d.text((x+4,y),f'{s["facing"]} {s["stage"]} / DOS{s["index"]}',font=font,fill='#e7bd70')
      if s['index']<shp.frames:
       tile=frames[s['index']].resize((200,156),Image.Resampling.NEAREST);board.paste(tile,(x+4,y+22),tile)
      else:d.text((x+4,y+60),'SOURCE INDEX OUT OF RANGE',font=font,fill='red')
     name=f'{key}-{offset//24:02}.png';board.save(folder/name);paths.append(name)
    boards[key]=paths
   actions.append(row)
  for n,ctx in contexts.items():
   if not ctx:contexts[n]=[dict(action='physical padding / unselected art',facing='physical slot',stage=n)]
  ref={}
  for name in ('original_1995_manual.png','original_1995_cameo.png','original_1995_render.png','original_1995_cinematic.png','original_1995_ingame.png'):
   p=root/f'art/remaster/{uid}/refs/{name}'
   if p.exists():ref[str(p.relative_to(root))]=sha(p)
  unit=dict(id=uid,name=LABELS.get(uid,'Civilian '+uid[1:]),frames=shp.frames,width=50,height=39,anchor=[27,20],table=table,actions=actions,source_boards=boards,original_sha256=sha(folder/'original.png'),references=ref,contexts=contexts)
  (folder/'source.json').write_text(json.dumps(unit,indent=2)+'\n');inventory.append({k:v for k,v in unit.items()if k not in ('contexts','source_boards')})
 inventory_data=dict(date='2026-10-20',scope='All20Tiberian Dawn infantry; E1 retained,19remaining sheets',units=inventory,remaining_physical_frames=sum(u['frames']for u in inventory if u['id']!='E1'),source_hashes={p:sha(root/p)for p in ['brain/vanilla/tiberiandawn/idata.cpp','brain/vanilla/tiberiandawn/infantry.cpp','data/dosdata/CONQUER.MIX','data/dosdata/LOCAL.MIX']},references=[dict(url='https://cnc-comm.com/files/CC-Manual.pdf',role='Original Westwood manual, character/weapon function and design photos; pages35–36,52–53'),dict(url='https://cncnz.com/games/command-conquer/10th-anniversary-gallery/manual-scans-infantry-vehicles-aircraft-buildings/',role='Original manual scans, existing project reference collection')],status='Original evidence only; semantic observations must be written before authoring')
 (out/'inventory.json').write_text(json.dumps(inventory_data,indent=2)+'\n');print(len(inventory),'units;',inventory_data['remaining_physical_frames'],'remaining physical frames; source-only boards exported')
if __name__=='__main__':main()
