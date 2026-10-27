"""Render every Red Alert Enhanced menu mockup at 2560x1440 (and the main menu at 720p)."""
import os, sys, re
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import paths
import screens as S, compose as K, chassis as C

OUT = sys.argv[1] if len(sys.argv) > 1 else paths.OUT
os.makedirs(OUT, exist_ok=True)
SRC = paths.WORK
done = []
def save(name, img):
    p = os.path.join(OUT, name + '.png'); img.save(p, optimize=True); done.append(p); print(p)

def shot(name, v, bg='title', title=False, main=False, pointer=None, fb=(2560, 1440)):
    img, _ = K.compose(v, bg, title=title, main=main, pointer=pointer, fb=fb); save(name, img)

# ---- the title (red) ----------------------------------------------------------------
shot('01-main-1440', S.main(hover='Skirmish'), title=True, main=True, pointer=(320, 318))
shot('02-main-720', S.main(hover='Skirmish'), title=True, main=True, pointer=(320, 318), fb=(1280, 720))
shot('03-main-exit-confirm-1440', S.choice_exit(), pointer=(470, 256))
still = Image.open(os.path.join(SRC, 'intro', 'PROLOG_038.png')).convert('RGB')
shot('04-intro-1440', S.choice_intro(still), pointer=(300, 398))

# the Settings opened from the title: compact frame under the wordmark
shot('05-title-settings-visuals-display-1440', S.visuals(0, theme='red', compact=True), title=True)
shot('06-title-load-mission-1440', S.slots('load', theme='red', compact=True), title=True)

# skirmish
prev = {}
def mapprev(theater, name, cut):
    # the 1996 terrain of the whole map, as the texture remaster tools render it
    p = os.path.join(paths.LAB_RA, 'textures', theater, 'MAP_' + name, 'dos.png')
    Image.MAX_IMAGE_PIXELS = None
    return Image.open(p).convert('RGB') if os.path.exists(p) else None
prev['SCM06EA'] = mapprev('RATEMP_144', 'SCM06EA', None)
prev['SCM11EA'] = mapprev('RASNOW_144', 'SCM11EA', None)
shot('07-skirmish-players-1440', S.lobby(False, prev['SCM06EA'], 'Isle of Fury (6-8)', (8, 96, 96)))
shot('08-skirmish-rules-1440', S.lobby(False, rules=True))
shot('09-skirmish-player-setup-1440', S.seat_popup())
shot('10-skirmish-player-setup-country-1440', S.seat_popup(drop='Country'))
maps = []
for f in sorted(os.listdir(SRC)):
    m = re.match(r'(SCM\d\dEA)\.INI$', f)
    if m:
        t = open(os.path.join(SRC, f), 'rb').read().decode('latin-1')
        nm = re.search(r'^Name=(.*)$', t, re.M).group(1).strip(); th = re.search(r'^Theater=(.*)$', t, re.M).group(1).strip()
        w = int(re.search(r'^Width=(\d+)', t, re.M).group(1)); h = int(re.search(r'^Height=(\d+)', t, re.M).group(1))
        wp = len(set(re.findall(r'^(\d)=\d+\s*$', t.split('[Waypoints]')[1].split('\n[')[0], re.M)))
        maps.append((m.group(1), nm, min(8, wp), w, h, th))
sel = [m[0] for m in maps].index('SCM11EA')
shot('11-skirmish-change-map-1440', S.mappicker(maps, sel, prev, top=8))

# multiplayer
shot('12-multiplayer-host-1440', S.browser('host'))
shot('13-multiplayer-join-1440', S.browser('join'))
shot('14-multiplayer-player-name-1440', S.browser('name'))
shot('15-multiplayer-wait-room-1440', S.browser('wait'))
chat = ['Lobby: Natasha joined the game.', 'Natasha: Soviet side, team 2.', 'Commander: Isle of Fury, 10000 credits?', 'Natasha: Fine. No crates.', 'Lobby: Natasha is ready.']
shot('16-multiplayer-lobby-1440', S.lobby(True, prev['SCM06EA'], 'Isle of Fury (6-8)', (8, 96, 96), chat=chat))
shot('17-multiplayer-lobby-rules-1440', S.lobby(True, rules=True))

# special ops, user maps
shot('18-special-ops-empty-1440', S.ops(True, [], empty_text='No Counterstrike or Aftermath missions are installed. The 1996 freeware discs carry the base game only; this button stays greyed on the main menu until expansion missions are found.'))
am = [('SCG43EA', 'Harbor Reclamation', 'Allied'), ('SCG41EA', 'In the nick of time', 'Allied'), ('SCG40EA', 'Caught in the act', 'Allied'),
      ('SCG42EA', 'Production Disruption', 'Allied'), ('SCG47EA', 'Negotiations', 'Allied'), ('SCG45EA', 'Monster Tank Madness', 'Allied'),
      ('SCG44EA', 'Time Flies', 'Allied'), ('SCG48EA', 'Absolut MADness', 'Allied'), ('SCG46EA', 'Pawn', 'Allied'),
      ('SCU43EA', 'Testing Grounds', 'Soviet'), ('SCU40EA', 'Shock Therapy', 'Soviet'), ('SCU42EA', "Let's Make a Steal", 'Soviet'),
      ('SCU41EA', 'Test Drive', 'Soviet'), ('SCU45EA', "Don't Drink The Water", 'Soviet'), ('SCU44EA', 'Situation Critical', 'Soviet'),
      ('SCU46EA', 'Brothers in Arms', 'Soviet'), ('SCU47EA', 'Deus Ex Machina', 'Soviet'), ('SCU48EA', 'Grunyev Revolution', 'Soviet')]
