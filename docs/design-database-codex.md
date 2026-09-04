# The DATABASE tab

An in-game codex: a full-screen reference for every structure, soldier, vehicle and
aircraft the player can build, with the real cartridge model turning in it and the 1995
engine's own numbers beside it. Opening it stops the world.

Section 6 lists what this feature still owes.

---

## 1. Where the tab comes from

It is not an invention. A 1995 pre-release screenshot of Tiberian Dawn shows a **four-tab
strip** across the top of the tactical view:

    TACTICAL | OPTIONS | DATABASE | S.DIGEST

The shipped game kept one of them. This brings back the second, from that screenshot.

The plate is the plate we already have. `tools/sidebar_redesign/tabs.py` letters the HUD's
tab plates from **GRAD6FNT out of `dossidebar.pack`** at 1.75x with a 12-pixel pitch, and
DATABASE is a fourth call to the same `plate_with()` that produced OPTIONS and SIDEBAR:
99 pixels of lettering inside the 132 the plate has between its end rivets. Nothing about
it is drawn by hand and nothing about it is a new design.

It occupies the 160-pixel slot **immediately right of OPTIONS**, which is where the 1995
strip put that caption and which is the slot the 640 HUD has been leaving empty since it
was laid out:

    | OPTIONS | DATABASE |            ...            | CREDITS | SIDEBAR |
      0..159    160..319                               fbw-320   fbw-160

## 2. What it is gated on, and why there are two conditions

`codex_available()` in `game/codex_mod.h`:

```c
return g_fx.enabled != 0 && g_hudNew && g_h6Pack != NULL;
```

**`g_fx.enabled` is Enhanced.** Classic is the 1995 game and the 1995 game shipped no
database, so the plate must not appear there. This is the same switch the movement queue
sits behind and no new one is added for it.

**`g_hudNew` is the Enhanced HUD.** The strip this plate lives in is the 640 HUD's. The
classic DOS bar has no plate row to put it in, and its tab art is the cartridge's, which
carries no DATABASE caption. The requirement is explicit: this is built on the Enhanced
HUD and is not available in Classic mode.

The hit test, the plate draw, the keyboard and the script verbs all ask that one function,
so the tab cannot be clickable on a screen that is not drawing it.

## 3. What happens when it opens

| | |
|---|---|
| the world | **stops**, through the same tick gate the pause dialog uses |
| the sidebar drawer | folds away (`g_h6SlideTarget = H6_BAR_W`), 16 frames |
| the four pinned plates | stay; DATABASE latches down, SIDEBAR greys out |
| the battlefield | stays on the glass under a 78% wash |
| the music | fades to **60%** (a 40% cut) over **1.5 s** |
| the house | taken from `g_playerHouse`, so a Nod mission opens the Nod roster |

Closing gives the drawer back where it was, unlatches the plate, restarts the world, and
fades the music back to the **Sound Controls setting** over 1.5 s.

### The pause

One predicate, `codex_holds_the_world()`, feeds the tick gate:

```c
if (!paused && !g_optOpen && !sim_over && !codex_holds_the_world()) { ... }
```

The rule is: opening the page pauses the game, EXCEPT in multiplayer. There is no
networked game on this branch to except -- the lockstep work lives on `multiplayer` -- so the exception is
written as one named function with the network case spelled out in its comment rather than
as a condition scattered through the loop. When that branch lands it is a single line.

### The music duck

Two things make it more than a lerp, both in `codex_audio_step()`:

1. **The ceiling is the Sound Controls setting**, `g_optState.set.music`, not whatever the
   mixer held when the screen opened. Reading the live volume as the base would ratchet:
   open, close early, open again, and each pass would take another 40% off the already
   ducked figure until the music was gone.
2. **It runs on the wall clock, not the tick.** The world is stopped while the page is up,
   which is exactly when the fade down has to happen; and the fade back up outlives the
   page by a second and a half. So it is stepped from `draw_frame` every frame, open or
   closed, and not from the page's own draw.

## 4. The screen

Two axes, as asked for.

