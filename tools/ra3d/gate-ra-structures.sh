#!/bin/sh
# Hidden runtime check: standing geometry through damage, layer suppression and snow fog, and
# every staged file's stages approved and within the stage budget.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
install=$(CDPATH= cd -- "$install" && pwd)
work=$(mktemp -d "/tmp/ra3d-structures.XXXXXX")
mission="$install/ra3d/missions/RASTRUCT.INI"
if [ -f "$mission" ]; then cp -p "$mission" "$work/previous.INI"; fi
echo "RA structure gate evidence: $work"
cleanup() {
    if [ -f "$work/previous.INI" ]; then cp -p "$work/previous.INI" "$mission"; else rm -f "$mission"; fi
    [ "${OPENCNC3D_KEEP_GATE_ARTIFACTS:-0}" = 1 ] || rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
cp "$root/tests/ra3d/RASTRUCT.INI" "$work/RASTRUCT.INI"
cp "$root/tests/ra3d/RASTRUCT.INI" "$mission"
# A turret's pose (EBLDPOSE) is recorded only for a building a frame actually draws, and
# every pass skips a building outside its view before drawing any of it. The SAM Site
# (79,80) is outside both of the other views, so the camera visits the turret row
# (Gun 70,80, SAM 79,80) for one frame after the order and returns; the next ebldump
# prints what that frame drew. Without the visit the SAM has no pose line at all.
cat > "$work/check.script" <<EOF
tick 5
gfxmode 2
reveal 1
cam 51 66
cursor 650 460
cursor3dstate 2
tick 1
shot $work/enhanced.png
cursor3ddump
ebldump
cam 58 80
lclickobj E1:Greece 0
expectsel 1
orderobj POWR:USSR 0
cam 78 80
shot $work/turrets.png
cam 58 80
tick 120
shot $work/under-fire.png
ebldump
gfxmode 0
tick 1
ebldump
quit
EOF
cd "$install"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib \
    --content ra3d/data --dir ra3d/missions/ --scen RASTRUCT \
    --pack ra3d/3d/SCG05EA.pack --dosdata ra3d/audio --nosound \
    --script "$work/check.script" > "$work/check.log" 2>&1
python3 - "$work/check.log" "$root/game/ebld_ra/manifest.json" <<'PY'
import json, re, sys
text = open(sys.argv[1]).read()
assert '0 failures' in text and 'refused:' not in text and 'FATAL:' not in text
rows = re.findall(r'^RASTRUCT\|([^\n]+)', text, re.M)
assert rows
models = [s for s in rows if s.endswith('|draw=model')]
assert len(models) == 66, models
assert any(s.startswith('FACF|') for s in models)
assert any(s.startswith('WEAF|') for s in models)
assert any(s.startswith('DOMF|') for s in models)
hurt = [s for s in models if s.startswith('POWR|id=26|')]
strengths = [int(re.search(r'\|str=(\d+)/', s)[1]) for s in hurt]
assert len(strengths) == 2 and 0 < strengths[1] < strengths[0] <= 200, hurt
assert any(s.startswith('KENN|id=32|') and '|str=200/400|' in s for s in models)
for code in ('TSLA', 'FTUR', 'AGUN', 'GUN', 'SAM'):
    assert any(s.startswith(code + '|') for s in models), code
assert all(s.endswith('|draw=original') for s in rows[-33:]), 'Classic claimed a model'
layers = re.findall(r'RASPRITES\|modelroots=(\d+)\|modelsubs=(\d+)\|fallbackbuildings=(\d+)', text)
assert layers and all(int(root) > 0 and int(sub) > 0 and int(fallback) == 0
                      for root, sub, fallback in layers), layers
assert '|RED|late textures=' in text
assert 'RA3D|gravel|OpenCNC3D g3s|cells=' in text
assert 'ECUR|attached|23 of 23 remastered cursor models' in text
assert 'CURSOR3DECUR|have=23|on=1|drawn=1|name=ECUR01|' in text
assert 'EBLDWAVE|BARR|waves=1|' in text and 'EBLDWAVE|TENT|waves=1|' in text
manifest = json.load(open(sys.argv[2]))
assert len(manifest['models']) == 29
# Each file is the approved standing model alone, or the model with the stages the lab approved
# under the destruction recipe (tools/ra3d/bake-structures.py), held to the stage budget.
for code, info in manifest['models'].items():
    assert info['sets']['INTACT']['triangles'] <= 10000, code
    assert any('mask' in t for t in info['textures']), code
    if set(info['sets']) == {'INTACT'}:
        assert info['emitters'] == [] and info['params']['scorch_tex'] == -1, code
    else:
        assert set(info['sets']) == {'INTACT', 'CUT', 'DESTROYED'}, code
        assert info.get('stages') == 'approved', f'{code}: stages in review in a build'
        d = info['sets']['DESTROYED']
        assert d['pieces'] <= 150 and d['triangles'] <= 30000, f'{code}: over the stage budget {d}'
        assert info['params']['scorch_tex'] >= 0, code
for code in ('GUN', 'SAM', 'AGUN'):
    assert any(m['kind'] == 'turret' for m in manifest['models'][code]['moving']), code
for code in ('GUN', 'SAM'):
    pose = re.search(r'EBLDPOSE\|' + code + r'\|[^\n]+\|kind=turret\|deg=(-?[\d.]+)', text)
    assert pose and abs(float(pose[1]) + 90) < 0.01, f'{code}: engine facing was not applied'
full = sorted(k for k, v in manifest['models'].items() if set(v['sets']) != {'INTACT'})
print('RA structures: PASS: 33 standing models, real infantry damage keeps its model, root and subobject sprites suppressed, 29 approved exports'
      + (f' ({len(full)} with their stages: {", ".join(full)})' if full else ' (standing models only)') + '; Classic original')
PY

# Snow uses the same approved gravel painting without temperate grass's red gain.
python3 - "$mission" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
p.write_text(p.read_text().replace('Theater=TEMPERATE', 'Theater=SNOW'))
PY
cat > "$work/snow.script" <<EOF
tick 5
gfxmode 2
reveal 1
cam 58 79
shot $work/snow.png
gfx dfog_on 0
shot $work/fog-off.png
gfx dfog_on 1
shot $work/fog-on.png
cam 44 79
shot $work/tesla.png
cam 80 76
shot $work/kennel.png
quit
EOF
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib \
    --content ra3d/data --dir ra3d/missions/ --scen RASTRUCT \
    --pack ra3d/3d/SCG01EA.pack --dosdata ra3d/audio --nosound \
    --script "$work/snow.script" > "$work/snow.log" 2>&1
python3 - "$work" <<'PY'
from pathlib import Path
from PIL import Image
import numpy as np
import sys
w = Path(sys.argv[1])
log = (w / 'snow.log').read_text()
assert '0 failures' in log and 'FX|distfog|on theater 3' in log
def pixels(name):
    return np.asarray(Image.open(w / name).convert('RGB')).astype(int)
snow = pixels('snow.png')
height, width = snow.shape[:2]
view = int(width * 0.8)
rgb = snow[:, :view]
pink = (rgb[:, :, 0] - rgb[:, :, 1] > 25) & (rgb[:, :, 2] - rgb[:, :, 1] > 10)
assert pink.mean() < 0.002, f'pink gravel region: {pink.mean():.3%}'
diff = np.abs(pixels('fog-on.png') - pixels('fog-off.png')).max(axis=2)
assert (diff[:, :view] > 1).sum() > 10000, 'snow fog did not change the world'
assert diff[:height // 3, :view].mean() > 0.5, 'no distant haze'
assert diff[height * 3 // 4:, :view].max() == 0, 'fog reached the near ground'
assert diff[:, int(width * 0.83):].max() == 0, 'fog changed the sidebar'
print('RA snow: PASS: gravel has no pink cast; fog changes distant ground and leaves near ground/sidebar clear')
PY
