#!/bin/sh
# Red Alert's Enhanced menus, walked the way a player walks them: the main menu in Red
# Alert's own kit, Start New Game, the Enhanced side choice (Allies, Normal, Start), the
# first mission, its pause dialog drawn blue, Save Mission opening Red Alert's own slot
# list and Back returning to the pause page, Resume Mission, and the main menu again after
# the mission, the same picture as before it; then Skirmish through its lobby, Load
# Mission from the main menu, Multiplayer opening the room screens, a room started from
# its host's lobby screen with a joiner, and a lost skirmish ending on the debrief. Hidden
# throughout.
#
#   tools/ra3d/gate-ra-enhanced-menu.sh [PLAY_FOLDER]
#
# Without Red Alert's Enhanced kit staged (ra3d/enhanced-menu.pack) it says so and passes.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
install=$(CDPATH= cd -- "$install" && pwd)
if [ ! -f "$install/ra3d/enhanced-menu.pack" ] || [ ! -f "$install/ra3d/RedAlert.dylib" ]; then
    echo "SKIP: no Red Alert module with its Enhanced kit is staged in $install"
    exit 0
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-enhmenu.XXXXXX")
keep=${RA_ENHMENU_KEEP:-}
trap '[ -n "$keep" ] || rm -rf "$work"' EXIT HUP INT TERM
# the shipped Enhanced look, named on the command line, which is what makes an automated
# Red Alert run start in Enhanced rather than Classic
cp "$root/docs/tuning/opencnc3d-fx.cfg" "$work/enhanced.cfg"
cat > "$work/walk.script" <<EOF
tick 2
options
shot $work/pause.png
emclick Save Mission
shot $work/slots.png
emclick Back
emclick Resume Mission
tick 2
quit
EOF
cd "$install"
OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- \
    --gfx "$work/enhanced.cfg" --harness 1 --rounds 1 --no-movies --nosound \
    --script "$work/walk.script" --shotdir "$work" > "$work/run.log" 2>&1 || true
fail() { echo "FAIL: $1"; [ -n "$keep" ] || tail -20 "$work/run.log"; exit 1; }
grep -Fq 'ENMENU|loaded|red-alert-v01' "$work/run.log" || fail "Red Alert's Enhanced kit was not loaded"
grep -Fq 'RASIDE|auto|side=0|difficulty=1' "$work/run.log" || fail "the Enhanced side choice was not shown"
grep -Fq 'RASIDE|start|side=Allies|difficulty=1' "$work/run.log" || fail "the side choice's Start did not start the Allied campaign"
grep -Fq 'RA3D|start|SCG01EA|original mission' "$work/run.log" || fail "Allied 1 did not start"
grep -Fq 'OPTIONS|script|open' "$work/run.log" || fail "the pause dialog did not open"
grep -Fq 'OPTIONS|script|emclick|Save Mission|act=0|page=9|' "$work/run.log" \
    || fail "Save Mission did not open the slot list"
grep -Fq 'OPTIONS|script|emclick|Back|act=0|page=0|' "$work/run.log" || fail "the slot list's Back did not come back to the pause page"
grep -Fq 'OPTIONS|script|emclick|Resume Mission|act=1|' "$work/run.log" || fail "Resume Mission did not close the dialog"
grep -Fq 'HARNESS|end|1 round(s), 0 failure(s)' "$work/run.log" || fail "the harness did not end cleanly (the menu after the mission differs, or a step failed)"
for f in h_menu0.png h_menu1.png h_side1.png pause.png slots.png; do
    test -s "$work/$f" || fail "$f was not written"
done
python3 - "$work" <<'PY' || fail "the pictures are not Red Alert's Enhanced screens"
import sys
from PIL import Image
w = sys.argv[1]
def frac(img, box, test):
    im = Image.open(img).convert('RGB').crop(box)
    px = [im.getpixel((x, y)) for y in range(0, im.height, 2) for x in range(0, im.width, 2)]
    return sum(1 for p in px if test(p)) / len(px)