shot('19-special-ops-aftermath-1440', S.ops(True, am, tab=1, sel=2), pointer=(320, 290))
shot('20-user-maps-empty-1440', S.ops(False, [], empty_text='No user maps installed. Red Alert scenario files (.INI or .MPR) placed in the user maps folder appear here.'))

# database
import numpy as np
_t = np.array(Image.open(os.path.join(paths.LAB_RA, 'TSLA', 'final', 'view_0.png')).convert('RGBA')).astype(np.int16)
_bg = _t[0, 0, :3]; _d = np.abs(_t[..., :3] - _bg).sum(-1)
_t[..., 3] = np.clip((_d - 6) * 40, 0, 255)          # the lab render's flat backdrop keyed out
tsla = Image.fromarray(_t.astype(np.uint8), 'RGBA')
bb = tsla.getbbox(); tsla = tsla.crop(bb); k = min(296 / tsla.height, 332 / tsla.width) * 0.94
tsla = tsla.resize((int(tsla.width * k), int(tsla.height * k)), Image.LANCZOS)
db = {'category': 0, 'house': 'Soviet', 'selected': 7, 'detail': 0,
      'names': ['Construction Yard', 'Power Plant', 'Ore Refinery', 'Barracks', 'Flame Tower', 'Radar Dome', 'War Factory', 'Tesla Coil',
                'Kennel', 'Airfield', 'SAM Site', 'Service Depot', 'Sub Pen', 'Advanced Power', 'Tech Center', 'Iron Curtain', 'Missile Silo', 'Ore Silo', 'Helipad'],
      'name': 'Tesla Coil', 'code': 'TSLA', 'cost': 1500,
      'brief': 'Soviet base defence. Fires an electric bolt at ground targets and needs power to charge. Built after a War Factory.',
      'armour': 'Heavy', 'hp': 400}
shot('21-database-1440', S.database(db, tsla), pointer=(1100, 104))
shot('22-notice-1440', S.notice('The Red Alert movies are not installed. Missions start without their briefing films; the Settings screen can fetch them.'))

# ---- in a mission (blue, the scenario palette) ----------------------------------------
shot('30-pause-1440', S.pause('blue', hover='Visuals'), bg='mission', pointer=(520, 198))
shot('31-pause-red-alternative-1440', S.pause('red'), bg='mission')
shot('32-game-controls-1440', S.controls('blue'), bg='mission')
pl, _ = C.piece('BTN-PL', 0, C.THEMES['blue']); st_, _ = C.piece('BTN-ST', 0, C.THEMES['blue'])
img, tracks = S.sound('blue', play_icon=pl, stop_icon=st_)
shot('33-sound-controls-1440', img, bg='mission')
shot('34-gameplay-1440', S.gameplay('blue'), bg='mission')
shot('35-visuals-display-resolution-1440', S.visuals(0, 'blue', drop='res'), bg='mission')
shot('36-visuals-world-1440', S.visuals(1, 'blue'), bg='mission')
shot('37-visuals-effects-1440', S.visuals(2, 'blue'), bg='mission')
shot('38-visuals-interface-1440', S.visuals(3, 'blue'), bg='mission')
shot('39-visuals-more-1440', S.visuals(4, 'blue'), bg='mission')
shot('40-save-mission-1440', S.slots('save', 'blue'), bg='mission')
shot('41-load-mission-1440', S.slots('load', 'blue'), bg='mission')
shot('42-delete-mission-1440', S.slots('delete', 'blue'), bg='mission')
shot('43-confirm-abort-1440', S.confirm('abort'), bg='mission')
shot('44-confirm-delete-1440', S.confirm('delete'), bg='mission')
shot('45-confirm-leave-match-1440', S.confirm('leave'), bg='mission')
ali = Image.open(os.path.join(SRC, 'HIRES_ALIPAPER.png')); sov = Image.open(os.path.join(SRC, 'HIRES_SOVPAPER.png'))
def brief(name):
    t = open(os.path.join(SRC, name + '.INI'), 'rb').read().decode('latin-1')
    sec = t.split('[Briefing]')[1].split('\n[')[0]
    return ' '.join(l.split('=', 1)[1].strip() for l in sec.strip().splitlines() if '=' in l).replace('@', '\n')
shot('46-briefing-allied-1440', S.restate(ali, brief('SCG01EA')), bg='mission')
shot('47-briefing-soviet-1440', S.restate(sov, brief('SCU01EA'), theme='blue'), bg='mission')
shot('48-cheats-1440', S.cheats('blue'), bg='mission')
print(len(done), 'mockups')