```
+----------------------------------------------------------------------+
| OPTIONS | DATABASE |                        | CREDITS | SIDEBAR      |  tab strip
+----------------------------------------------------------------------+
|  [emblem]            DATABASE                          [EVA]         |  header band
|                 GDI FIELD ARCHIVE                                    |
+----------------------------------------------------------------------+
| STRUCTURES |  INFANTRY  |  VEHICLES  |  AIRCRAFT |                   |  horizontal axis
+------------+---------------------------------------------------------+
| POWER PLANT|  +-------------------+   Power Plant                    |
| BARRACKS   |  |                   |   NUKE   $300                    |
| REFINERY   |  |   the real model, |   Supplies 100 power; unarmed;   |
| ...        |  |   turning         |   wood armour.                   |
|            |  +-------------------+   RANGE   4.0 cells              |
| (vertical  |                          ARMOUR  Wood                   |
|  axis)     |  HITPOINTS  [====----]  200                             |
|            |  DAMAGE     --                                          |
|            |  SPEED      --                                          |
|            |  ATTACK SPEED --                                        |
|            |  STRONG VS  ...                                         |
|            |  WEAK VS    Obelisk, Artillery, S.S.M. Launcher         |
|            |  UNLOCKS    Advanced Power Plant, Barracks, ...         |
+------------+---------------------------------------------------------+
```

**The emblems.** The faction emblem top left and the EVA wordmark top right, both real
cartridge meshes turning slowly, drawn through `app/logo3d.c` -- the same module the score
screen uses. `logo3d.c` is now compiled into the renderer as well as the app; one file,
one format, one baker.

**The page is one 8-bit DOS surface.** Chrome, text, boxes and bars are rasterised into a
`DB_Surface` at HUD scale and put on the glass as one texture and one quad. That is
`dosopt_gl.h`'s mechanism verbatim, and the reason to copy it is the text: `db_print`
gives the game's own letterforms for nothing, so not one glyph on this screen is
authored. The alternative -- GL quads and a font atlas -- would have meant inventing
letterforms for a screen that is mostly words.

### It wears the pause dialog's clothes, not the sidebar's

The requirement is that the page match the game's other menus: the same green, the same
font and style, the same borders and frames.

The first cut of this page was drawn in the SIDEBAR's idiom -- 6POINT and 8POINT through
the full-shadow palette, grey `db_draw_box` bevels, and hitpoints in green, damage in red,
speed in blue. Every one of those is a real thing this program does; none of them is what
the Options, Game Controls and Sound Controls screens do. So the page was rebuilt on the
dialogs' own four widgets, which are now **exported from `dosopt.c`** rather than copied:

| widget | what it draws | 1995 origin |
|---|---|---|
| `dopt_style_dialog` | black, with a one-pixel green rect inset by one | `dialog.cpp:64` BOXSTYLE_GREEN_BORDER |
| `dopt_style_caption` | two OPTIONS.SHP filigrees, the centred caption, the rule under it | `goptions.cpp:507` Draw_Caption |
| `dopt_style_plate` | the green button plate, bevel swapped when pressed | `dialog.cpp` ButtonColorsClassic |
| `dopt_style_track` | the sunken green gauge with its bright fill | `dialog.cpp` BOXSTYLE_GREEN_DOWN |

Exporting rather than copying is the point: there is **one** dialog style in this program,
and the day somebody retunes a bevel both screens move together.

The palette is the dialogs' own `DOPT_` constants, which are 1995's `CC_` entries with the
`defines.h` line numbers beside them:

| role | index | constant | RGB |
|---|---|---|---|
| ground | 12 | `DB_BLACK` | 0,0,0 |
| frame | 159 | `DOPT_GREEN_BOX` | 60,152,56 |
| caption + rule | 3 | `DOPT_CC_GREEN` | 0,168,0 |
| label | 41 | `DOPT_TEXT_MEDIUM` | 84,176,36 |
| value, selected row | 4 | `DOPT_TEXT_BRIGHT` | 84,252,84 |
| plate face, selected fill | 141 | `DOPT_GREEN_BKGD` | 48,84,44 |
| plate shadow | 140 | `DOPT_GREEN_SHADOW` | 40,68,36 |
| gauge fill | 167 | `DOPT_BRIGHT_GREEN` | 140,200,8 |

The font is **GRAD6FNT** through `db_font_palette_grad`, which is what `dosopt.c` asks for
ten times and for nothing else. Despite its name that helper is not a gradient -- it is
`dialog.cpp`'s TPF_NOSHADOW arm, a flat single-colour palette -- which is why the page's
colour set is exactly nine values and G147 can assert membership rather than hue.

**The page is monochrome green on purpose.** A 1995 C&C dialog carries one hue and
separates things by BRIGHTNESS: `DOPT_TEXT_MEDIUM` for a label, `DOPT_TEXT_BRIGHT` for its
value. Colour-coding the stats was informative and was a different program's screen.

