#!/usr/bin/env python3
"""Compile saved, source-led daylight deltas; never classify runtime diffuse colour.

Original Enhanced paintings stay immutable. Exact template/variant keys overlay
only reviewed surface assets. Shared ground loses its obsolete painted shade
channel; true sun visibility is supplied by the renderer. Idempotent pack edits.
"""
from pathlib import Path
from functools import lru_cache
import hashlib,json,struct,sys
from PIL import Image
import numpy as np
import re
from scipy import ndimage as nd
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'art/remaster/textures/unified_daylight_v01'
DESERT_MATERIAL=ROOT/'art/remaster/ROCKS/cliff_sandstone_v05/material.png'
DESERT_MATERIAL_SHA='80e935c7e85ce5bc9442a359a04d8912df25f59e3517f9a5fef885283faecd8a'
DESERT_CACHE={}
C=144
WEIGHTS=np.array([.2126,.7152,.0722],np.float32)
@lru_cache(None)
def manifest():
    return json.loads((SOURCE/'MANIFEST.json').read_text())
def rows(bank):
    return [r for r in manifest()['assets'] if r['theater']==bank]
def surface_source(r):
    """RA's matching S templates use the complete TD material, not only its tint.

    IDs differ between games; template/variant, occupied cells and dimensions are
    the explicit shared contract. Snow, shores and RA-only forms retain their art.
    """
    if r['theater'] not in ('RATEMP','DESERT') or not re.fullmatch(r'S\d+',r['name']):return r
    source=next((s for s in rows('TEMPERAT')
                 if (s['name'],s['variant'])==(r['name'],r['variant'])),None)
    # Desert has six extra cosmetic variants of three identical native layouts.
    if source is None and r['theater']=='DESERT':
        source=next((s for s in rows('TEMPERAT') if s['name']==r['name'] and s['variant']==0),None)
    if source is None or source['cells']!=r['cells'] or source['have']!=r['have']:
        raise ValueError('Shared cliff layout differs: '+r['export'])
    return source
def checked(r,filename,mode):
    p=SOURCE/r['export']/filename
    if filename=='ground_mask.png' and 'ground_contact_file'in r:
        p=SOURCE/r['ground_contact_file']
        if hashlib.sha256(p.read_bytes()).hexdigest()!=r['ground_contact_sha256']:raise ValueError('Ground contact source changed: '+str(p))
        im=Image.open(p)
        if im.mode!='L' or im.size!=(r['cells'][0]*C,r['cells'][1]*C):raise ValueError('Invalid grayscale ground contact dimensions: '+str(p))
        return im.convert(mode)
    key={'albedo.png':'albedo_sha256','height.png':'height_sha256','ground_mask.png':'ground_mask_sha256','player_colour_mask.png':'player_colour_mask_sha256','rock_mask.png':'rock_mask_sha256'}.get(filename)
    if key and hashlib.sha256(p.read_bytes()).hexdigest()!=r[key]:raise ValueError('Daylight source changed: '+str(p))
    im=Image.open(p).convert(mode);w,h=r['cells']
    if filename in ['albedo.png','height.png','ground_mask.png','player_colour_mask.png','rock_mask.png'] and im.size!=(w*C,h*C):raise ValueError('Daylight dimensions changed: '+str(p))
    return im

def field(icon,v,w,h):return icon|(v<<8)|((((w-1)<<10)|((h-1)<<13)) if v else 0)
def temperate_stone(im,mask):
    a=np.array(im).astype(np.float32);rgb=a[...,:3]/255.0
    y=rgb@np.array([.2126,.7152,.0722]);grey=np.repeat(y[...,None],3,axis=2)
    # Retain fine mineral contrast, reduce warm gold and pale sun-facing plate colour.
    # Preserve mineral luminance and grit while removing the warm brown streaks
    # that showed through this shared OpenCNC3D material in both games.
    graded=(grey*.96+rgb*.04)*.78
    m=np.array(mask).astype(np.float32)[...,None]/255.0
    a[...,:3]=np.rint(np.clip(rgb*(1-m)+graded*m,0,1)*255)
    return Image.fromarray(a.astype(np.uint8),im.mode)

