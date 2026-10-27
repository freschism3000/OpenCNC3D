#!/usr/bin/env python3
"""G353: Red Alert 3D's atom bomb draws its own blast under Enhanced (game/ra3d_atom_mod.h,
effects_mod.h's g_efxRecipeOf, g_efxOwnMeshes and g_efxHostMeshes, the ATOM_BLAST_RA rows of
game/efx_enhanced.h, ra3d/efx/ATOMSFX.efxm written into the module by tools/ra3d/stage-structures.py
from tools/art/remaster/ra/nuke3d_ATOMSFX.py).

    python3 game/gate_ra3datom.py script DIR     write the runs' missions and scripts
    python3 game/gate_ra3datom.py check DIR      read the logs: "1 message" or "0 message"

Run from a play folder with a staged Red Alert module. Three hidden runs:

  t  Allied 5, temperate: an atom bomb raised by the engine (`efxanim 60`) where the camera
     looks, every tick of its 111 frames sampled (atomdump, efxdump), shots along the way
  s  Allied 1, snow: the same
  n  Allied 2, snow: a Missile Silo and a yard added for the player at tech level 15, its
     missile recharged and fired
     from the sidebar (SW_Nuke) at a field north of the base, the real launch, sampled the
     same

Claims, for every run:
  (1) the file loaded: 13 parts, 27,636 triangles, 111 frames;
  (2) the blast was armed once at the anim's stage 0 and drawn on every frame 0 to 110 (the
      anim itself ends earlier: its last stage is reported), each frame drawing exactly the
      parts the file has visible on it (read here from the staged file itself);
  (3) Tiberian Dawn's nuclear strike stood down: no EFXNUKE line (the NUKEFX mushroom), the
      anim fired the recipe ATOM_BLAST_RA and not ATOM_BLAST, and its particles spawned;
  (4) the flash: nothing at frame 0, 0.85 at frame 1, falling, nothing from frame 8; the
      ground shook once, at frame 19;
  (5) the pictures: the frame 1 shot is far brighter than the one before the blast, and the
      frame 30 shot differs from it where the blast stands.
Tiberian Dawn's ATOM_BLAST is not run here: its pictures are compared byte for byte before
and after by hand, and G97 holds its mushroom.

PROVEN TO FAIL: the renderer before this work prints no RA3DATOM line and draws Tiberian
Dawn's mushroom (EFXNUKE) and fire ring (ATOM_BLAST) for Red Alert's atom bomb.
NOT COVERED: how it looks (the frame strips, by eye); the damage and its timing are the
engine's.
"""
import json
import os
import re
import shutil
import struct
import sys

PLAY = os.getcwd()
FRAMES = 111
# tag: scenario, theater, the camera, where the bomb goes off, the silo's house and cell
RUNS = {
    't': ('SCG05EA', 'temperate', (52, 85), (52, 82), None),
    's': ('SCG01EA', 'snow', (66, 56), (66, 53), None),
    'n': ('SCG02EA', 'snow', (90, 53), (90, 50), ('Greece', (80, 56))),
}
SHOTS = (0, 1, 4, 10, 19, 30, 50, 80, 110)


