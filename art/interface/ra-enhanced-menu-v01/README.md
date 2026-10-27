# Red Alert Enhanced menus v01

Mockups of every screen the Tiberian Dawn Enhanced menu has, in Red Alert's version, plus
the concepts for the two new 3D faction emblems. `PROPOSAL.md` lists the choices that were
open; all of them are now settled (below).

## Approved

The design is approved with option A of every row of `PROPOSAL.md`:

- red dialogs at the title, blue in a mission, as the 1996 dialogs are;
- the 1996 field darkened to 62% (the grid to 50%);
- the RA3D lettering lifted off the title and drawn above the panel at whole-pixel scales
  (the HIRES cut at half the panel's scale when that is even, the LORES cut when it is odd);
- the HIRES title as the backdrop where it fits at least twice, the LORES title below that;
- GRAD6FNT, the LORES face drawn 2x; warm white labels;
- the shared Enhanced pointer (CUR00);
- the Allied emblem as the 1996 plaque (concept A, no globe), the Soviet star as drawn;
- the Enhanced side choice box (both emblems, Allies / Soviet, the difficulty, Start);
- Special Ops as Counterstrike and Aftermath tabs, greyed on the main menu while none is
  installed; User Maps live, the empty screen saying where scenario files go;
- the Database with five categories (Ships added) and RULES.INI numbers;
- the network lobby rows as "England / 1" (the start in Player Setup);
- the legal footer as proposed;
- menu sounds: the shared HUD hover and click clips.

The two 3D emblems are approved as built and shown in `emblems/emblems-review.png`: the
Allied plaque (296 triangles) and the Soviet star (712 triangles).

The match debrief, which had no mockup, is approved as proposed: the red Enhanced dialog over
the title art (not the 1996 MLTIPLYR.WSA backdrop), titled TOP SCORES, with the 1996 screen's
three panels (Commander, Battles Won, Kills one column a game), the result and the map above
them and this machine's own numbers below. `em_ra_debrief` draws it; G325 renders it won and
lost (`ra-debrief`, `ra-debrief-defeat`).

The rules page of the skirmish and network lobbies follows `mockups/08` and `mockups/17`
(Bases, Ore Regenerates, Crates, Shroud Regrows, Capture The Flag, AI Takeover) and adds a
fourth slider under Unit Count, Computers: Easy / Normal / Hard, as on the 1996 skirmish
dialog. This paragraph records the original approved design; the authorized
19 October 2026 functional amendment below supersedes that global skill control.

## Per-player difficulty amendment — 19 October 2026

The design review requested Easy, Normal, Hard and Expert in both games, selectable per AI
in Skirmish. The Players well now has a Difficulty column, reusing the accepted RA
chassis, font and control states. Human/open/blocked rows show a dash. Hosts edit
AI values; joiners read the synchronized values. Rules has a four-value New AI
default for newly added computers; existing seats retain their own settings.

Local Players is600w; each Difficulty seat is120x30 at x500, row stride32. Network
Players/Map/Chat are508/188/404 wide at x38/566/774, all368h at y150 with20px gaps;
network Difficulty is at x408. These fitted widths preserve the1216x636 housing.
Classic retains its320x200 controller with E/N/H/X and full names in the seat popup.

The source/image SHA-256 manifest, actual native captures at720p/1440p, native
interaction checks and Remaster Lab browser review are preserved under
`tools/art/remaster/viewer/ai-difficulty-v01/`. No source-art replacement was needed.
See `docs/design-fair-skirmish-ai.md` for the honest runtime policy and the dated
fair-skirmish verification receipt for final main/Dock delivery. Native and browser
checks establish layout/controller behavior; they do not claim a new art approval.

## What the game ships from this design

- `kit/`: the hand-checked plates the pack is baked from: `title-backdrop-hi.png` and
  `title-backdrop-lo.png` (the title with the lettering lifted off, made by
  `kit/cleanplate_hand.py` and checked by eye at both densities) and `intro-still.png`
  (frame 2220 of PROLOG.VQA, the movie's own 320 x 156).
- `menu/bake_ra_enhanced_menu.py`: everything else (frames, buttons, font, palettes,
  wordmark cuts, briefing papers, colours) from `data/radata`, into
  `ra3d/enhanced-menu.pack`.
- `emblems/menu_ra_emblem_meshes.h`: the two low poly emblems, built by
  `emblems/build_emblems.py` from the concepts (needs Blender); `emblems/emblems-review.png`
  shows them beside the concepts.
- Current runtime capability (19 October2026): New HUD,3D Trees and3D Grass are live
  controls, on by default in Enhanced. Trees use the accepted Enhanced model reuse;
  grass uses RA's native ground masks.3D Ore, Bushes, Rocks and Ground patches remain
  unavailable. Earlier mockups greyed trees/HUD before their implementation; that
  historical restriction no longer describes the shipped Settings controller.

## What is fixed by the requirement

- Frames and buttons are built from Red Alert's own Win95 dialog art (`DD-BKGND`,
  `DD-EDGE`, `DD-LEFT`, `DD-RIGHT`, `DD-TOP`, `DD-BOTM`, `DD-CRNR`, `BTN-UP`, `BTN-DN`,
  `BTN-PL`, `BTN-ST` from HIRES.MIX), laid out exactly as the TD Enhanced screens are: the
  same frame sizes, seats, wells and native coordinates as `menu/enhanced_menu.cpp`, so the
  existing controllers drive them unchanged.
- The title art (TITLE.PCX with the RA3D lettering, `menu/art/ra3d-title-win95.png`)
  behind the menus, Red Alert's palettes and its GRAD6FNT font.
- New 3D Allied and Soviet emblems, made through the concept step.
- Menu sounds: the shared HUD hover and click clips TD's Enhanced menu plays.

## How the chassis is made

The 1996 `Dialog_Box` (redalert/dialog.cpp) is two silver rails with a black channel,
brass-riveted corner plates, ribbed inner strips and a field of a 48 pixel grid with
diagonals and range marks. At the title its palette makes the field and buttons red; in a
mission the scenario palette makes them blue. The Enhanced frames keep every one of those
pieces at their native HIRES pixels (one HIRES pixel is one native unit, the unit the TD
panels use) and change three things:

- the field continues at any size: the grid at its own pitch and phase, the 1996 diagonal
  and marks block centred as `Dialog_Box` centres it and mirrored outward, so no line is cut;
- the field is darkened (62%, the grid 50%) and drops away from the rails by two pixels, so
  plates and text separate from it;
- buttons, checks, sliders, fields, rows and swatches follow the TD Enhanced controls'
  geometry and six states, drawn in the dialog's own colour ramp (PALETTE.CPS row 10,
  indices 192 to 199), with the rail's brushed grain and the corners' brass as fasteners.
  The Sound Controls' Play and Stop carry the 1996 `BTN-PL` and `BTN-ST` glyphs.