The surface is re-rasterised only when something changes. The three moving things (model,
emblem, wordmark) are drawn over it in their own GL viewports, so a still page costs one
quad a frame.

**The model viewport.** `codex_draw_model()` sets up its own projection and view inside a
scissored viewport and calls the renderer's own `draw_mesh`, so the model in the codex is
the model on the battlefield and cannot drift from it. Three things it has to do that the
world pass does not:

* **opt out of the shroud** (`g_meshNoShroud`). `draw_mesh` subtracts the shroud envelope
  sampled at each vertex's world position, and this draw parks the model at world (0,0),
  which in a real mission is unexplored. Without the opt-out every model rendered as a
  black silhouette on a black plate. It looks like a lighting bug and it is a location bug.
* **measure only what it draws.** The framing box excludes `MODE_SHADOW` triangles. A
  structure carries baked shadow faces cast out across the ground beside it, and this
  viewport does not draw them; measuring them anyway inflated the box by the length of the
  shadow and dragged its centre off the building, so every structure came out small and
  parked in a corner.
* **move the model, not the camera.** `draw_mesh`'s `cx`/`cz`/`ylift` put the box centre on
  the origin. Aiming the camera instead works out the same for a static mesh and stops
  working the moment anything rotates.

**Infantry have no mesh, and that is the cartridge's doing.** This project draws its
infantry from the 1995 MS-DOS art (`dosinfantry.pack`); the N64 billboards it replaced were
billboards too. So an infantry entry gets a **turntable of the eight baked facings**
stepped in the engine's own facing order -- the same rotation, out of the art that exists.

## 5. Where the numbers come from

**All of them are the 1995 engine's own.** `tools/database_codex/codex_data.py` reads
`brain/vanilla/tiberiandawn/` directly and `gen_codex_table.py` writes `game/codex_table.h`
from what it found. Nothing in the table is typed by hand and the header says so.

The four type tables are positional constructor calls and the parameter NAMES for each
position are in `type.h`, so the reader lifts the signature, lifts the call, and zips them.
That is why it survives the four files not sharing a comment convention: `udata.cpp` writes
`// STRENGTH:`, `bdata.cpp` writes `// STRNTH:` and `aadata.cpp` writes `// The strength of
this unit.` A comment-tag parser reads two of the three and silently drops the third.

An arity mismatch is a hard error, never a shrug: a shifted zip would print one row's
armour beside another row's speed and look entirely reasonable.

Three things that had to be handled rather than assumed, each of which was caught by that
refusal rather than by a reader noticing a wrong number on screen:

* **Preprocessor conditionals, two of five live.** `SCENARIO_EDITOR` (defines.h:93) and
  `PATCH` (defines.h:61) are defined and their branches compile; `ADVANCED`, `NEVER` and
  `OBSOLETE` are defined nowhere. The Recon Bike's STRENGTH is written twice under
  `#ifdef ADVANCED` -- 90 and 160 -- and 160 is the one the compiler takes. A macro in
  neither list is a hard error, so the next conditional somebody adds cannot ride in on
  this one's precedent.