# The flash-image witness was authored at d51f4699 with this complete photometric rig.
# Keep its legacy lighting/grade/atmosphere while shared player defaults continue to evolve.
REFERENCE_LIGHT = [
    'gfx bloom_clamp 8.875',
    'gfx bloom_intensity 0.92856997',
    'gfx bloom_knee 0.15000001',
    'gfx bloom_on 1',
    'gfx bloom_passes 4',
    'gfx bloom_radius 1.9375',
    'gfx bloom_threshold 0.68000001',
    'gfx clear_saturation 0.54911',
    'gfx cloud_cover 0.34799999',
    'gfx cloud_height 12',
    'gfx cloud_on 1',
    'gfx cloud_rot 67.5',
    'gfx cloud_scale 20',
    'gfx cloud_sharp 0.035999998',
    'gfx cloud_speed 0.57099998',
    'gfx cloud_strength 0.30357',
    'gfx contact_bias 0.02',
    'gfx contact_len 0.30000001',
    'gfx contact_on 1',
    'gfx contact_steps 8',
    'gfx contact_strength 1',
    'gfx contact_thick 0.60000002',
    'gfx contrast 1.22321',
    'gfx dfog_d_amount 0.3125',
    'gfx dfog_d_b 0.62',
    'gfx dfog_d_depth 20.848221',
    'gfx dfog_d_g 0.75999999',
    'gfx dfog_d_r 0.83036',
    'gfx dfog_d_start 1.1428601',
    'gfx dfog_on 1',
    'gfx dfog_t_amount 0.33036',
    'gfx dfog_t_b 0.76999998',
    'gfx dfog_t_depth 22.24107',
    'gfx dfog_t_g 0.74106997',
    'gfx dfog_t_r 0.69999999',
    'gfx dfog_t_start 2',
    'gfx exposure -0.10714',
    'gfx fog_density 0.053569999',
    'gfx fog_fx 1',
    'gfx fog_height 1.46964',
    'gfx fog_light 4.5',
    'gfx fog_scale 2',
    'gfx fog_speed 0.57143003',
    'gfx fog_wind 64.285713',
    'gfx gain 1.55804',
    'gfx gamma 1',
    'gfx gamma_on 0',
    'gfx grade_on 1',
    'gfx hdr_on 0',
    'gfx lift -0.03571',
    'gfx light_b 0.36000001',
    'gfx light_explosions 1',
    'gfx light_fade 0.26786',
    'gfx light_fade_muzzle 0.15000001',
    'gfx light_falloff 1.88393',
    'gfx light_fire 1',
    'gfx light_g 0.72000003',
    'gfx light_intensity 0.96429002',
    'gfx light_muzzle 1',
    'gfx light_r 1',
    'gfx light_radius 2.875',
    'gfx light_tiberium 1',
    'gfx m3_ambient 3',
    'gfx m3_ao 0.51786',
    'gfx m3_env 1',
    'gfx m3_normal 0.76786',
    'gfx m3_roughmin 0.11607',
    'gfx m3_specamb 0.76786',
    'gfx m3_sun 0.69643003',
    'gfx rain_amount 0.76786',
    'gfx rain_dark 0.49107',
    'gfx rain_drop_force 0',
    'gfx rain_drops 0',
    'gfx rain_fog 0.071429998',
    'gfx rain_fog_detail 1',
    'gfx rain_fog_height 1.0803601',
    'gfx rain_fog_scale 21.160709',
    'gfx rain_fog_speed 1',
    'gfx rain_fx 0',
    'gfx rain_gloss 26.5',
    'gfx rain_haze 0',
    'gfx rain_height 6.9910698',
    'gfx rain_len 0.44999999',
    'gfx rain_overcast 0.38393',
    'gfx rain_puddle 0.375',
    'gfx rain_puddle_edge 0.26049',
    'gfx rain_puddle_flat 0.80355',
    'gfx rain_puddle_mirror 0.24107',
    'gfx rain_puddle_size 34.910709',
    'gfx rain_puddle_ssr 0.625',
    'gfx rain_relief 0.0625',
    'gfx rain_relief_body 0.080360003',
    'gfx rain_ripple 0.53570998',
    'gfx rain_ripple_density 0.071429998',
    'gfx rain_ripple_rate 1.2',
    'gfx rain_ripple_scale 6.9910698',
    'gfx rain_ripple_shade 0',
    'gfx rain_sea 0.69643003',
    'gfx rain_sheen 0.071429998',
    'gfx rain_slant 0.25',
    'gfx rain_sound 0.44999999',
    'gfx rain_spec 0.48214',
    'gfx rain_speed 17.25',
    'gfx rain_splash 0',
    'gfx rain_splash_rate 0.5',
    'gfx rain_splash_size 0.050000001',
    'gfx rain_streaks 0.84820998',
    'gfx rain_water_ripple 0',
    'gfx rain_wet 0.74106997',
    'gfx rain_width 0.02',
    'gfx rain_wind 154.28572',
    'gfx sand_density 0.20536',
    'gfx sand_fx 1',
    'gfx sand_height 0.80000001',
    'gfx sand_light 5.5',
    'gfx sand_saturation 0.75',
    'gfx sand_scale 10',
    'gfx sand_speed 1.8571399',
    'gfx sand_wind 80.35714',
    'gfx saturation 1.125',
    'gfx shadow_b 0.6875',
    'gfx shadow_bias 0.03754',
    'gfx shadow_g 0.57999998',
    'gfx shadow_noff 1.5',
    'gfx shadow_on 1',
    'gfx shadow_ox 11.57143',
    'gfx shadow_oz 7.7142801',
    'gfx shadow_pen 0.079999998',
    'gfx shadow_r 0.49107',
    'gfx shadow_res 4096',
    'gfx shadow_soft 0.82143003',
    'gfx shadow_span 1.41429',
    'gfx shadow_strength 0.625',
    'gfx shadow_terrain 1',
    'gfx shadow_terrain_rise 0',
    'gfx snow_brightness 0.80356997',
    'gfx snow_contrast 1',
    'gfx snow_saturation 0.83036',
    'gfx spr_shadow_scale 1.7',
    'gfx ssao_bias 0.01384',
    'gfx ssao_foliage 0.79464',
    'gfx ssao_ground 0.5',
    'gfx ssao_intensity 0.60714',
    'gfx ssao_object 1',
    'gfx ssao_on 1',
    'gfx ssao_power 1.3125',
    'gfx ssao_radius 0.2067',
    'gfx ssao_samples 18',
    'gfx st_ao 1.625',
    'gfx st_bright 0.875',
    'gfx st_contrast 1',
    'gfx st_gamma 0.92500001',
    'gfx st_glow 0.75',
    'gfx st_sat 0.92856997',
    'gfx st_shade 1.32143',
    'gfx st_sun 0.89286',
    'gfx sun_az 70.714287',
    'gfx sun_el 30.85714',
    'gfx sun_floor 0.51786',
    'gfx sun_lambert 1',
    'gfx temperature 0.08929',
    'gfx terrain_brightness 1.1428601',
    'gfx terrain_contrast 1.03571',
    'gfx terrain_normals 1',
    'gfx terrain_shadow_strength 0.60714',
    'gfx tint 0',
    'gfx tone_knee 0.85000002',
    'gfx tonemap 1',
    'gfx unified_lighting 0',
    'gfx vh_ao 1.42857',
    'gfx vh_bright 1',
    'gfx vh_contrast 1',
    'gfx vh_gamma 0.75625002',
    'gfx vh_glow 0.83036',
    'gfx vh_sat 0.66070998',
    'gfx vh_shade 0.98214',
    'gfx vh_sun 0.64286',
    'gfx vlight_cone 1',
    'gfx vlight_glow 1',
    'gfx vlight_head 1',
    'gfx vlight_on 1',
    'gfx vlight_range 1',
    'gfx vlight_shadows 1',
    'gfx vlight_tail 1',
    'gfx vspec_gloss 1.22098',
    'gfx vspec_on 1',
    'gfx vspec_sky 2.07143',
    'gfx vspec_sun 2.17857',
    'gfx water_bump 0.26786',
    'gfx water_current 67.5',
    'gfx water_dark 0.29464',
    'gfx water_deep 0.39286',
    'gfx water_edge 0.53570998',
    'gfx water_fade 0.28571001',
    'gfx water_fade_width 0.85268003',
    'gfx water_flow 0.34999999',
    'gfx water_flow_rate 0.25',
    'gfx water_foam 0',
    'gfx water_foam_cross 0.13393',
    'gfx water_foam_width 0.35714',
    'gfx water_fx 1',
    'gfx water_natural 0.84820998',
    'gfx water_pan 0.34999999',
    'gfx water_refract 0.02036',
    'gfx water_river_speed 1.5',
    'gfx water_river_width 3',
    'gfx water_shallow 0.47321001',
    'gfx water_spec 1.0446399',
    'gfx water_wake 0.03571',
    'gfx water_wake_bump 1.2',
    'gfx water_wake_ring 0.12',
    'gfx water_wake_sim 1',
    'gfx water_wake_speed 0.02',
    'gfx water_wake_width 1.28214',
    'gfx water_wave_len 0.5',
    'gfx water_wave_speed 0.34999999',
]

