#!/usr/bin/env python3
"""Gather the mockups' inputs into the work folder (build-native/ra-enhanced-menu-v01):
Red Alert's own Win95 dialog pieces, palettes, pictures, maps and briefings out of the 1996
archives in data/radata; one still of the prologue; the title with its lettering lifted
off, and the lettering as a wordmark; the emblem cut-outs from the concept sheets."""
import io, os, re, shutil, subprocess
import numpy as np
from PIL import Image, ImageDraw
import paths
from ramix import RaMix
from mixshp import Shape, ShapeBlock

W = paths.WORK
os.makedirs(os.path.join(W, 'HIRES'), exist_ok=True); os.makedirs(os.path.join(W, 'intro'), exist_ok=True)
mix = lambda n: RaMix(os.path.join(paths.RADATA, n))
hires, general, conquer, local = mix('HIRES.MIX'), mix('GENERAL.MIX'), mix('CONQUER.MIX'), mix('LOCAL.MIX')

# the dialog pieces, as raw palette indices (chassis.py colours them per palette)
for n in ('DD-BKGND', 'DD-EDGE', 'DD-LEFT', 'DD-RIGHT', 'DD-TOP', 'DD-BOTM', 'DD-CRNR', 'BTN-UP', 'BTN-DN', 'BTN-PL', 'BTN-ST'):
    s = Shape(hires.read(n + '.SHP'))
    with open(os.path.join(W, 'HIRES', n + '.idx'), 'wb') as f:
        for i in range(s.frames): f.write(s.frame(i))
d = open(os.path.join(W, 'HIRES', 'DD-BKGND.idx'), 'rb').read()
q = [np.frombuffer(d[i * 312 * 192:(i + 1) * 312 * 192], np.uint8).reshape(192, 312) for i in range(4)]
np.save(os.path.join(W, 'bkgnd_idx.npy'), np.vstack([np.hstack([q[0], q[1]]), np.hstack([q[2], q[3]])]))

open(os.path.join(W, 'TITLE.PCX'), 'wb').write(hires.read('TITLE.PCX'))
for n in ('ALIPAPER', 'SOVPAPER'):
    Image.open(io.BytesIO(hires.read(n + '.PCX'))).convert('RGB').save(os.path.join(W, 'HIRES_%s.png' % n))

pal = local.read('TEMPERAT.PAL'); P = [tuple(min(255, c * 4 + c // 16) for c in pal[i * 3:i * 3 + 3]) for i in range(256)]
px, hd = ShapeBlock(hires.read('MOUSE.SHP')).frame(0)
m = Image.new('RGBA', (hd['width'], hd['height']))
m.putdata([(0, 0, 0, 0) if c == 0 else P[c] + (255,) for c in px]); m.save(os.path.join(W, 'MOUSE_HIRES_00.png'))

# the multiplayer maps and two campaign briefings
for i in range(1, 25):
    r = general.read('SCM%02dEA.INI' % i)
    if r: open(os.path.join(W, 'SCM%02dEA.INI' % i), 'wb').write(r)
for n in ('SCG01EA', 'SCU01EA'):
    open(os.path.join(W, n + '.INI'), 'wb').write(general.read(n + '.INI'))

# a still of the prologue (frame 2220: the C&C logo in the corridor)
mv = os.path.join(paths.RADATA, 'movies', 'PROLOG.VQA'); out = os.path.join(W, 'intro', 'PROLOG_038.png')
if shutil.which('ffmpeg') and os.path.exists(mv):
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', mv, '-vf', r'select=eq(n\,2220)', '-frames:v', '1', out], check=False)
if not os.path.exists(out): Image.new('RGB', (320, 156), (0, 0, 0)).save(out)

ec = os.path.join(paths.PLAY, 'ecur', 'ecur.bin')
if os.path.exists(ec): shutil.copy(ec, os.path.join(W, 'ecur.bin'))

# the title without its lettering, and the lettering as a wordmark at both densities
import cleanplate
art = os.path.join(paths.ROOT, 'menu', 'art')
cleanplate.clean(os.path.join(art, 'ra3d-title-win95.png'), 1, os.path.join(W, 'ra_title_backdrop_clean.png'))
cleanplate.clean(os.path.join(art, 'ra3d-title.png'), 0.5, os.path.join(W, 'ra_title_backdrop_clean_lo.png'))
for src, k, name in ((os.path.join(art, 'ra3d-title-win95.png'), 1, 'ra_wordmark_hi.png'), (os.path.join(art, 'ra3d-title.png'), 0.5, 'ra_wordmark_lo.png')):
    im = np.array(Image.open(src).convert('RGB')); H, Wd, _ = im.shape
    mask = Image.new('L', (Wd, H), 0); ImageDraw.Draw(mask).polygon([(x * k, y * k) for x, y in cleanplate.POLY], fill=255); mk = np.array(mask) > 0
    rgba = np.dstack([im, np.where(mk, 255, 0).astype(np.uint8)]); ys, xs = np.where(mk)
    Image.fromarray(rgba, 'RGBA').crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)).save(os.path.join(W, name))

# the emblems: each concept sheet's front view, keyed off its black ground
for side, path in (('Allied', 'allied/c1'), ('Soviet', 'soviet/c1'), ('AlliedGlobe', 'allied/c1g')):
    c = Image.open(os.path.join(paths.EMBLEMS, path, 'concept.png')).convert('RGB'); w, h = c.size
    a = np.array(c.crop((0, 0, w // 2, h // 2))).astype(np.float32); al = np.clip((a.max(-1) - 10) / 25, 0, 1)
    e = Image.fromarray(np.dstack([a, al * 255]).astype(np.uint8), 'RGBA'); e = e.crop(e.getbbox())
    s = max(e.size); sq = Image.new('RGBA', (s, s), (0, 0, 0, 0)); sq.alpha_composite(e, ((s - e.width) // 2, (s - e.height) // 2))
    sq.save(os.path.join(W, 'emblem_%s.png' % side))
print('inputs in', W)
