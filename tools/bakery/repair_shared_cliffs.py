#!/usr/bin/env python3
"""Rebuild the saved shared cliff mattes without eroding template boundary joins.

This is an offline mask repair over immutable source labels, not runtime colour
classification. Edge replication is essential for both rock and ground labels:
zero-border closing erodes true joins and preserves a false two-pixel mineral rim.
"""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image
from scipy import ndimage as nd

def repair(root):
    source=root/'art/remaster/textures/unified_daylight_v01'
    manifest=source/'MANIFEST.json';data=json.loads(manifest.read_text())
    for r in data['assets']:
        if r['theater']!='TEMPERAT' or not r['name'][1:].isdigit() or not r['name'].startswith('S'):continue
        old=source/r.get('pre_shared_cleanup_export',r['export'])
        dest=source/'shared_temperate_cliffs_v04'/old.relative_to(source/'TEMPERAT')
        dest.mkdir(parents=True,exist_ok=True)
        rock=np.asarray(Image.open(old/'rock_mask.png'))>127
        pad=8
        rock=np.pad(rock,pad,mode='edge')
        rock=nd.binary_fill_holes(nd.binary_closing(rock,iterations=2))
        rock=nd.binary_opening(rock,iterations=1)[pad:-pad,pad:-pad]
        labels,n=nd.label(rock);sizes=np.bincount(labels.ravel());rock&=sizes[labels]>100
        w,h=r['cells'];have=np.zeros(rock.shape,bool)
        for icon in r['have']:
            y,x=divmod(icon,w);have[y*144:(y+1)*144,x*144:(x+1)*144]=True
        rock&=have
        # Source-painted grass is ground, not physical stone.
        # Follow only source-labelled colour regions connected to the outside;
        # closing the retained mineral matte protects thin interior fissures.
        rgb=np.asarray(Image.open(root/r['source']).convert('RGB'),dtype=float)/255
        rr,gg,bb=rgb.transpose(2,0,1)
        grass=(gg>rr*1.035)&(bb<gg*.98)
        suspect=nd.binary_closing(np.pad(grass,4,mode='edge'),iterations=2)[4:-4,4:-4]
        connected=nd.binary_propagation(~rock,mask=(~rock)|suspect)
        before=int(rock.sum());rock &= ~connected
        rock=nd.binary_fill_holes(nd.binary_closing(np.pad(rock,8,mode='edge'),iterations=3))[8:-8,8:-8]&have
        rock=nd.binary_fill_holes(nd.binary_opening(np.pad(rock,8,mode='edge'),iterations=3))[8:-8,8:-8]&have
        r['removed_outside_connected_painted_ground_pixels']=before-int(rock.sum())
        r.pop('removed_outside_connected_painted_grass_shadow_pixels',None)
        outside=np.clip(nd.distance_transform_edt(~np.pad(rock,pad,mode='edge'))[pad:-pad,pad:-pad]/3,0,1)
        ground=outside*outside*(3-2*outside)*have
        height=np.asarray(Image.open(old/'height.png'),dtype=float)/255
        rise=np.clip(nd.distance_transform_edt(np.pad(rock,16,mode='edge'))[16:-16,16:-16]/12,0,1)
        height=nd.gaussian_filter(height*rise,2,mode='nearest')*rise
        for f,a in [('rock_mask.png',rock),('ground_mask.png',ground),('height.png',height)]:
            Image.fromarray(np.uint8(np.rint(a*255))).save(dest/f)
        for f in ['albedo.png','player_colour_mask.png']:(dest/f).write_bytes((old/f).read_bytes())
        for f,k in [('albedo.png','albedo_sha256'),('rock_mask.png','rock_mask_sha256'),('ground_mask.png','ground_mask_sha256'),('height.png','height_sha256'),('player_colour_mask.png','player_colour_mask_sha256')]:r[k]=hashlib.sha256((dest/f).read_bytes()).hexdigest()
        r['export']=str(dest.relative_to(source));r['rock_pixels']=int(rock.sum())
        # Preserve original external strips. They are independently feathered by
        # their authored alpha; do not multiply them by an unrelated edge texel.
        r['margins']=r.get('pre_shared_cleanup_margins',r['margins'])
    manifest.write_text(json.dumps(data,indent=2)+'\n')

if __name__=='__main__':repair(Path(__file__).resolve().parents[2])