Accent text (well titles, values) is the title lettering's gold; body text is warm white.

## Files

| Path | What |
|---|---|
| `mockups/00-index.png` | every picture on one sheet |
| `mockups/01..22-*` | the title side: main menu (1440p and 720p), exit, intro, Settings and Load opened from the title, skirmish (players, rules, player setup, country list, map picker), multiplayer (host, join, name, wait room, lobby with chat, lobby rules), Special Ops (empty and filled), User Maps, Database, notice |
| `mockups/30..48-*` | in a mission: pause (blue, and a red alternative), Game Controls, Sound Controls (jukebox), Gameplay, Visuals (all five pages, a dropdown open), Save, Load, Delete, the confirm boxes, the mission briefing with ALIPAPER and with SOVPAPER, Cheats |
| `mockups/50-kit-chassis.png` | the 1996 pieces as `Dialog_Box` draws them, and every Enhanced control state in both palettes |
| `mockups/52..55-choice-*` | the open choices: pointer, lettering, font, an Enhanced side choice |
| `mockups/60-emblem-concepts.png` | the emblem concepts beside their 1996 references |
| `emblems/<side>/<round>/` | each concept: the inputs it was given (`1_..6_`) and `concept.png` (pixel-only re-save) |
| `mockup/` | the generator (`build.sh`); `emblem_inputs.py` rebuilds the concept inputs |

All screen mockups are 2560x1440, placed as `em_draw` places a panel: integer scale
`h / 693` (2 here), the title offset and wordmark on title screens, the backdrop integer-fit
on title screens and cover-filled behind other shell screens, a mission under 30% black
behind in-mission screens, the legal footer at its own scale. Emblems in the mockups are
cut from the concept sheets and stand in for the 3D models; the Tesla Coil in the Database
is the lab render of its remaster. The battlefield behind in-mission screens is a saved Red
Alert Enhanced game capture (`RAMENU_BATTLE` names another). Saves, rooms, chat and player
names are fixtures; maps, countries, colours, themes, missions, briefings and rules come
from the 1996 data.

## Rebuild

```
art/interface/ra-enhanced-menu-v01/mockup/build.sh
```

It reads `data/radata` (dialog pieces, palettes, PALETTE.CPS, TITLE.PCX, ALIPAPER,
SOVPAPER, MOUSE, the SCM maps, two briefings), `menu/art/ra3d-title*.png`,
`audio/ra_theme_table.h`, one prologue frame through ffmpeg, the shared pointer from a play
folder (`OPENCNC3D_PLAY`, `ecur/ecur.bin`), and two lab products under
`build-native/remaster-ra` (the Tesla Coil render, the 1996 terrain of two maps for the map
previews). Intermediates go to `build-native/ra-enhanced-menu-v01`.

The title with its lettering lifted off (`mockup/cleanplate.py`) is a mockup-grade plate:
inside the screen circle the lettering becomes black field and grid, over the bezel ring
the ring is rotated in from below the wing. A shipping plate needs a hand-checked cleanup.

19 October fair AI delivery: current-main TD/RA binaries are installed in the
existing legacy C&C3D.app Dock installation. Exact installed/source hashes, preserved settings
and hidden checks are in `docs/verification/2026-10-19-fair-skirmish-ai/`.

## Main-title cleanup amendment —24 October2026

The design review approved the exact generated **RA3D** wordmark and scope cleanplate in
the title cleanup round. That version supersedes the
older lifted Red Alert 3D lettering and baked rectangular panel/copyright for the
Enhanced title backdrop; the native menu frame and controls remain unchanged. The live
main-menu footer is also suppressed. The original kit/scans stay preserved here.
See that version's artwork for derived384×130/192×65wordmarks,640×400/320×200
backdrops, actual720p/1440p/native-browser evidence and92-image RA gate result.
