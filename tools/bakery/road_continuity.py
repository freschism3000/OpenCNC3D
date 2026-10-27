"""Compile saved native-outline road exits; no diffuse classification at runtime."""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image

def margin_coordinates(row,strip,shape,cell=144):
    """Use authored neighbouring pixels; mirror the current icon over empty canvas.

    An irregular template has no painting in its missing icons. Clamping to the
    bounding rectangle sampled opaque black there; clamping to one edge texel
    stretched a line across the whole join. Mirroring keeps source grain and the
    literal edge colour without reading a missing icon.
    """
    w,h=row['cells'];cy,cx=divmod(strip['icon'],w);mh,mw=shape
    yy,xx=np.indices(shape);xx+=cx*cell-(mw if strip['side']=='L' else 0);yy+=cy*cell-(mh if strip['side']=='T' else 0)
    inside=(xx>=0)&(yy>=0)&(xx<w*cell)&(yy<h*cell)
    present=np.isin((np.clip(yy,0,h*cell-1)//cell)*w+np.clip(xx,0,w*cell-1)//cell,row['have'])
    valid=inside&present
    fallback_x=np.clip(2*cx*cell-1-xx,cx*cell,(cx+1)*cell-1)if strip['side']=='L'else np.clip(xx,cx*cell,(cx+1)*cell-1)
    fallback_y=np.clip(2*cy*cell-1-yy,cy*cell,(cy+1)*cell-1)if strip['side']=='T'else np.clip(yy,cy*cell,(cy+1)*cell-1)
    return np.where(valid,xx,fallback_x),np.where(valid,yy,fallback_y)

def updates(root,bank,kind):
    if bank not in ('RASNOW','SNOW','WINTER','DESERT'):
        if kind=='terrainenh':return {}
        # Other theaters retain their own paintings and approved strip alpha.
        # Only pixels sampled beyond an irregular road's occupied canvas need
        # the valid mirrored icon, never its black missing neighbour.
        original=root/'art/remaster/textures'/bank
        if not (original/'set_144.json').is_file():return {}
        meta=json.loads((original/'set_144.json').read_text())
        result={}
        for name,row in meta['templates'].items():
            if not (name.startswith('D') and name[1:].isdigit()):continue
            face=np.asarray(Image.open(original/name/'final_144.png').convert('RGB'))
            for strip in row.get('margins',[]):
                rgba=np.array(Image.open(original/name/strip['file']).convert('RGBA'))
                mh,mw=rgba.shape[:2];w,h=row['cells'];cy,cx=divmod(strip['icon'],w)
                yy,xx=np.indices((mh,mw));xx+=cx*144-(mw if strip['side']=='L'else 0);yy+=cy*144-(mh if strip['side']=='T'else 0)
                valid=(xx>=0)&(yy>=0)&(xx<w*144)&(yy<h*144)
                present=np.isin((np.clip(yy,0,h*144-1)//144)*w+np.clip(xx,0,w*144-1)//144,row['have'])
                repair=~(valid&present);sx,sy=margin_coordinates(row,strip,(mh,mw))
                rgba[...,:3][repair]=face[sy,sx][repair]
                result[row['tid'],strip['icon'],0 if strip['side']=='L'else 1]=rgba.tobytes()
        return result
    source=root/('art/remaster/textures/unified_daylight_v01/desert_road_exits_v02'if bank=='DESERT'else'art/remaster/textures/ra_road_continuity_v02')
    if bank in ('SNOW','WINTER'):source=source/bank
    manifest=json.loads((source/'MANIFEST.json').read_text())
    original=root/'art/remaster/textures'/bank
    meta=json.loads((original/'set_144.json').read_text())['templates']
    result={}
    for r in manifest['assets']:
        p=root/r['source']
        assert hashlib.sha256(p.read_bytes()).hexdigest()==r['source_sha256'],p
        for f,digest in r['files'].items():
            assert hashlib.sha256((source/r['name']/f).read_bytes()).hexdigest()==digest,f
        im=Image.open(source/r['name']/'albedo.png').convert('RGB');w,h=r['cells'];a=np.asarray(im)
        assert Image.open(source/r['name']/'player_colour_mask.png').getextrema()==(0,0)
        if kind=='terrainenh':
            for icon in r['have']:
                y,x=divmod(icon,w);result[r['tid'],icon]=im.crop((x*144,y*144,(x+1)*144,(y+1)*144)).tobytes()
        else:
            mask=np.asarray(Image.open(source/r['name']/('road_mask.png' if bank=='DESERT' else 'strip_mask.png')),float)/255
            for s in meta[r['name']].get('margins',[]):
                old=Image.open(original/r['name']/s['file']);mw,mh=old.size
                cy,cx=divmod(s['icon'],w);yy,xx=np.indices((mh,mw))
                ox,oy=(cx*144-mw,cy*144) if s['side']=='L' else (cx*144,cy*144-mh)
                xx,yy=margin_coordinates(r,s,(mh,mw))
                # The extended edge shares the face's RGB. Only its exact saved
                # native road coverage fades; old snow and lip pixels cannot return.
                rgba=np.asarray(old.convert('RGBA')).copy();rgba[...,:3]=a[yy,xx]
                if s['side']=='L':
                    coverage=mask[cy*144:(cy+1)*144,cx*144][:,None]
                    fade=(np.arange(mw)+1)/mw
                else:
                    coverage=mask[cy*144,cx*144:(cx+1)*144][None,:]
                    fade=((np.arange(mh)+1)/mh)[:,None]
                if not coverage.any():
                    result[r['tid'],s['icon'],0 if s['side']=='L' else 1]=rgba.tobytes();continue
                rgba[...,3]=np.uint8(np.rint(255*coverage*fade*fade*(3-2*fade)))
                result[r['tid'],s['icon'],0 if s['side']=='L' else 1]=rgba.tobytes()
    return result


def ground_updates(root,bank):
    if bank!='RASNOW':return {},{}
    folder=root/'art/remaster/textures/ra_road_continuity_v02';manifest=json.loads((folder/'MANIFEST.json').read_text());updates={};names={}
    for r in manifest['assets']:
        p=folder/r['name']/'ground_mask.png';assert hashlib.sha256(p.read_bytes()).hexdigest()==r['files']['ground_mask.png']
        im=Image.open(p).convert('L');w,h=r['cells']
        for icon in r['have']:
            y,x=divmod(icon,w);key=(r['tid'],icon,0,1);updates[key]=im.crop((x*144,y*144,(x+1)*144,(y+1)*144)).tobytes();names[key]=r['name']
    return updates,names