@lru_cache(None)
def desert_material():
    if hashlib.sha256(DESERT_MATERIAL.read_bytes()).hexdigest()!=DESERT_MATERIAL_SHA:
        raise ValueError('Reviewed Desert sandstone material changed: '+str(DESERT_MATERIAL))
    stone=np.asarray(Image.open(DESERT_MATERIAL).convert('RGB'),dtype=np.float32)/255.0
    return stone,float((stone@WEIGHTS).mean())

def shared_stone(im,mask):
    """Keep Temperate's mineral forms; Desert is a warm, mid-value sandstone.

    Extend the face under the outside-only ground feather. Mixing dark grass into
    that feather before blending sand a second time produced an ink-dark skirt.
    """
    rgb=np.asarray(im,dtype=np.float32)[...,:3]/255
    rock=np.asarray(mask)>127
    nearest=nd.distance_transform_edt(~rock,return_distances=False,return_indices=True)
    y=(rgb@WEIGHTS)[nearest[0],nearest[1]]
    # Match the mineral surface, not the bright orange ground. No sun direction,
    # height or face orientation enters this albedo treatment.
    value=np.clip(y*1.32+.035,0,1)
    tint=np.array([1.16,1.015,.80],np.float32)
    graded=np.uint8(np.rint(np.clip(value[...,None]*tint,0,1)*255))
    if im.mode=='RGBA':graded=np.dstack((graded,np.asarray(im)[...,3]))
    return Image.fromarray(graded,im.mode)

def desert_sample(x,y,seed):
    stone,mean_y=desert_material();h,w=stone.shape[:2]
    # One source texel per input texel: callers supply world cells * 144.
    # The old 98px crop stretched over 310px reduced the visible stone density.
    u=np.mod(x+seed*137.0,w);v=np.mod(y+seed*311.0,h)
    x0=u.astype(np.int32);y0=v.astype(np.int32);x1=(x0+1)%w;y1=(y0+1)%h;fx=(u-x0)[...,None];fy=(v-y0)[...,None]
    tex=(stone[y0,x0]*(1-fx)+stone[y0,x1]*fx)*(1-fy)+(stone[y1,x0]*(1-fx)+stone[y1,x1]*fx)*fy
    # Keep the reviewed sandstone's grain, with a quieter ochre chroma. The
    # user's Desert RGB/tint controls still apply later to the whole biome.
    tex=tex*.65+(tex@WEIGHTS)[...,None]*.35
    return tex,mean_y

def desert_stone(r):
    """Apply the reviewed sandstone only through the source's explicit rock mask.

    The complete source cliff footprint is stone. Its saved ground matte feathers
    only outside that footprint; material application cannot shrink it by hue,
    brightness or a second dilation/blur. Heights and ground coverage share that
    original outline. The explicit black player-colour mask stays unchanged.
    """
    key=r['export']
    if key in DESERT_CACHE:return DESERT_CACHE[key]
    if r.get('material_recipe'):
        result=(checked(r,'albedo.png','RGB'),np.asarray(checked(r,'ground_mask.png','L')))
        DESERT_CACHE[key]=result;return result
    _,mean_y=desert_material()
    old=np.asarray(checked(r,'albedo.png','RGB'),dtype=np.float32)/255.0
    rock=np.asarray(checked(r,'rock_mask.png','L'),dtype=np.float32)/255.0
    ground=np.asarray(checked(r,'ground_mask.png','L'),dtype=np.float32)/255.0
    coverage=rock
    material_input=old
    if r['name'].startswith('S') and not r['name'].startswith('SH'):
        coverage=1.0-ground
        if np.any(coverage[rock>.5]<.999):
            raise ValueError('Ground matte cuts into original Desert cliff: '+key)
        # The ground matte is the sole coverage operation. Extend the stone's
        # neutral albedo underneath its skirt; mixing the old painted sand here
        # and then blending ground again produced a dark brown fringe.
        nearest=nd.distance_transform_edt(rock<.5,return_distances=False,return_indices=True)
        material_input=old[nearest[0],nearest[1]]
    y,x=np.indices(old.shape[:2],dtype=np.float32)
    tex,mean_y=desert_sample(x,y,float(r['tid']));tex_y=tex@WEIGHTS
    painted=(material_input@WEIGHTS)[...,None]*.92*(tex/np.maximum(tex_y[...,None],.02))*(.85+.15*tex_y[...,None]/mean_y)
    result=np.clip(painted if re.fullmatch(r'S\d+',r['name']) else
                   old*(1-coverage[...,None])+painted*coverage[...,None],0,1)
    result=(Image.fromarray(np.rint(result*255).astype(np.uint8),'RGB'),
            np.asarray(np.clip(ground,0,1)*255,dtype=np.uint8))
    DESERT_CACHE[key]=result
    return result