def write(path, lines):
    if path.endswith('.txt'):
        lines = REFERENCE_LIGHT + list(lines)
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def efxm_visible(path):
    """Per frame, the names of the parts the file draws (visible, alpha above nothing)."""
    b = open(path, 'rb').read()
    assert b[:4] == b'EFXM'
    ver, frames, ntex, nparts = struct.unpack_from('<4I', b, 4)
    o = 20
    for _ in range(ntex):
        w, h = struct.unpack_from('<II', b, o + 8)
        o += 16 + w * h * 4
    names = []
    for _ in range(nparts):
        name = b[o:o + 16].split(b'\0')[0].decode()
        ntris = struct.unpack_from('<I', b, o + 28)[0]
        names.append(name)
        o += 32 + ntris * 3 * 24
    vis = []
    for f in range(frames):
        on = []
        for p in range(nparts):
            drawn = b[o + 48]
            a = struct.unpack_from('<f', b, o + 52)[0]
            if drawn and a > 0.002:
                on.append(names[p])
            o += 68
        vis.append(on)
    return frames, names, vis


def mission(d, tag):
    scen, _, _, _, silo = RUNS[tag]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    if not silo:
        return
    house, (x, y) = silo
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().splitlines()
    st = next(i for i, l in enumerate(L) if l.strip() == '[STRUCTURES]')
    # (and a Construction Yard beside it: a mission with no yard of the player's own has an
    # empty sidebar, and the missile is fired from the sidebar)
    L.insert(st + 1, f'90={house},MSLO,256,{y * 128 + x},0,None,1,0')
    L.insert(st + 2, f'91={house},FACT,256,{y * 128 + x + 3},0,None,1,0')
    # (and the player's tech level raised: at 10 or below the 1996 silo's missile goes up and
    # never comes down, the campaign's "artificial nukes", building.cpp LAUNCH_UP)
    h = next(i for i, l in enumerate(L) if l.strip() == f'[{house}]')
    e = h + 1
    while e < len(L) and not L[e].startswith('['):
        e += 1
    L[h + 1:e] = [l for l in L[h + 1:e] if not l.startswith('TechLevel=')]
    L.insert(h + 1, 'TechLevel=15')
    write(path, L)


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, (scen, _, (cx, cy), (bx, by), silo) in RUNS.items():
        mission(d, tag)
        v = ['tick 2', 'reveal 1', 'tick 20', f'cam {cx} {cy}', 'dist 2600', 'tick 1',
             f'shot {d}/{tag}_before.png']
        if silo:
            v += ['tick 90', 'superrecharge', 'tick 2', 'sbclickitem SW_Nuke', 'tick 1', f'actcell {bx} {by}']
        else:
            v += [f'efxanim 60 {bx + 0.5} {by + 0.5}']
        # (the missile flies first: up to 400 ticks more for the launch)
        for k in range(FRAMES + 4 + (400 if silo else 0)):
            v += ['tick 1', 'frames 1', 'atomdump', 'efxdump']
            if not silo and k in SHOTS:
                v.append(f'shot {d}/{tag}_f{k:03d}.png')
        write(os.path.join(d, tag + '.txt'), v + ['quit'])