menu = Image.open(w + '/h_menu0.png'); W, H = menu.size
red = lambda p: p[0] > 60 and p[0] > 2 * p[1] and p[0] > 2 * p[2]
blue = lambda p: p[2] > 40 and p[2] > p[0] + 15
gold = lambda p: p[0] > 150 and p[1] > 90 and p[2] < 90
# the main menu: a red panel in the middle; the side box: red, with both emblems' gold and red
mid = (W * 3 // 8, H // 3, W * 5 // 8, H * 2 // 3)
m, s = frac(w + '/h_menu0.png', mid, red), frac(w + '/h_side1.png', mid, red)
g = frac(w + '/h_side1.png', (W // 4, H // 4, W * 3 // 4, H // 2), gold)
# in the mission the dialog is blue, never red (the slot list's well is mostly dark field)
p, n = frac(w + '/pause.png', mid, blue), frac(w + '/slots.png', mid, blue)
pr, nr = frac(w + '/pause.png', mid, red), frac(w + '/slots.png', mid, red)
print('RAENH|pictures|menu-red=%.2f|side-red=%.2f|side-gold=%.3f|pause-blue=%.2f|slots-blue=%.2f' % (m, s, g, p, n))
assert m > 0.3 and s > 0.1 and g > 0.005 and p > 0.3 and n > 0.03 and pr < 0.01 and nr < 0.01
PY
# THE SKIRMISH: Skirmish pressed on the main menu, the lobby's own seats pressed (the map
# picker opened and closed, seat 4 blocked, mixed difficulty cells selected, then Play),
# and the match the lobby asked for is the one that starts: Red Alert's first multiplayer
# map, each active seat its own country and colour, with its selected difficulty.
OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- \
    --gfx "$work/enhanced.cfg" --lobbyplay 30 --no-movies --nosound --shotdir "$work" > "$work/skirmish.log" 2>&1 || true
grep -Fq 'LOBBY|click|Play|' "$work/skirmish.log" || fail "the lobby's Play was not pressed"
grep -Eq 'APP\|raskirmish\|start\|SCM01EA\|players=7\|tech=10\|credits=10000\|' "$work/skirmish.log" \
    || fail "the Red Alert skirmish did not report its seven active seats"
grep -Fq 'skirmish: 7 player(s), Red Alert,' "$work/skirmish.log" \
    || fail "the engine did not start the lobby's seven active seats"
for c in Greece USSR England Ukraine France Turkey Spain; do
    grep -Eq "APP\|raskirmish\|seat=[0-7]\|(person|computer)\|country=$c\|" "$work/skirmish.log" || fail "no seat played $c"
done
if grep -Eq '^(APP\|raskirmish\|seat=4\||skirmish: seat=4\|)' "$work/skirmish.log"; then
    fail "the blocked seat appeared in the active roster"
fi
python3 - "$work/skirmish.log" <<'PY' || fail "the selected per-seat difficulties did not reach the engine"
import re, sys
app, engine = {}, {}
for line in open(sys.argv[1]):
    if line.startswith('APP|lobby|player='):
        fields = dict(re.findall(r'(?:^|\|)([a-z_]+)=([^|\n]+)', line))
        app[int(fields['player'])] = fields
    elif line.startswith('skirmish: seat='):
        fields = dict(re.findall(r'(?:skirmish: |\|)([a-z_]+)=([^|\n]+)', line))
        engine[int(fields['seat'])] = fields
assert set(app) == set(range(8)), app
assert app[4]['mode'] == '3' and app[4]['difficulty'] == '-', app[4]
assert set(engine) == {0, 1, 2, 3, 5, 6, 7}, engine
for seat, level in {1: 'Expert', 2: 'Hard', 3: 'Easy', 5: 'Normal', 6: 'Normal', 7: 'Normal'}.items():
    assert app[seat]['mode'] == '1' and app[seat]['difficulty'] == level, app[seat]
    assert engine[seat]['difficulty'] == level, engine[seat]
print('RAENH|difficulty|Expert,Hard,Easy,blocked,Normal,Normal,Normal|APP=engine')
PY
grep -Fq 'GAMELOOP|end|ticks=30|' "$work/skirmish.log" || fail "the skirmish did not play its 30 ticks"
# LOAD MISSION from the main menu: a Red Alert save of Allied 1 made in Enhanced, the
# button pressed, the newest slot taken, the mission loaded at the frame it was saved on,
# and the menu after it the same picture as before.
mkdir -p "$work/save" "$work/load"
printf 'tick 120\nsave 0 menu load\nquit\n' > "$work/save.txt"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib --content ra3d/data --dir ra3d/missions/ \
    --dosdata ra3d/audio --nosound --w 640 --h 400 --scen SCG01EA --pack ra3d/3d/SCG01EA.pack \
    --gfx "$work/enhanced.cfg" --savedir "$work/save" --script "$work/save.txt" > "$work/save.log" 2>&1 || true
grep -Eq '^SAVE\|slot=0\|menu load\|.*\|frame=120\|scen=SCG01EA\|' "$work/save.log" || fail "the save for Load Mission was not written"
OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- \
    --gfx "$work/enhanced.cfg" --harness 60 --rounds 1 --harnessload --savedir "$work/save" \
    --no-movies --nosound --shotdir "$work/load" > "$work/load.log" 2>&1 || true
grep -Fq 'HARNESS|click|LOAD MISSION' "$work/load.log" || fail "Load Mission was not pressed"
grep -Fq 'APP|load|slot=0|scen=SCG01EA|pack=ra3d/3d/SCG01EA.pack|' "$work/load.log" || fail "Load Mission did not take Allied 1's slot"
grep -Eq '^LOAD\|slot=0\|frame=120\|scen=SCG01EA\|' "$work/load.log" || fail "the slot did not load at frame 120"
grep -Fq 'HARNESS|end|1 round(s), 0 failure(s)' "$work/load.log" || fail "the menu after the loaded mission differs from the one before it"
# MULTIPLAYER: the button opens the room screens (host, join, the game list), not a notice.
mkdir -p "$work/mp"
OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- \
    --gfx "$work/enhanced.cfg" --mpplay 3000 --no-movies --nosound --shotdir "$work/mp" > "$work/mp.log" 2>&1 || true
grep -Fq 'MPPLAY|click|Multiplayer' "$work/mp.log" || fail "Multiplayer was not pressed"
if grep -Fq 'APP|menu|ra-route-notice' "$work/mp.log"; then fail "Multiplayer answered with a notice"; fi
grep -Fq 'MPPLAY|left|' "$work/mp.log" || fail "the multiplayer screen did not open"
# A ROOM STARTED FROM ITS LOBBY SCREEN: a host whose own Enhanced lobby presses Start, a
# joiner who asks for Ukraine in orange, two computers; both ends start the same room, on
# the room's seed, with its rules.
printf 'tick 5\ntick 300\nquit\n' > "$work/room.txt"
OPENCNC3D_BACKGROUND=1 ./opencnc3d --module ra3d --no-movies --nosound --gfx "$work/enhanced.cfg" \
    --savedir "$work" --mphost 40 --mpscreenstart --mpmap SCM10EA --mpbots 2 --side soviet --credits 10000 \
    --shroudregrow --difficulty easy --script "$work/room.txt" > "$work/room-host.log" 2>&1 &
rh=$!
sleep 3
OPENCNC3D_BACKGROUND=1 ./opencnc3d --module ra3d --no-movies --nosound --gfx "$work/enhanced.cfg" \
    --savedir "$work" --mpjoin 40 --mpmap SCM10EA --mpmyseat 4,4 --script "$work/room.txt" > "$work/room-join.log" 2>&1 &
rj=$!
w=0
while [ $w -lt 150 ] && { kill -0 $rh 2>/dev/null || kill -0 $rj 2>/dev/null; }; do sleep 1; w=$((w + 1)); done
kill -9 $rh $rj 2>/dev/null || true
grep -Eq '^LOBBY\|click\|Start\|' "$work/room-host.log" || fail "the host's lobby screen did not press Start"
grep -Fq 'MPLOBBY|started|seat=0|host=1|door=screen' "$work/room-host.log" || fail "the room did not start from the host's screen"
grep -Fq 'MPLOBBY|started|seat=1|host=0|' "$work/room-join.log" || fail "the joiner never started"
grep '^APP|raroom|' "$work/room-host.log" > "$work/room-host.room"
grep '^APP|raroom|' "$work/room-join.log" > "$work/room-join.room"
cmp -s "$work/room-host.room" "$work/room-join.room" || fail "the two ends started different rooms"
grep -Eq '^APP\|raroom\|start\|SCM10EA\|tech=10\|credits=10000\|ore=1\|crates=0\|shroud=1\|ctf=0\|difficulty=0\|seed=[1-9][0-9]*\|' \
    "$work/room-host.room" || fail "the room's rules or its seed did not travel"
grep -Fq 'APP|raroom|seat=0|person|country=USSR|colour=Red' "$work/room-host.room" || fail "the host is not the USSR"
grep -Fq 'APP|raroom|seat=1|person|country=Ukraine|colour=Orange' "$work/room-host.room" || fail "the joiner's own row did not reach the room"
# THE DEBRIEF: a skirmish lost to one hard computer ends on Red Alert's Enhanced
# debrief, drawn in its title kit.
mkdir -p "$work/deb"
: > "$work/beaten.txt"
i=0
while [ $i -lt 13 ]; do printf 'tick 1500\n' >> "$work/beaten.txt"; i=$((i + 1)); done
echo quit >> "$work/beaten.txt"
OPENCNC3D_BACKGROUND=1 ./opencnc3d --module ra3d --no-movies --nosound --gfx "$work/enhanced.cfg" --w 1280 --h 720 \
    --savedir "$work" --raskirmish SCM08EA --ai 1 --difficulty hard --credits 10000 --seed 4242 \
    --script "$work/beaten.txt" --shotdir "$work/deb" > "$work/deb.log" 2>&1 || true
grep -Fq 'APP|radebrief|SCM08EA|Shallow Grave (4)|win=0|decided=1|' "$work/deb.log" || fail "the skirmish did not end on its debrief"
grep -Fq 'RADEBRIEF|shown|once' "$work/deb.log" || fail "the Enhanced debrief screen was not drawn"
test -s "$work/deb/ra_debrief.png" || fail "no picture of the debrief"
python3 - "$work/deb/ra_debrief.png" <<'PY' || fail "the debrief is not drawn in Red Alert's red title kit"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); W, H = im.size
box = im.crop((W // 4, H // 3, W * 3 // 4, H * 2 // 3))
px = [box.getpixel((x, y)) for y in range(0, box.height, 2) for x in range(0, box.width, 2)]
# the panels are the title kit's dark red field, (18, 2, 0) and its kin; never the mission's blue
red = sum(1 for p in px if p[0] >= 12 and p[0] > 2 * p[1] + 1 and p[0] > 2 * p[2]) / len(px)
blue = sum(1 for p in px if p[2] > 40 and p[2] > p[0] + 15) / len(px)
print('RAENH|debrief|red=%.2f|blue=%.2f' % (red, blue))
assert red > 0.5 and blue < 0.01
PY
echo "RA3D Enhanced menus: kit, side choice, mission, blue pause dialog, its own slot list and back, menu after the mission, the skirmish lobby's match, Load Mission, Multiplayer, a room started from its lobby screen, the debrief: PASS"
