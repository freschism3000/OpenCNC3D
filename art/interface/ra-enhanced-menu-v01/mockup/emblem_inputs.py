#!/usr/bin/env python3
"""Rebuild the reference images handed to the emblem concept step (emblems/*/c1*/1_..6_).
The 1996 pictures come from data/radata; the barracks flag from the Tent Barracks finish
sheet (build-native/remaster-ra/shots/finish/TENT.jpg, written by the Red Alert structure
tools); the style reference is a crop of the Tiberian Dawn Enhanced main menu's turning
emblems (TD_MENU_SHOT, a 1280x720 capture of that menu). A missing source is reported and
its image left as committed."""
import io, os, shutil, subprocess, tempfile
from PIL import Image
import paths
from ramix import RaMix
from mixshp import Shape

hires = RaMix(os.path.join(paths.RADATA, 'HIRES.MIX'))
pal = RaMix(os.path.join(paths.RADATA, 'LOCAL.MIX')).read('TEMPERAT.PAL')
P = [tuple(min(255, c * 4 + c // 16) for c in pal[i * 3:i * 3 + 3]) for i in range(256)]
def radar(n):
    s = Shape(hires.read(n + '.SHP')); im = Image.new('RGB', (s.width, s.height)); im.putdata([P[c] for c in s.frame(0)])
    return im.resize((s.width * 3, s.height * 3), Image.NEAREST)
def movie(name, frames):
    mv = os.path.join(paths.RADATA, 'movies', name + '.VQA')
    if not shutil.which('ffmpeg') or not os.path.exists(mv): return None
    out = Image.new('RGB', (640 * len(frames), 312))
    with tempfile.TemporaryDirectory() as t:
        for k, n in enumerate(frames):
            f = os.path.join(t, '%d.png' % n)
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', mv, '-vf', 'select=eq(n\\,%d)' % n, '-frames:v', '1', f], check=False)
            if os.path.exists(f): out.paste(Image.open(f).convert('RGB').resize((640, 312), Image.NEAREST), (640 * k, 0))
    return out
tent = os.path.join(paths.LAB_RA, 'shots', 'finish', 'TENT.jpg')
td = os.environ.get('TD_MENU_SHOT', os.path.join(paths.ROOT, 'tools', 'art', 'remaster', 'viewer', 'cnc-interface', 'menus', 'screenshots', 'enhanced-menu-v08-main.png'))
for side, rn, mv, fr, paper, crop in (('allied', 'NATORADR', 'ALLYMORF', (49, 69), 'ALIPAPER', (300, 1300, 560, 1560)),
                                      ('soviet', 'USSRRADR', 'SOVTSTAR', (94, 104), 'SOVPAPER', (300, 1890, 560, 2150))):
    for var in (('c1', 'c1g') if side == 'allied' else ('c1',)):
        d = os.path.join(paths.EMBLEMS, side, var)
        radar(rn).save(os.path.join(d, '1_ref_1996_radar_logo.png'))
        m = movie(mv, fr)
        if m: m.save(os.path.join(d, '2_ref_1996_logo_movie.png'))
        Image.open(io.BytesIO(hires.read(paper + '.PCX'))).convert('RGB').save(os.path.join(d, '3_ref_1996_briefing_paper.png'))
        if os.path.exists(tent): Image.open(tent).convert('RGB').crop(crop).resize((520, 520), Image.LANCZOS).save(os.path.join(d, '4_ref_barracks_flag.png'))
        else: print('no', tent)
        if os.path.exists(td):
            t = Image.open(td).convert('RGB'); s = Image.new('RGB', (760, 380), (30, 30, 30))
            s.paste(t.crop((80, 490, 270, 680)).resize((380, 380), Image.NEAREST), (0, 0)); s.paste(t.crop((1010, 490, 1200, 680)).resize((380, 380), Image.NEAREST), (380, 0))
            s.save(os.path.join(d, '5_style_td_menu_emblems.png'))
        else: print('no', td)
m = movie('ALLYMORF', (14, 24))
if m: m.save(os.path.join(paths.EMBLEMS, 'allied', 'c1g', '6_ref_1996_logo_movie_start.png'))
print('emblem inputs rebuilt')