# SHORE TEMPLATES KEEP THEIR APPROVED PAINTING AND JOIN STRIPS in every bank whose shores
# are plain water and wet ground. The daylight pass divides each template's low-frequency
# light out of everything that is not snow-covered ground, so on a shore it also brightened
# the dark wet shallows, by up to 1.6 times, inside templates whose rock footprint covered
# them and not in a beach icon whose footprint was empty: a beach cell then drew as a dark
# square with four straight edges, and two shore templates meeting along a cliff stepped in
# brightness down the whole shared cell edge. The approved paintings join across every
# shore edge. Desert keeps its own shore material (desert_stone).
SHORE_BANKS=['TEMPERAT','RATEMP','RASNOW','SNOW','WINTER']

# EVERY OTHER PROCESSED TEMPLATE HANDS ITS EDGES BACK TO THE APPROVED PAINTING in these
# banks. A template meets whatever template a map puts beside it, and the approved set joins
# the two with painted strips that reach past each template's edge over its neighbour. The
# daylight pass re-exposed each template on its own and gave every strip lying outside its
# template no opacity at all, so a processed template ended on a straight cell-edge line
# with a step in brightness across it. Here the processing fades out over the last
# EDGE_FADE texels before the template's edge (or an icon it does not have), so its edge is
# the approved painting, and the strips outside it are the authored ones again. A bank's
# own material pass (the temperate rock grade) is part of what fades: the edge is the
# approved painting of the material the template draws.
EDGE_BANKS=['RASNOW','RATEMP','SNOW','WINTER']
EDGE_FADE=48
EDGE_FACES={}

def edge_weight(r):
    """The template's present icons, and a weight of 1 inside falling to 0 at their edge."""
    w,h=r['cells'];have=np.zeros((h*C,w*C),bool)
    for icon in r['have']:
        y,x=divmod(icon,w);have[y*C:(y+1)*C,x*C:(x+1)*C]=True
    t=np.clip(nd.distance_transform_edt(np.pad(have,1))[1:-1,1:-1]/EDGE_FADE,0,1)
    return have,t*t*(3-2*t)

def bank_pass(bank,material,im):
    """A bank's own material pass over a template's picture: the temperate rock grade."""
    if bank in ['TEMPERAT','RATEMP']:return temperate_stone(im,checked(material,'rock_mask.png','L'))
    return im

def edge_faded(bank,r,im):
    """The processed picture, returning at the template's edge to the approved painting
    under the bank's own material pass."""
    have,k=edge_weight(r)
    a=np.asarray(bank_pass(bank,r,Image.open(ROOT/r['source']).convert('RGB')),np.float32);b=np.asarray(im,np.float32)
    return Image.fromarray(np.uint8(np.rint(a*(1-k[...,None])+b*k[...,None])),'RGB')

