#!/usr/bin/env python3
"""Hidden runtime checks for Enhanced Temperate and Desert ground patches.
Checks visible dry-ground pixels, controls, repeatability, the HUD and Classic/DOS.
Requires a built playable folder, Pillow and NumPy.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--rundir', type=Path, default=ROOT/'playable')
p.add_argument('--output', type=Path, default=ROOT/'tmp/ground-patches-gate')
a = p.parse_args()
run, out = a.rundir.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
cfg = run/'opencnc3d-fx.cfg'
# A fresh installation has no tuning file. Absence is also player state.
before = cfg.read_bytes() if cfg.exists() else None

def shot(name):
    return f'shot {out/(name+".png")}\nmottledump\n'

def render(name, body, scenario):
    script = out/(name+'.txt')
    script.write_text(body+'quit\n')
    targets = re.findall(r'^shot (.+)$', body, re.M)
    for target in targets:
        Path(target).unlink(missing_ok=True)
    cmd = [str(run/'cnc_eyes'), '--scen', scenario, '--pack', scenario+'.pack',
           '--cameos', 'cameos.pack', '--dospack', 'dossidebar.pack', '--dosinf', 'dosinfantry.pack',
           '--dylib', 'TiberianDawn.dylib', '--dir', 'missions/', '--content', 'content/',
           '--enhanced', '--nosound', '--noshroud', '--w', '1000', '--h', '650', '--script', str(script)]
    r = subprocess.run(cmd, cwd=run, env={**os.environ,'OPENCNC3D_BACKGROUND':'1'},
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    log = r.stdout.decode(errors='replace')
    (out/(name+'.log')).write_text(log)
    assert r.returncode == 0 and re.search(r'^SCRIPT\|end .+: \d+ lines, 0 failures$',log,re.M), log[-4000:]
    assert not re.search(r'FX\|(?:shader|link|rt)\|.*(?:fail|incomplete)',log,re.I)
    for target in targets:
        assert Path(target).is_file(), target
    return log

def pixels(name):
    return np.asarray(Image.open(out/(name+'.png')).convert('RGB')).astype(int)

def dry(name):
    # Both pinned views have dry land here. Excludes animated water, top credits
    # and the sidebar. A nonzero whole-screen delta alone is not patch evidence.
    return pixels(name)[24:-24,20:300] if name.startswith('temperate') else pixels(name)[24:-24,300:810]

setup = ('tick 20\ngfx show_stats 0\ngfx ss_scale 1\ngfx cloud_on 0\n'
         'gfx fog_fx 0\ngfx sand_fx 0\ngfx dfog_on 0\ngfx bloom_on 0\ngfx fol_on 0\n'
         'gfx rock_on 0\ngfx mottle_on 1\ngfx mottle_density 0.0625\n'
         'gfx mottle_strength 0.41071\ncam 30 40\n')
for name, scenario, bank, materials in [('desert','SCB37EA','mottle_desert.pack',5),
                                        ('temperate','SCG04EA','mottle.pack',15)]:
    view = setup
    log = render(name, view+shot(name+'-on')+'gfx mottle_on 0\n'+shot(name+'-off')+
                 'gfx mottle_on 1\ngfx mottle_strength 0\n'+shot(name+'-strength-zero')+
                 'gfx mottle_strength 0.41071\ngfx mottle_density 0\n'+shot(name+'-density-zero'),scenario)
    assert re.search(r'bank='+bank+r'\|materials='+str(materials)+r'\|available=1\|drawn=[1-9]\d*\|',log)
    count = int((np.abs(dry(name+'-on')-dry(name+'-off')).sum(-1)>12).sum())
    assert count > 20000, (name,count)
    for zero in ['strength-zero','density-zero']:
        assert np.array_equal(dry(name+'-off'),dry(name+'-'+zero)), zero
    assert np.array_equal(pixels(name+'-on')[24:-24,870:980],pixels(name+'-off')[24:-24,870:980])
    render(name+'-repeat',view+shot(name+'-repeat'),scenario)
    assert np.array_equal(dry(name+'-on'),dry(name+'-repeat')), name+' repeat'
    print(f'PASS: {name} patches change {count} dry-ground pixels; zero controls, HUD and repeat match',flush=True)

for name, mode in [('classic','gfxmode 0\n'),('dos','gfx texset 1\n')]:
    log = render(name,setup+mode+shot(name+'-on')+'gfx mottle_on 0\n'+shot(name+'-off'),'SCB37EA')
    assert np.array_equal(dry(name+'-on'),dry(name+'-off'))
    assert re.search(r'available=0\|drawn=0\|',log)
    print(f'PASS: {name} terrain has no ground patches',flush=True)
after = cfg.read_bytes() if cfg.exists() else None
assert after == before, 'player CFG changed'
print('PASS: player CFG preserved byte for byte' if before is not None
      else 'PASS: player CFG remains absent',flush=True)
