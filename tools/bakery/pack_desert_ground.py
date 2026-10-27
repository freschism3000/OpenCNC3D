#!/usr/bin/env python3
"""Rebuild the Enhanced Desert world material and exact saved ground coverage.

Run from any checkout: python3 tools/bakery/pack_desert_ground.py [output_dir]
The twelve RGB layouts and all-black player masks have explicit pairings in the
versioned manifest. Coverage and shade come from the saved native outline exports;
no material classification from albedo is performed here or in the renderer.
"""
from pathlib import Path
import hashlib,json,struct,sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'art/remaster/textures/DESERT/2026-10-18/clear_v03'
FEATURES=ROOT/'art/remaster/textures/DESERT/2026-10-19/cliff_shore_repair_v07'
COASTS=ROOT/'art/remaster/textures/DESERT/2026-10-18/cliffs_shores_v04'
def pack(out):
    out.mkdir(parents=True,exist_ok=True)
    m=json.loads((SOURCE/'PACK_MANIFEST.json').read_text());cell=m['cell'];width=m['width'];payload=[]
    for row in m['layouts']:
        source=SOURCE/row['source']
        if hashlib.sha256(source.read_bytes()).hexdigest()!=row['source_sha256']:raise ValueError('Source version changed: '+row['source'])
        im=Image.open(SOURCE/row['image']).convert('RGB');mask=Image.open(SOURCE/row['player_colour_mask']).convert('L')
        if im.size!=(width,width) or mask.size!=im.size or mask.getextrema()!=(0,0):raise ValueError('Invalid material or player mask: '+row['image'])
        payload.append(im.tobytes())
    counts=m['counts'];data=b'C3DCLR\0\0'+struct.pack('<IIIIIIII',3,cell,width,sum(counts),0xD35E2026,*counts)+b''.join(payload)
    if hashlib.sha256(data).hexdigest()!=m['sha256']:raise ValueError('Material pack differs from reviewed source manifest')
    (out/'clearblend_DESERT.pack').write_bytes(data)
    keys=[];payload=[]
    features=json.loads((FEATURES/'FEATURE_MANIFEST.json').read_text())
    for row in features['assets']:
        d=FEATURES/row['export'];cov=Image.open(d/'ground_mask.png').convert('L');shade=Image.open(d/'ground_shade.png').convert('L');w,h=row['cells']
        if cov.size!=(w*cell,h*cell) or shade.size!=cov.size:raise ValueError('Ground coverage dimensions differ: '+row['name'])
        for icon in row['have']:
            y,x=divmod(icon,w);box=(x*cell,y*cell,(x+1)*cell,(y+1)*cell);c=cov.crop(box)
            if c.getextrema()[1]==0:continue
            keys.append(row['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',row['tid'],icon,row['variant'],0,0))
            payload.append(Image.merge('LA',(c,shade.crop(box))).tobytes())
    if len(keys)!=features['mask_cells'] or len(set(keys))!=len(keys):raise ValueError('Ground coverage keys differ from manifest')
    data=b'C3DGRAS\0'+struct.pack('<III',1,cell,len(keys))+b''.join(keys)+b''.join(payload)
    (out/'groundmask_DESERT.pack').write_bytes(data)
    from terrain_daylight import apply_ground
    apply_ground(out/'groundmask_DESERT.pack','DESERT')
    coast=json.loads((COASTS/'COAST_MANIFEST.json').read_text());coast_keys=[];coast_pixels=[]
    for row in coast['templates']:
        alpha=Image.open(COASTS/row['land_alpha']).convert('L');w,h=row['cells']
        if alpha.size!=(w*cell,h*cell):raise ValueError('Coast coverage dimensions differ: '+row['name'])
        for icon in row['have']:
            y,x=divmod(icon,w);coast_keys.append(row['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',row['tid'],icon,row['variant'],0,0))
            coast_pixels.append(alpha.crop((x*cell,y*cell,(x+1)*cell,(y+1)*cell)).tobytes())
    data=b'C3DCOAS\0'+struct.pack('<III',1,cell,len(coast_keys))+b''.join(coast_keys)+b''.join(coast_pixels)
    if len(coast_keys)!=coast['count'] or hashlib.sha256(data).hexdigest()!=coast['sha256']:raise ValueError('Coast coverage differs from manifest')
    (out/'coastmask_DESERT.pack').write_bytes(data)
    print('Desert:',sum(counts),'layouts;',len(keys),'ground coverage cells;',len(coast_keys),'coast cells')
if __name__=='__main__':pack(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'game')