def edge_strip(bank,r,material,s,delivered,face):
    """A join strip: over the template's own icons, its final picture (face) under the
    delivered opacity; past its edge or over an icon it does not have, the authored strip
    of the painting it draws (material)."""
    w,h=r['cells'];have,_=edge_weight(r)
    face=np.asarray(face)
    mar=next(m for m in original_margins(material) if m['icon']==s['icon'] and m['side']==s['side'])
    authored=Image.open((ROOT/material['source']).parent/mar['file']).convert('RGBA')
    if bank in ['TEMPERAT','RATEMP']:authored=temperate_stone(authored,authored.getchannel('A'))
    authored=np.asarray(authored).copy()
    out=np.asarray(delivered).copy();mh,mw=out.shape[:2]
    cy,cx=divmod(s['icon'],w)
    ox,oy=(cx*C-mw,cy*C) if s['side']=='L' else (cx*C,cy*C-mh)
    yy,xx=np.indices((mh,mw));xx+=ox;yy+=oy
    inside=(xx>=0)&(yy>=0)&(xx<w*C)&(yy<h*C)
    iy,ix=np.clip(yy,0,h*C-1),np.clip(xx,0,w*C-1)
    own=inside&have[iy,ix]
    out[...,:3][own]=face[iy,ix][own]
    out[~own]=authored[~own]
    return Image.fromarray(out,'RGBA')

def template_picture(bank,r,material):
    """A template's picture as the terrain pack carries it: an approved shore painting, or
    the daylight albedo with its bank's material pass, faded to the approved painting at
    the template's edge in EDGE_BANKS."""
    if r['export'] in EDGE_FACES:return EDGE_FACES[r['export']]
    src=ROOT/r['source']
    if r['name'].startswith('SH') and bank in SHORE_BANKS:
        im=Image.open(src).convert('RGB')
    else:
        im=checked(material,'albedo.png','RGB')
        if bank=='DESERT':
            im=shared_stone(im,checked(material,'rock_mask.png','L')) if material is not r else desert_stone(r)[0]
        else:
            im=bank_pass(bank,material,im)
        if bank in EDGE_BANKS:
            im=edge_faded(bank,material,im)
    if bank in EDGE_BANKS:EDGE_FACES[r['export']]=im
    return im

def original_margins(r):
    meta=json.loads((ROOT/'art/remaster/textures'/r['theater']/'set_144.json').read_text())['templates'][r['name']]
    if r['variant']:meta=next(v for v in meta['variants']if v['n']==r['variant'])
    return meta.get('margins',[])

