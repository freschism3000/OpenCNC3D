#!/usr/bin/env python3
"""Pack accepted RA snow and explicit native Outline Check coverage; no art repaint."""
from pathlib import Path
import hashlib,json,struct,sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'art/remaster/textures/SNOW/2026-10-19/ground_continuity_v02'
def pack(out):
    out.mkdir(parents=True,exist_ok=True)
    m=json.loads((SOURCE/'MANIFEST.json').read_text());C=144
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    path=ROOT/m['source_clear']
    if sha(path)!=m['source_clear_sha256']:raise ValueError('Snow source version changed')
    clear=Image.open(path).convert('RGB')
    if clear.size!=(576,576):raise ValueError('Snow material dimensions changed')
    materials=b'C3DCLR\0\0'+struct.pack('<IIIII',1,C,576,3,0x5A020026)+clear.tobytes()*3
    keys=[];pixels=[]
    for row in m['templates']:
        source=ROOT/row['source'];d=SOURCE/row['export']
        if sha(source)!=row['source_sha256']:raise ValueError('Cliff source version changed: '+row['name'])
        for f,k in [('ground_mask.png','ground_mask_sha256'),('ground_shade.png','ground_shade_sha256'),('player_colour_mask.png','player_colour_mask_sha256')]:
            if sha(d/f)!=row[k]:raise ValueError('Saved snow mask changed: '+str(d/f))
        coverage=Image.open(d/'ground_mask.png').convert('L');shade=Image.open(d/'ground_shade.png').convert('L');player=Image.open(d/'player_colour_mask.png').convert('L');w,h=row['cells']
        if coverage.size!=(w*C,h*C) or shade.size!=coverage.size or player.size!=coverage.size or player.getextrema()!=(0,0):raise ValueError('Snow mask dimensions or player-colour contract differs')
        for icon in row['have']:
            y,x=divmod(icon,w);box=(x*C,y*C,(x+1)*C,(y+1)*C);c=coverage.crop(box)
            if not c.getextrema()[1]:continue
            keys.append(row['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',row['tid'],icon,0,1,0));pixels.append(Image.merge('LA',(c,shade.crop(box))).tobytes())
    if len(keys)!=m['mask_cells'] or len(set(keys))!=len(keys):raise ValueError('Snow mask keys differ')
    masks=b'C3DGRAS\0'+struct.pack('<III',1,C,len(keys))+b''.join(keys)+b''.join(pixels)
    for name,data in [('clearblend_RA_SNOW.pack',materials),('groundmask_RA_SNOW.pack',masks)]:
        if hashlib.sha256(data).hexdigest()!=m['packs'][name]:raise ValueError('Snow pack does not reproduce: '+name)
        (out/name).write_bytes(data)
    from terrain_daylight import apply_ground
    apply_ground(out/'groundmask_RA_SNOW.pack','RASNOW')
    print('Shared RA snow:',len(m['templates']),'templates;',len(keys),'ground coverage cells')
if __name__=='__main__':pack(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'game')
