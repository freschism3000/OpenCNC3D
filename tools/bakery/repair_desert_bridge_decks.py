#!/usr/bin/env python3
"""Prepare a candidate-only Desert bridge deck repair from existing bridge paint.

BRIDGE3's saved source has sand-shaped holes in the road, with matching clear-
blend coverage. This repair uses an explicit deck region and immutable masonry
regions, then borrows only the already accepted BRIDGE4 road pixels. It does not
infer bridge geometry from diffuse colour or change any physical terrain maps.
The canonical daylight manifest is never written by this tool.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import shutil

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as nd

CELL = 144
DECKS = {
    'BRIDGE3': [[(179,455),(445,201),(598,240),(365,484),(353,495),(218,501)]],
    # BRIDGE4 already has a continuous road. Its semantic core is a control;
    # its RGB is deliberately immutable, including every existing road detail.
    'BRIDGE4': [[(225,210),(389,135),(646,375),(469,447)]],
}
MASONRY = {
    'BRIDGE3': [
        [(177,467),(145,430),(144,414),(206,346),(218,321),(258,290),
         (275,276),(280,263),(316,232),(321,214),(340,193),(368,188),
         (398,163),(422,155),(438,164),(445,199),(425,219),(401,243),
         (381,263),(359,285),(336,308),(315,329),(293,351),(273,371),
         (251,393),(230,414),(207,437),(187,457)],
        [(357,490),(363,461),(374,450),(404,420),(426,398),(446,378),
         (467,357),(489,335),(510,314),(534,290),(554,270),(575,249),
         (595,229),(612,212),(650,223),(645,254),(635,269),(631,304),
         (616,324),(608,356),(589,372),(579,406),(559,423),(554,455),
         (532,470),(520,488),(500,492),(484,504),(461,510),(435,519),
         (398,520),(374,505)],
    ],
    'BRIDGE4': [
        [(169,185),(184,171),(205,173),(239,204),(243,226),(277,246),
         (287,270),(332,294),(337,319),(379,347),(396,370),(440,402),
         (462,439),(462,458),(423,466),(388,443),(344,418),(303,389),
         (261,354),(225,322),(211,291),(206,267),(213,244),(174,217)],
        [(381,113),(401,106),(435,123),(439,148),(471,173),(479,194),
         (512,210),(516,229),(553,254),(563,278),(602,300),(615,324),
         (653,350),(666,371),(696,389),(696,407),(669,408),(640,389),
         (604,362),(567,330),(526,296),(494,267),(464,238),(428,206),
         (398,172),(384,145)],
    ],
}
# This independent, conservative interior region contains no wall, water or
# approach. Closest-point clamping avoids ever borrowing BRIDGE4 masonry.
DONOR_QUAD = [(628,368),(392,163),(263,217),(492,422)]
DESTINATION_QUAD = [(180,460),(445,200),(598,240),(353,501)]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mask(size, polygons):
    im = Image.new('L', size)
    draw = ImageDraw.Draw(im)
    for polygon in polygons:
        draw.polygon(polygon, fill=255)
    return np.asarray(im) > 0


def save_mask(path, data):
    Image.fromarray(np.uint8(data) * 255).save(path)


def donor_homography():
    """Measured four-point map; preserves useful road grain without edge smear."""
    equations, targets = [], []
    for (x,y),(u,v) in zip(DESTINATION_QUAD,DONOR_QUAD):
        equations.extend([[x,y,1,0,0,0,-u*x,-u*y],
                          [0,0,0,x,y,1,-v*x,-v*y]])
        targets.extend([u,v])
    return np.append(np.linalg.solve(equations,targets),1).reshape(3,3)


def donor_coordinates(shape):
    """Apply saved projective map, then clamp only outside the safe donor road."""
    yy, xx = np.indices(shape, dtype=float)
    homogeneous = np.stack((xx,yy,np.ones(shape)),axis=-1)@donor_homography().T
    points = homogeneous[...,:2]/homogeneous[...,2,None]
    vertices = np.asarray(DONOR_QUAD, dtype=float)
    # Points inside the convex polygon retain the exact projective map;
    # outside points use the nearest edge point, regardless of winding.
    cross = []
    candidates = []
    distances = []
    for a, b in zip(vertices, np.roll(vertices, -1, axis=0)):
        edge = b-a
        local = points-a
        cross.append(edge[0]*local[...,1]-edge[1]*local[...,0])
        t = np.clip(np.sum(local*edge, axis=-1)/np.dot(edge,edge), 0, 1)
        projected = a+t[...,None]*edge
        candidates.append(projected)
        distances.append(np.sum((points-projected)**2, axis=-1))
    cross = np.stack(cross)
    inside = np.all(cross >= -1e-6,axis=0)|np.all(cross <= 1e-6,axis=0)
    nearest = np.argmin(np.stack(distances), axis=0)
    projected = np.take_along_axis(np.stack(candidates),
                                  nearest[None,...,None], axis=0)[0]
    points = np.where(inside[...,None], points, projected)
    return points[...,0], points[...,1], ~inside


def sample_rgb(rgb, x, y):
    # Bilinear sampling of the existing accepted paint; no synthetic grain,
    # colour grading, sharpening, high-frequency noise or generated pixels.
    return np.stack([nd.map_coordinates(rgb[...,i].astype(float), [y,x],
                                       order=1, mode='nearest')
                     for i in range(3)], axis=-1)


def road_end_weight(shape):
    """Road-end transitions, independent of road-facing wall boundaries."""
    yy,xx = np.indices(shape,dtype=float)
    far = (153*(yy-201)-39*(xx-445))/np.hypot(153,39)
    near = (40*(xx-179)-186*(yy-455))/np.hypot(186,40)
    weight = np.clip(np.minimum(far/24,near/16),0,1)
    return weight*weight*(3-2*weight)


def repair(project, out):
    project = project.resolve()
    root = project/'art/remaster/textures/unified_daylight_v01'
    manifest_path = root/'MANIFEST.json'
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    candidate = copy.deepcopy(manifest)
    rows = {r['name']:r for r in candidate['assets']
            if r['theater']=='DESERT' and r['variant']==0}
    originals = {name:root/rows[name].get('pre_bridge_decks_v07_export',
                                        rows[name]['export'])
                 for name in DECKS}
    donor_path = originals['BRIDGE4']/'albedo.png'
    donor = np.asarray(Image.open(donor_path).convert('RGB'))
    out.mkdir(parents=True, exist_ok=True)
    regions = {
        'schema':1,
        'units':'whole-template native Enhanced pixels; 144 px/cell; top-left origin',
        'source_exports':{name:str(path.relative_to(root))
                          for name,path in originals.items()},
        'deck_polygons':DECKS,
        'masonry_polygons':MASONRY,
        'selection':'explicit polygons, occupied cells, opaque land; no RGB classification',
        'road_end_transitions':{
            'BRIDGE3':{
                'far_line':[[445,201],[598,240]],'far_inward_feather_px':24,
                'near_line':[[179,455],[365,495]],'near_inward_feather_px':16,
                'profile':'smoothstep; full-strength core is min normalized inward distance >= 1',
            },
            'BRIDGE4':'none; accepted RGB and ground maps remain byte-exact',
        },
        'donor':{
            'source':str(donor_path.relative_to(root)),
            'sha256':sha(donor_path),
            'homography_destination_to_source':donor_homography().tolist(),
            'control_points_destination':DESTINATION_QUAD,
            'control_points_source':DONOR_QUAD,
            'safe_source_polygon':DONOR_QUAD,
            'clamp':'Euclidean closest point on convex safe source polygon',
            'sampling':'bilinear; native RGB, no hue or brightness adjustment',
        },
        'contract':{
            'BRIDGE3':'repair existing ground islands plus a 6px road-side blend; 24px far/16px near smooth ends join existing approaches; all full-strength deck coverage zero',
            'BRIDGE4':'entire RGB immutable; remove ground coverage only in semantic deck core',
            'preserve_exact':['height.png','rock_mask.png','land_alpha.png',
                              'player_colour_mask.png','all margin PNGs',
                              'all pixels outside deck support',
                              'all authored masonry semantic pixels'],
            'crossing_protected_mask':'previous protected mask minus new authored deck support; masonry has its own exact preservation mask',
        },
    }
    region_path = out/'deck_regions.json'
    region_path.write_text(json.dumps(regions,indent=2)+'\n')
    reports = []
    for name, row in ((name,rows[name]) for name in DECKS):
        old = originals[name]
        dest = out/name
        dest.mkdir(exist_ok=True)
        for file in old.iterdir():
            if file.is_file() and file.suffix=='.png':
                shutil.copy2(file,dest/file.name)
        rgb = np.asarray(Image.open(old/'albedo.png').convert('RGB'))
        ground = np.asarray(Image.open(old/'ground_mask.png').convert('L'))
        alpha = np.asarray(Image.open(old/'land_alpha.png').convert('L'))
        rock = np.asarray(Image.open(old/'rock_mask.png').convert('L'))
        height = np.asarray(Image.open(old/'height.png').convert('L'))
        protected = np.asarray(Image.open(old/'crossing_protected_mask.png').convert('L'))>127
        h,w = ground.shape
        have = np.zeros((h,w),bool)
        for icon in row['have']:
            iz,ix = divmod(icon,row['cells'][0])
            have[iz*CELL:(iz+1)*CELL,ix*CELL:(ix+1)*CELL] = True
        masonry = mask((w,h),MASONRY[name])&have
        support = mask((w,h),DECKS[name])&have&~masonry
        end_weight = road_end_weight((h,w)) if name=='BRIDGE3' else np.ones((h,w))
        semantic = support&(end_weight==1)
        assert not np.any(support&(alpha!=255)), name+' deck touches water alpha'
        assert not np.any(support&(rock!=0)), name+' deck touches mineral relief'
        assert not np.any(support&(height!=0)), name+' deck touches raised relief'
        new_ground = ground.copy()
        new_ground[support] = np.uint8(np.rint(ground[support]*(1-end_weight[support])))
        weight = np.zeros((h,w),float)
        result = rgb.copy()
        clamp_count = 0
        if name=='BRIDGE3':
            # Existing saved coverage identifies the known erroneous islands,
            # only after the explicit semantic deck/masonry decision. Extend
            # the blend into adjacent original road so there is no sand rim.
            islands = support&(ground>0)
            distance = nd.distance_transform_edt(~islands)
            weight = np.clip(1-distance/6.0,0,1)
            weight = weight*weight*(3-2*weight)*support*end_weight
            x,y,clamped = donor_coordinates((h,w))
            paint = sample_rgb(donor,x,y)
            result = np.uint8(np.rint(rgb*(1-weight[...,None])+
                                     paint*weight[...,None]))
            clamp_count = int((clamped&(weight>0)).sum())
        Image.fromarray(result).save(dest/'albedo.png')
        Image.fromarray(new_ground).save(dest/'ground_mask.png')
        save_mask(dest/'deck_semantic_mask.png',semantic)
        save_mask(dest/'deck_support_mask.png',support)
        save_mask(dest/'masonry_semantic_mask.png',masonry)
        save_mask(dest/'crossing_protected_mask.png',protected&~support)
        Image.fromarray(np.uint8(np.rint(weight*255))).save(dest/'deck_donor_weight.png')
        Image.fromarray(np.uint8(np.rint(end_weight*support*255))).save(dest/'deck_end_weight.png')
        assert np.array_equal(result[~support],rgb[~support])
        assert np.array_equal(new_ground[~support],ground[~support])
        assert np.array_equal(result[masonry],rgb[masonry])
        assert np.array_equal(new_ground[masonry],ground[masonry])
        immutable = ['height.png','rock_mask.png','land_alpha.png','player_colour_mask.png']
        immutable += [f.name for f in old.glob('margin_*.png')]
        assert all(sha(dest/f)==sha(old/f) for f in immutable)
        if name=='BRIDGE4':
            assert np.array_equal(result,rgb)
            # Preserve the byte representation, too, not only decoded RGB.
            shutil.copy2(old/'albedo.png',dest/'albedo.png')
        row['pre_bridge_decks_v07_export'] = str(old.relative_to(root))
        row['export'] = str(dest.relative_to(root))
        row['bridge_deck_region_source'] = str(region_path.relative_to(root))
        row['bridge_deck_region_sha256'] = sha(region_path)
        row['bridge_deck_recipe'] = ('Explicit full deck exclusion from clear blend; '
            'BRIDGE3 ground islands repaired with existing BRIDGE4 native road paint; '
            'masonry, bank mineral, water alpha and physical relief exact.')
        for filename,key in [
            ('albedo.png','albedo_sha256'),('ground_mask.png','ground_mask_sha256'),
            ('deck_semantic_mask.png','deck_semantic_mask_sha256'),
            ('deck_support_mask.png','deck_support_mask_sha256'),
            ('masonry_semantic_mask.png','masonry_semantic_mask_sha256'),
            ('deck_donor_weight.png','deck_donor_weight_sha256'),
            ('deck_end_weight.png','deck_end_weight_sha256'),
        ]:
            row[key] = sha(dest/filename)
        for margin in row['margins']:
            p = dest/Path(margin['file']).name
            margin['file'] = str(p.relative_to(root))
            margin['sha256'] = sha(p)
        changed = np.any(result!=rgb,axis=-1)
        reports.append({
            'name':name,'tid':row['tid'],'occupied_cells':len(row['have']),
            'deck_semantic_pixels':int(semantic.sum()),
            'deck_support_pixels':int(support.sum()),
            'masonry_semantic_pixels':int(masonry.sum()),
            'changed_rgb_pixels':int(changed.sum()),
            'changed_ground_pixels':int(np.count_nonzero(new_ground!=ground)),
            'old_deck_ground_nonzero':int(np.count_nonzero(ground[support])),
            'new_deck_ground_nonzero':int(np.count_nonzero(new_ground[support])),
            'new_deck_core_ground_nonzero':int(np.count_nonzero(new_ground[semantic])),
            'donor_clamped_blend_pixels':clamp_count,
            'outside_deck_rgb_and_ground_exact':True,
            'masonry_rgb_and_ground_exact':True,
            'immutable_maps_and_margins_exact':True,
            'albedo_sha256':sha(dest/'albedo.png'),
            'ground_mask_sha256':sha(dest/'ground_mask.png'),
            'source_export':str(old.relative_to(root)),
            'source_albedo_sha256':sha(old/'albedo.png'),
            'source_ground_sha256':sha(old/'ground_mask.png'),
        })
        # Evidence is a direct pixel preview of the actual candidate maps.
        before = Image.fromarray(rgb)
        after = Image.fromarray(result)
        sheet = Image.new('RGB',(w*2,h+28),(22,22,22))
        sheet.paste(before,(0,28)); sheet.paste(after,(w,28))
        draw = ImageDraw.Draw(sheet)
        draw.text((8,8),name+' v06 source',fill='white')
        draw.text((w+8,8),name+' v07 candidate (actual pixels)',fill='white')
        sheet.save(out/(name+'-before-after.png'))
        overlay = before.copy()
        draw = ImageDraw.Draw(overlay)
        for poly in DECKS[name]:draw.line(poly+[poly[0]],fill='cyan',width=2)
        for poly in MASONRY[name]:draw.line(poly+[poly[0]],fill='magenta',width=2)
        overlay.save(out/(name+'-semantic-regions.png'))
    (out/'MANIFEST.candidate.json').write_text(json.dumps(candidate,indent=2)+'\n')
    report = {'schema':1,'candidate_only':True,'canonical_manifest_updated':False,
              'canonical_manifest_sha256':hashlib.sha256(manifest_bytes).hexdigest(),
              'region_metadata_sha256':sha(region_path),'templates':reports}
    (out/'REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
    assert manifest_path.read_bytes()==manifest_bytes
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project',type=Path,required=True)
    p.add_argument('--out',type=Path)
    a = p.parse_args()
    repair(a.project,a.out or a.project/'art/remaster/textures/unified_daylight_v01/desert_bridge_decks_v07')