def cell_updates(bank,kind):
    result={}
    for r in rows(bank):
        src=ROOT/r['source']
        if hashlib.sha256(src.read_bytes()).hexdigest()!=r['source_sha256']:raise ValueError('Original source changed; re-audit '+str(src))
        material=surface_source(r)
        player=checked(material,'player_colour_mask.png','L')
        if player.getextrema()!=(0,0):raise ValueError('Terrain must retain its explicit non-recolourable mask')
        w,h=r['cells'];v=r['variant']
        if kind=='terrainenh':
            im=template_picture(bank,r,material)
            for icon in r['have']:
                y,x=divmod(icon,w);result[(r['tid'],field(icon,v,w,h))]=im.crop((x*C,y*C,(x+1)*C,(y+1)*C)).tobytes()
        else:
            for s in material['margins']:
                p=SOURCE/s['file']
                if hashlib.sha256(p.read_bytes()).hexdigest()!=s['sha256']:raise ValueError('Daylight join source changed: '+str(p))
                im=Image.open(p).convert('RGBA')
                if r['name'].startswith('SH') and bank in SHORE_BANKS:
                    mar=next(m for m in original_margins(r) if m['icon']==s['icon'] and m['side']==s['side'])
                    im=Image.open(src.parent/mar['file']).convert('RGBA')
                elif bank in EDGE_BANKS:
                    im=edge_strip(bank,r,material,s,im,template_picture(bank,r,material))
                elif bank in ['TEMPERAT','RATEMP']:
                    # Match the rock colour on both sides of a strip; alpha remains
                    # the explicit authored feature matte, never inferred from hue.
                    im=temperate_stone(im,im.getchannel('A'))
                elif bank=='DESERT':
                    if material is not r:
                        im=shared_stone(im,im.getchannel('A'))
                        result[(r['tid'],field(s['icon'],v,w,h),0 if s['side']=='L' else 1)]=im.tobytes()
                        continue
                    if re.fullmatch(r'S\d+',r['name']) or r.get('surface_kind')=='river-rock':
                        # Strip pixels use the same template coordinates and RGB
                        # as the face they join. Alpha alone feathers the border.
                        rgba=np.array(im); mh,mw=rgba.shape[:2]
                        cy,cx=divmod(s['icon'],w)
                        ox,oy=(cx*C-mw,cy*C) if s['side']=='L' else (cx*C,cy*C-mh)
                        yy,xx=np.indices((mh,mw)); xx+=ox; yy+=oy
                        face=np.asarray(desert_stone(r)[0])
                        from road_continuity import margin_coordinates
                        xx,yy=margin_coordinates(r,s,(mh,mw),C)
                        rgba[...,:3]=face[yy,xx]
                        im=Image.fromarray(rgba,'RGBA')
                        result[(r['tid'],field(s['icon'],v,w,h),0 if s['side']=='L' else 1)]=im.tobytes()
                        continue
                    # This is a one-time authored surface selection for the margin
                    # pixels. The runtime still receives only packed RGB and exact alpha.
                    rgba=np.asarray(im,dtype=np.float32)/255.0;rgb=rgba[...,:3]
                    matte=rgba[...,3]*((rgb[...,0]<=rgb[...,1]*1.50)&(rgb[...,1]<=rgb[...,0]*1.12)&(rgb[...,2]<=rgb[...,1]*1.12))
                    if re.fullmatch(r'S\d+',r['name']):matte=rgba[...,3]
                    mh,mw=rgb.shape[:2];yy,xx=np.indices((mh,mw),dtype=np.float32);tex,mean_y=desert_sample(xx,yy,r['tid'])
                    yv=rgb@WEIGHTS;ty=tex@WEIGHTS
                    painted=yv[...,None]*.92*(tex/np.maximum(ty[...,None],.02))*(.85+.15*ty[...,None]/mean_y)
                    rgba[...,:3]=rgb*(1-matte[...,None])+painted*matte[...,None]
                    im=Image.fromarray(np.rint(np.clip(rgba,0,1)*255).astype(np.uint8),'RGBA')
                result[(r['tid'],field(s['icon'],v,w,h),0 if s['side']=='L' else 1)]=im.tobytes()
    if bank=='DESERT' and kind!='terrainenh':
        art=ROOT/'art/remaster/textures/DESERT'
        templates=json.loads((art/'set_144.json').read_text())['templates']
        for name in ('P05','P06'):
            meta=templates[name];w,h=meta['cells']
            for strip in meta.get('margins',[]):
                size=Image.open(art/name/strip['file']).size
                result[(meta['tid'],field(strip['icon'],0,w,h),0 if strip['side']=='L' else 1)]=bytes(size[0]*size[1]*4)
    from road_continuity import updates as road_updates
    result.update(road_updates(ROOT,bank,kind))
    return result