* **Default arguments.** `BuildingTypeClass`'s last parameter is `bool is_unsellable =
  false`; taking the last identifier of that text yields the name "false" and every one of
  the 65 building rows then reads one argument short.
* **Fields fixed in the base-class call.** `InfantryTypeClass` takes no `armor` and no
  `is_buildable`; its constructor passes `ARMOR_NONE` and `true` straight into
  `TechnoTypeClass`. Read positionally one level down. Without it all twenty infantry came
  out armour-less, which silently emptied every strong/weak list they appear in.

### The derived fields

| stat | source |
|---|---|
| Hitpoints | `strength`, verbatim |
| Damage | `Weapons[primary].damage` (`const.cpp`) |
| Attack speed | `Weapons[primary].rof`, ticks BETWEEN shots |
| Speed | `MPHType` through `defines.h` (MPH_MEDIUM = 18) |
| Range | the weapon's range in leptons, 256 to the cell |
| Unlocks | the reverse of the `pre` (STRUCTF_) prerequisite field |

**Attack speed reads backwards on purpose.** The engine's number is a tick DELAY, so small
is fast. The bar is drawn from its complement so a long bar means fast; the printed figure
stays the engine's own, so nothing on the page disagrees with the source.

**Strong and weak are computed, not authored.** C&C has no "strong against" field and no
shipped list of matchups. What it has is `Warheads[].modifier[armor]`, a 0x100-relative
multiplier per armour class. Two readings:

* **STRONG VS** -- the opponents this entry's primary weapon lands the most damage on
* **WEAK VS** -- the opponents whose primary weapon lands the most damage on this one

Both rank by effective damage (base x armour modifier), keep only pairings at 0xC0 or
better, and take the leaders. **Cost is the tiebreak**, and it is load bearing: within one
armour class every pairing scores identically, so ranking on damage alone broke ties
alphabetically and printed "Airstrip, APC, Flame Tank, Gun Turret" against every
armour-piercing weapon in the game. Of the things a weapon beats, the expensive ones are
the ones worth naming.

The bar scales (`CODEX_MAX_*`) are computed across the whole table at generation time, so
two entries can be compared by eye.

### The roster

Buildable, house-owned, non-civilian: **50 rows** -- 25 structures, 15 vehicles, 7 infantry,
3 aircraft. The 37 civilian buildings, the ten named civilians, the dinosaurs and the
aircraft the engine flies in by itself stay out, because a codex is a build reference and
not a dump of the type tables.

## 6. What is owed

This is the short list; each is recorded in full where gaps are registered.

1. **The 1995 manual's unit copy is not in this repository.** It is the one thing on the
   page that is not 1995's. Each row carries a sentence assembled out of fields it already
   holds ("Tracked; armed with the 105mm cannon; range 4.8 cells; steel armour."), because
   writing a paragraph in the manual's voice would be inventing 1995 text. **Dropping the
   real copy in is a change to `codex_data.py`'s `brief()`, not to the screen.**
2. **Tier 1 will need the page tiled.** It is one 1024x1024 POT texture and a Voodoo 2 caps
   at 256x256. The pause dialog has the same problem at 512 and the Win98 branch handles
   its own drawing, so this is that branch's when it gets there.
3. **The multiplayer exception is unwired**, because there is no networked game on this
   branch to except. One line in `codex_holds_the_world()`.
4. **The EVA wordmark's lighting is ours.** Its Vtx RGB bytes are three grey levels, not
   packed normals, so `bake_logos.py` synthesises face normals for it. The three emblems
   keep the cartridge's own.

## 6b. It needs two packs re-baked, and a binaries-only update will not carry them

**This is the way this feature breaks on somebody else's machine.** Two baked files change
with it and NEITHER is tracked in git -- both are build products, regenerable from inputs
that ARE tracked:

| file | why it changed | regenerate with |
|---|---|---|
| `playable/hud640.pack` | carries the new `tab_database` plate | `python3 tools/sidebar_redesign/bake_pack.py` |
| `game/logos.pack` | carries EVA as a fourth logo | `python3 game/bake_logos.py` then copy it beside the binary |

A checkout that pulled this and rebuilt only the BINARY used to get the whole feature
with a black gap where the tab belongs: `hud640_draw_tab` finds no `tab_database` asset,
blits nothing, and uploads a transparent plate, so the slot reads black beside a lit
OPTIONS and nothing anywhere says why. Correct code, stale data, no error.

**`game/make-build.sh` now bakes both packs itself and checks them by CONTENT**, so that
path is closed. It had two holes and each one alone was enough:

* its pack-refresh loop scans `tools/bakery/game` and `game/`, and `bake_pack.py` writes
  straight to `playable/hud640.pack` -- neither of those -- so the loop had never seen
  that pack at all;
* `check_asset` is an existence test, which any pack from any previous build satisfies
  for ever.

It now runs both bakers before the refresh, greps `hud640.pack` for `tab_database`, and
reads `logos.pack`'s header for a fourth logo. Either one stale is a named failure with
the command to fix it, not a silent success.

**`tools/win/make-build-win.sh --bins-only` remains the exception**: six executables and
DLLs and no pack at all, by design, which is right for every change that does not touch
baked data and wrong for this one. A binaries-only update for this build must have those
two packs copied in beside it.

By hand, on a fresh checkout:

```
python3 tools/sidebar_redesign/bake_pack.py     # -> playable/hud640.pack
python3 game/bake_logos.py                      # -> game/logos.pack
cp game/logos.pack playable/logos.pack
cd game && sh build.sh                          # the renderer
cd ../app && sh build.sh                        # the game, staged to ../playable
```

Both bakes read only committed inputs (`tools/sidebar_redesign/chunks/tab_database.png`
and `data/rom/cnc_eu.z64`), so they work from a clean clone with no extra assets.

### The strip and the page are drawn at two different scales

Worth knowing before measuring anything on this screen, and the source of three separate
defects. The tab strip draws at the HUD's own `g_h6Scale` -- 1 at 1280x720. The page does
not and cannot: it is **one fixed layout space** `CODEX_PAGE_W` pixels wide, scaled up by
whatever whole multiple the window holds.

That pinning is itself a fix. The page used to take the HUD's scale and lay itself out in
whatever pixels that left, which is wrong in the one direction nobody tests on: every size
on this page is an absolute number of page pixels, so a page that grows LOGICALLY with the
window puts the same fixed content in a corner of it. On a 2560-wide display the whole
codex sat in the top left with two thirds of the screen black. Pinned, the content is the
same fraction of the screen everywhere and the lettering grows with the display.

The two scales still differ, and everything that spans them is now measured rather than
assumed:

* **The top edge.** `H6_TAB_H + 2` page pixels is the strip's height only by accident.
  Where they diverged the page rode UP behind the OPTIONS and DATABASE plates, its green
  border and the faction emblem passing underneath them. `codex_layout` converts the
  strip's real framebuffer height into page pixels and rounds up; G147 asserts
  `boxtop >= strip` in framebuffer pixels, from numbers the screen prints.
* **The plate's rectangle.** G147's plate leg was written against the page's scale and
  spent its first life measuring a rectangle the plate has never occupied -- green on a
  picture it never looked at, proven by a doctored pack scoring identically to a good one.
* **The pointer.** `update_cursor` had no branch for this page, so it ran a full pick
  against the terrain, decided the pointer was over the world, and drew the cartridge's
  3-D cursor IN the world -- underneath the overlay. `cnc_sidebar.h` had already written
  that defect down about the OPTIONS plate, in the same words: "the mouse cursor goes
  behind it". The page joins the panel branch and gets the plain arrow, which is what 1995
  shows over a modal dialog.

## 7. Driving it

Plate, keyboard, or script.

```
ESC / RETURN     close
UP / DOWN        move down the vertical axis
LEFT / RIGHT     change category
TAB              next category
wheel            move down the vertical axis
```

Every other key is swallowed while the page is up. A hotkey that queues a Mammoth Tank
while the player is reading about Mammoth Tanks is the class of bug that closes.

Script verbs, all of which go through the same functions the plate and the keyboard call:

```
codex open | close | toggle
codex settle                 the drawer's end state, for a script with no frame clock
codex cat N                  0..3
codex row N
codex house gdi|nod
codex state                  prints open, avail, house, category, row, code and the stats
```

G147 also asserts the STYLE, because "it matches the other menus" is a claim about every
pixel and exactly the kind that rots silently. It samples the interior of the list box and
the stats block -- two rectangles measured off the page's own green borders, containing no
3D model, no emblem and no filigree art -- and requires every pixel to be one of the nine
colours above. It currently reads an off-palette fraction of 0.0000. One `db_fill_rect`
with a grey in it fails it and names the intruder.

`scripts/codex.script` walks the whole screen and takes eight shots. It needs `--gfx` and
`CNC3D_HUD=new`; without them the tab is correctly absent and every line would be measuring
its absence, which is why the first line of the script prints `avail=`.

```
cd playable && CNC3D_HUD=new ../game/cnc_eyes --scen SCG01EB --pack SCG01EA.pack \
    --build 98 --gfx --script ../scripts/codex.script
```

## 8. Files

| file | what |
|---|---|
| `game/codex_mod.h` | the screen: layout, rasteriser, model viewport, input, music duck |
| `game/codex_table.h` | **generated**; 50 rows of 1995 numbers |
| `tools/database_codex/codex_data.py` | the GPL reader |
| `tools/database_codex/gen_codex_table.py` | codex.json -> the header |
| `tools/sidebar_redesign/tabs.py` | the DATABASE plate, same font and plate as OPTIONS |
| `tools/sidebar_redesign/bake_pack.py` | carries it into `hud640.pack` |
| `game/bake_logos.py` | now bakes EVA as a fourth logo |
| `game/cnc_sidebar.h` | `SBH_DATABASE`, the hit test, the plate draw, the greyed handle |
| `game/hud640.c` / `.h` | the label picks its own plate; `database_frame` |
| `game/dosopt.c` / `.h` | the four dialog widgets, exported so both screens share them |
| `game/cnc_eyes.cpp` | include, draw order, pause gate, modal input, script verb |
| `scripts/codex.script` | the walkthrough |