def kv(line):
    out = {}
    for part in line.strip().split('|'):
        if '=' in part:
            k, v = part.split('=', 1)
            out[k] = v
    return out


def mean_luma(path):
    try:
        from PIL import Image
    except ImportError:
        return None
    im = Image.open(path).convert('L')
    px = im.getdata()
    return sum(px) / float(len(px))


def diff_frac(a, b):
    from PIL import Image, ImageChops
    d = ImageChops.difference(Image.open(a).convert('RGB'), Image.open(b).convert('RGB')).convert('L')
    px = d.getdata()
    return sum(1 for p in px if p > 24) / float(len(px))


def check_run(d, tag, vis, names):
    scen, theater, _, _, silo = RUNS[tag]
    label = f'{tag} ({scen}, {theater}{", the silo launch" if silo else ""})'
    path = os.path.join(d, tag + '.log')
    if not os.path.isfile(path):
        return f'{label}: no log'
    text = open(path, errors='replace').read()
    lines = text.splitlines()
    ld = [kv(l) for l in lines if l.startswith('RA3DATOM|loaded|')]
    if not ld or (ld[0].get('parts'), ld[0].get('tris'), ld[0].get('frames')) != ('13', '27636', '111'):
        return f'{label}: the blast\'s file did not load as 13 parts, 27,636 triangles, 111 frames ({ld[:1]})'
    armed = [kv(l) for l in lines if l.startswith('RA3DATOM|armed|')]
    if len(armed) != 1:
        return f'{label}: {len(armed)} blasts armed, not one'
    draws = {}
    for l in lines:
        if l.startswith('RA3DATOM|draw|'):
            k = kv(l)
            draws[int(k['frame'])] = (k.get('on') or '').split(',') if k.get('on') else []
    missing = [f for f in range(FRAMES) if f not in draws]
    if missing:
        return f'{label}: frames {missing[:8]}{"..." if len(missing) > 8 else ""} never drawn ({len(draws)} drawn)'
    wrong = [f for f in range(FRAMES) if sorted(draws[f]) != sorted(vis[f])]
    if wrong:
        f = wrong[0]
        return f'{label}: frame {f} drew {draws[f]}, the file has {vis[f]} on it'
    stages = [int(m.group(1)) for m in re.finditer(r'EFXDUMP\|ANIM\|ATOMSFX\|[^\n]*\|stage=(\d+)/', text)]
    if not stages:
        return f'{label}: the anim was never in the dump'
    if 'EFXNUKE|' in text:
        return f'{label}: Tiberian Dawn\'s mushroom (EFXNUKE) drew'
    sp = [kv(l) for l in lines if l.startswith('EFXSPAWN|ATOMSFX|')]
    if not sp or sp[-1].get('recipe') != 'ANIM_ATOM_BLAST_RA':
        return f'{label}: the anim fired {sp[-1].get("recipe") if sp else "no recipe"}, not ATOM_BLAST_RA'
    if int(sp[-1].get('particles', '0')) <= 0:
        return f'{label}: its own recipe spawned no particles'
    # the flash by frame: each atomdump after a tick, the blast's frame on the line after it
    flash = {}
    cur = None
    for l in lines:
        if l.startswith('RA3DATOM|dump|'):
            cur = float(kv(l)['flash'])
        elif l.startswith('RA3DATOM|blast|') and cur is not None:
            flash[int(kv(l)['frame'])] = cur
            cur = None
    want = {0: (0.0, 0.0), 1: (0.84, 0.86), 4: (0.45, 0.52), 8: (0.0, 0.0), 30: (0.0, 0.0)}
    for f, (lo, hi) in want.items():
        if f not in flash or not lo <= flash[f] <= hi:
            return f'{label}: the flash at frame {f} is {flash.get(f)}, wanted {lo} to {hi}'
    shakes = [int(kv(l)['frame']) for l in lines if l.startswith('RA3DATOM|shake|')]
    if shakes != [19]:
        return f'{label}: the ground shook at {shakes}, not once at frame 19'
    msg = f'{label}: drawn frames 0 to 110 part for part, the anim living to stage {max(stages)}, ' \
          f'{sp[-1]["particles"]} particles of its own, flash 0.85 at frame 1, shake at 19'
    if not silo:
        b, f1, f30 = (os.path.join(d, f'{tag}_{n}.png') for n in ('before', 'f001', 'f030'))
        if not all(os.path.isfile(p) for p in (b, f1, f30)):
            return f'{label}: a shot is missing'
        lb, l1 = mean_luma(b), mean_luma(f1)
        if lb is not None:
            if l1 < lb + 60:
                return f'{label}: the frame 1 shot is not flashed (mean {l1:.0f} against {lb:.0f} before)'
            df = diff_frac(b, f30)
            if df < 0.02:
                return f'{label}: the frame 30 shot is the picture before the blast ({df:.3f} of it changed)'
            msg += f'; flash lifts the picture from {lb:.0f} to {l1:.0f}, {df:.0%} of it changed at frame 30'
    return None, msg


def check(d):
    efxm = os.path.join(PLAY, 'ra3d/efx/ATOMSFX.efxm')
    if not os.path.isfile(efxm):
        print('0 G353 Red Alert 3D\'s atom bomb: no ra3d/efx/ATOMSFX.efxm staged (tools/ra3d/stage-structures.py)')
        return
    frames, names, vis = efxm_visible(efxm)
    msgs = []
    for tag in RUNS:
        r = check_run(d, tag, vis, names)
        if isinstance(r, str):
            print(f'0 G353 Red Alert 3D\'s atom bomb: {r}')
            return
        msgs.append(r[1])
    print('1 G353 Red Alert 3D\'s atom bomb draws its own blast under Enhanced: ' + '; '.join(msgs))


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        sys.exit(__doc__)
    (script if sys.argv[1] == 'script' else check)(sys.argv[2])