def apply(path,bank,kind):
    if not rows(bank):return
    raw=bytearray(Path(path).read_bytes());ks=4 if kind=='terrainenh' else 8;head=20 if ks==4 else 24
    if ks==4:
        if raw[:8]!=b'C3DT30\0\0':raise ValueError('Not a native Enhanced pack')
        ver,c,n=struct.unpack_from('<III',raw,8);stride=c*c*3
    else:
        if raw[:8]!=b'C3DSEAM\0':raise ValueError('Not a native join pack')
        ver,c,margin,n=struct.unpack_from('<IIII',raw,8);stride=c*margin*4
    if c!=C:return
    assert len(raw)==head+n*ks+n*stride
    updates=cell_updates(bank,kind);found=set();start=head+n*ks
    if ks==8:
        # Sharing a painting also shares its complete external feather strips.
        # RA's old strip inventory differs; keeping it clips the TD border, while
        # leaving old extra strips paints fragments of the former RA cliff back.
        shared={(r['tid'],r['variant']) for r in rows(bank) if surface_source(r) is not r}
        records={}
        for i in range(n):
            tid,icon=struct.unpack_from('<HH',raw,head+i*ks)
            key=(tid,icon,raw[head+i*ks+4])
            if (tid,(icon>>8)&3) in shared and key not in updates:continue
            records[key]=(bytes(raw[head+i*ks:head+(i+1)*ks]),bytes(raw[start+i*stride:start+(i+1)*stride]))
        for key,pixels in updates.items():
            assert len(pixels)==stride
            # Keep authored strip kind/reserved metadata for every existing key.
            # New shared cliff strips use the ordinary feather-strip kind (zero).
            packed_key=records[key][0] if key in records else struct.pack('<HHB3x',*key)
            records[key]=(packed_key,pixels)
        header=raw[:head];struct.pack_into('<I',header,20,len(records))
        Path(path).write_bytes(header+b''.join(r[0] for r in records.values())+b''.join(r[1] for r in records.values()))
        return
    for i in range(n):
        tid,icon=struct.unpack_from('<HH',raw,head+i*ks);key=(tid,icon) if ks==4 else (tid,icon,raw[head+i*ks+4])
        if key in updates:
            assert len(updates[key])==stride
            raw[start+i*stride:start+(i+1)*stride]=updates[key];found.add(key)
    if found!=set(updates):raise ValueError('Daylight keys missing: '+repr(set(updates)-found))
    Path(path).write_bytes(raw)

def apply_ground(path,bank):
    raw=Path(path).read_bytes();assert raw[:8]==b'C3DGRAS\0';ver,c,n=struct.unpack_from('<III',raw,8);assert c==C and ver==1
    stride=c*c*2;start=20+n*24;assert len(raw)==start+n*stride
    updates={};names={}
    for r in rows(bank):
        w,h=r['cells'];cov=checked(surface_source(r),'ground_mask.png','L')
        if bank=='DESERT' and surface_source(r) is r:cov=Image.fromarray(desert_stone(r)[1],'L')
        if bank in ['TEMPERAT','RATEMP'] and r['name'].startswith('SH'):
            # A beach is ground, but it is not CLEAR1 grass. Restore the exact
            # accepted grass-only outline instead of the mineral complement.
            refs=json.loads((SOURCE/'shore_grass_v01/MANIFEST.json').read_text())['records']
            name=r['name']+(('_v'+str(r['variant']))if r['variant']else'')
            ref=refs[bank+'/'+name]
            if 'file' in ref:
                p=ROOT/ref['file']
                if hashlib.sha256(p.read_bytes()).hexdigest()!=ref['sha256']:raise ValueError('Approved shore grass mask changed: '+str(p))
                cov=Image.open(p).convert('L')
                if cov.size!=(w*C,h*C):raise ValueError('Shore grass mask dimensions changed')
            else:cov=Image.new('L',(w*C,h*C),0)

        for icon in r['have']:
            y,x=divmod(icon,w);key=(r['tid'],icon,r['variant'],int(bank.startswith('RA')))
            updates[key]=cov.crop((x*C,y*C,(x+1)*C,(y+1)*C)).tobytes();names[key]=r['name']
    if bank=='RASNOW':
        from road_continuity import ground_updates as snow_road_ground
        road_masks,road_names=snow_road_ground(ROOT,bank);updates.update(road_masks);names.update(road_names)
    if bank=='DESERT':
        from desert_feature_edges import ground_updates
        for key,pixels in ground_updates(ROOT).items():
            updates[key]=pixels;names[key]=('D%02d'%(key[0]-92)) if 93<=key[0]<=135 else ('P%02d'%(key[0]-66))
    records=[];found=set()
    for i in range(n):
        k=raw[20+i*24:44+i*24];tid,icon,v,game,alternate=struct.unpack_from('<HHBBH',k,16)
        pixels=bytearray(raw[start+i*stride:start+(i+1)*stride]);key=(tid,icon,v,game)
        if not alternate:
            if key in updates:pixels[::2]=updates[key];found.add(key)
            if bank=='DESERT' and tid in (71,72):pixels[::2]=bytes([255])*(c*c)
        pixels[1::2]=bytes([128])*(c*c);records.append((k,bytes(pixels)))
    # Previously omitted bridge/falls/mineral templates need actual keys, not a
    # silent no-op when their new saved masks have no predecessor in the pack.
    for key in sorted(updates.keys()-found):
        k=names[key].encode().ljust(16,b'\0')+struct.pack('<HHBBH',*key,0)
        pixels=bytearray(stride);pixels[::2]=updates[key];pixels[1::2]=bytes([128])*(c*c)
        records.append((k,bytes(pixels)))
    Path(path).write_bytes(b'C3DGRAS\0'+struct.pack('<III',ver,c,len(records))+b''.join(k for k,p in records)+b''.join(p for k,p in records))

def surface(out,bank):
    keys=[];pixels=[];hc=48
    for r in rows(bank):
        w,h=r['cells'];im=checked(surface_source(r),'height.png','L')
        for icon in r['have']:
            y,x=divmod(icon,w);keys.append(r['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',r['tid'],icon,r['variant'],int(bank.startswith('RA')),0))
            pixels.append(im.crop((x*C,y*C,(x+1)*C,(y+1)*C)).resize((hc,hc),Image.Resampling.BOX).tobytes())
    assert len(keys)==len(set(keys))
    rt={'RATEMP':'RA_TEMPERAT','RASNOW':'RA_SNOW'}.get(bank,bank)
    (out/f'terrainsurface_{rt}.pack').write_bytes(b'C3DSURF\0'+struct.pack('<III',1,hc,len(keys))+b''.join(keys)+b''.join(pixels))

def apply_coast(path,bank):
    """Overlay saved complete-template coast coverage, shared with bank ground."""
    raw=Path(path).read_bytes();assert raw[:8]==b'C3DCOAS\0'
    ver,c,n=struct.unpack_from('<III',raw,8);assert ver==1 and c==144
    start=20+n*24;stride=c*c;assert len(raw)==start+n*stride
    updates={};names={}
    for r in rows(bank):
        if 'land_alpha_sha256'not in r:continue
        p=SOURCE/r['export']/'land_alpha.png'
        if hashlib.sha256(p.read_bytes()).hexdigest()!=r['land_alpha_sha256']:raise ValueError('Coast source changed: '+str(p))
        a=Image.open(p).convert('L');w,h=r['cells']
        for icon in r['have']:
            y,x=divmod(icon,w);key=(r['tid'],icon,r['variant'],int(bank.startswith('RA')))
            updates[key]=a.crop((x*C,y*C,(x+1)*C,(y+1)*C)).tobytes();names[key]=r['name']
    records=[];found=set()
    for i in range(n):
        k=raw[20+i*24:44+i*24];key=struct.unpack_from('<HHBB',k,16)
        records.append((k,updates.get(key,raw[start+i*stride:start+(i+1)*stride])));found.add(key)
    for key in sorted(updates.keys()-found):
        records.append((names[key].encode().ljust(16,b'\0')+struct.pack('<HHBBH',*key,0),updates[key]))
    Path(path).write_bytes(b'C3DCOAS\0'+struct.pack('<III',ver,c,len(records))+b''.join(k for k,p in records)+b''.join(p for k,p in records))

def build(out):
    out.mkdir(parents=True,exist_ok=True)
    for bank in sorted({r['theater'] for r in manifest()['assets']}):surface(out,bank)
    for name,bank in [('grassmask_TEMPERAT.pack','TEMPERAT'),('grassmask_TEMPERAT.pack','RATEMP'),('groundmask_DESERT.pack','DESERT'),('groundmask_RA_SNOW.pack','RASNOW')]:
        if (out/name).exists():apply_ground(out/name,bank)
    if (out/'coastmask_DESERT.pack').exists():apply_coast(out/'coastmask_DESERT.pack','DESERT')
    from native_coast_edges import apply as repair_native_edges
    repair_native_edges(out)
    from terrain_connectors import apply as repair_connectors
    repair_connectors(out)
    print('Daylight:',len(manifest()['assets']),'source versions; six shared physical-surface packs')
if __name__=='__main__':build(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'game')
