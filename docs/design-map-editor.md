<!-- Written 24 Aug 2026, from a research pass over thirty years of C&C map editors,
     the settled UX of modern tile editors, how other RTS games author elevation tiers,
     and a measurement pass over this repo's own data. Every design claim carries a URL
     and every claim about the code carries a file and line. Where the evidence does not
     reach it says UNPROVEN, and those are the parts to be most careful with. -->

# THE CNC3D MAP EDITOR: THE PLAN

*Written for the project owner first, implementer second. Every design claim from the web carries a URL; every claim from the code carries a file and line. Where the evidence does not reach, it says UNPROVEN.*

---

## 0. DECIDED, 24 Aug 2026

Section 7 asked the project owner the questions that change what gets built. He answered all three,
so they are no longer open and the build order below is the one that follows.

1. **v1 must be PLAYABLE, not export-only.** The finish line is a map made in the browser
   appearing in the CNC3D skirmish lobby and starting. That is slice 2, and it needs no
   terrain editing and no baking: the map borrows a theater-matched pack, which is valid
   precisely because no tile was changed.
2. **v1 EDITS SHIPPED MAPS.** Objects and ownership on top of cartridge terrain. New-map
   authoring comes later, and it is the branch that terrain and elevation tools belong
   to, because a tier tool cannot open a shipped map without requantising up to 40% of
   its corners.
3. **Elevation: fix the pipe, defer the brush.** `bake_heights` gets its parameter so
   heights can reach the game at all (they cannot today, and the nine converted skirmish
   maps are flat in-game as a result), then PNG import/export of the 65x65 heightmap.
   The tier brush waits for the new-map branch.

The consequence for the build order in section 6: **slices 1, 2 and 4 are the committed
work, in that order, and slice 3 (terrain painting) moves behind slice 4.** Slice 4 is
split, because only its first half is cheap and it is the half that unblocks everything
else:

  4a. `bake5.build(scen, outpath, heights=None, cmvals=None)`, about five lines at
      `game/bake5.py:1647` and `:1657`. Highest value change in this document: until it
      runs, every elevation feature anyone proposes is unshippable, and the flatness of
      the shipped skirmish maps is invisible.
  4b. PNG round trip of the 65x65 heightmap on a five-value grey palette.

Two things to do before slice 1 writes a single byte:

* **Fix the height contamination at `app.js:988`.** `show()` sets `m.h = padHeights(m)`,
  so from that moment `MAP.h` is the renderer's cosmetic building pads and not the map's
  authored elevation. An editor that saved `MAP.h` would write pads to disk as terrain.
  Keep the authored heights in a field the renderer never touches.
* **Commit the viewer.** `tools/heightmap-viewer/` is untracked, and per repository rules rule 3
  untracked work does not exist. The editor is going to be built on top of it.

---

## 0b. BUILT, 24 Aug 2026

Slices 1 and 2 shipped, and then **terrain painting (slice 3) shipped ahead of slice
4**, out of the order section 0 decided. That was the project owner's call, made after using slices
1 and 2: he asked for terrain next. Slice 4a is therefore **still not done**, and its
consequence still stands: `bake5.build` has no `heights` parameter, so elevation cannot
reach the game and the converted skirmish maps are still flat in play. Terrain painting
does not depend on it (tiles and heights are different files) but nothing about the
flatness has changed.

What is in the editor now:

* **Objects.** Arm-and-stamp with the real mesh as a translucent ghost, footprint
  decals with invalid drawn over the ghost, bib correction, four-house owner selector,
  place/move/delete, undo/redo, the derived no-go wash, the linter, `.INI` writing and
  autosave.
* **Occupancy.** A building is refused where a building, a tree, a rock, a wall or a
  tiberium field already stands; the status line names what is in the way. Walls and
  tiberium refuse only a second one of themselves, so a drag paints across ground it
  has already covered without stuttering. Vehicles and infantry stand where they like.
* **Wall connection.** Recomputed after every edit, not only at load, using the
  engine's own rule (`CellClass::Wall_Update`) and the cartridge's 16-way variant
  table. Four wall cells in a square close into a square as the fourth is drawn.
* **An image-grid palette.** Every tile wears the thing it places. Anything buildable
  in the real game shows its **sidebar cameo** off the disc (53 types, packed into
  `public/data/cameos.png`); trees, rocks, tiberium, boats and the civilians never
  appear in a sidebar and have no cameo, so the editor **renders the actual cartridge
  mesh** to a thumbnail through the scene's own renderer. Names come from the brain's
  own text table (`conquer.h` paired with each type class), never invented.
* **Terrain painting**, on a semantic palette of eight drawers keyed to the
  cartridge's own template name families (Ground, Cliffs, Water, Shore, Rivers, Roads,
  Rock, Bridges) rather than a list of 216 numbered TemplateTypes. Each tile shows the
  template's **real icons composed at its real w x h**, cropped straight out of the
  theater atlas, so a 1x1 patch and a 5x4 river mouth are told apart at a glance.
  Water tiles are alpha holes on this hardware, so the thumbnail lays the game's own
  water texture underneath exactly as the console does, or every river and shore tile
  would be a blank square. Painting is anti-smeared (one block per cell crossed), a
  whole drag is one undo step, and the six templates whose icons the cartridge never
  shipped are not offered.
* **The `.BIN` writer.** Verified against the game's own `game/missions/SCG01EA.BIN`:
  **zero template mismatches over all 4,096 cells**, and every icon matches too except
  on plain-ground cells, where the icon only chooses which of the sixteen clear
  variants is drawn and the N64 conversion re-scattered them. Only written when a tile
  actually changed.
* **The bake warning.** Terrain is the one thing that cannot ride a borrowed pack: the
  engine reads collision from the `.BIN` and the renderer reads geometry from the pack,
  so an edited tile means units walk through cliffs you can see until the pack is
  rebuilt. The linter now fails on this rather than reporting the old constant "terrain
  unchanged".

Still open from the MUST/SHOULD lists: the eraser as a visible tool, flood fill and
variant randomisation, the CLI bake script, a user-maps source in `export.py`, PNG
round trip of the heightmap, and facing control on units.

---

## 0c. ELEVATION, CLIFFS AND BRIDGES, 24 Aug 2026

the project owner's instruction: *"Terrain painting should not use Cliffs. Cliffs should instead be
part of the terrain tool. So if someone decides to change the heightmap, and paint
that, the cliff edges should automatically appear there, with the option to place
ramps."* That inverts section 4a's question and answers it: the tier is authored, the
cliff and the heightmap are both derived from it. The Cliffs drawer is gone from the
terrain palette.

Everything below was measured over the 55 cartridge maps that ship their own
heightmap, restricted to each map's playable rectangle, and then re-derived
independently by a second pass before it was written down. Where the two passes
disagreed, the disagreement is recorded rather than smoothed over.

### The precondition, now met

`bake5.build` took no height override, so authored elevation could not reach the game
at all and the converted skirmish maps were flat in play whatever their author drew.
`bake_heights(scen, override=None)` and `build(scen, out, heights=None, cmvals=None)`
now exist, validate length and range, and leave the ROM path byte-identical when no
override is passed. Without this the tier tool would emit cliff art onto terrain that
bakes flat, which is the one thing the cartridge itself essentially never does (3.6%
of its SLOPE cells).

### The ladder

`RUNG = [0, 64, 128, 191, 255]`, tier = nearest rung, ties to the lower. Of 151,325
playable corners, **58.56% sit exactly on a rung and 99.05% within 31 of one**.
Ground is rung 1 (64), not rung 0 (which holds 1.32% of corners), so the brush calls
rung 1 "Ground".

### The compass

A 2x2 block, four bits for its four outer corners, set when that corner is above the
block's own lowest. Twelve of the sixteen masks map to a template; two are flat and
two are diagonal saddles the tool refuses. Shares below are P(mask | template) on the
block lattice. The inverse, P(template | mask) — which is what a lookup table is
really judged by — runs **67.0% to 90.6%**, and the residue is always an end-cap
template this table deliberately never emits.

| mask | corners | meaning | template | share |
|---|---|---|---|---|
| 1 | H L L L | outer corner NW | S32 | 98.8% |
| 2 | L H L L | outer corner NE | S29 | 96.1% |
| 3 | H H L L | straight, high N | S03 / S04 / S05 | 95.3% |
| 4 | L L H L | outer corner SW | S31 | 95.5% |
| 5 | H L H L | straight, high W | S25 / S24 / S26 | 97.9% |
| 6 | L H H L | **saddle — refused** | — | n=5 |
| 7 | H H H L | inner corner NW | S34 | 88.9% |
| 8 | L L L H | outer corner SE | S30 | 89.2% |
| 9 | H L L H | **saddle — refused** | — | n=5 |
| 10 | L H L H | straight, high E | S11 / S10 / S12 | 97.6% |
| 11 | H H L H | inner corner NE | S35 | 89.0% |
| 12 | L L H H | straight, high S | S17 / S18 / S19 | 96.5% |
| 13 | H L H H | inner corner SW | S33 | 86.9% |
| 14 | L H H H | inner corner SE | S36 | 95.2% |

Where three siblings are listed they are interchangeable and all above 91%; one is
picked per block by a hash of its coordinates, so a long cliff is not visibly tiled
and the same map always redraws identically.

**Never auto-emitted.** S01, S07, S08, S14, S15 and S28: block-lattice modal share
40.4%–73.5%, because they are ramp end-caps rather than plain runs. S02, S06, S09,
S13, S16, S20, S21, S22, S23 and S27: not 2x2, so they cannot land on the lattice.
S37 and S38: saddles, n=5 each in the whole cartridge.

### Heights

Corner heights come from bilinear interpolation across each block. Verified against
the cartridge: the median error of its own mid-edge corners against this formula is
**+0.000** over 12,964 samples, and of the block-centre corner **−0.008**. A north
cliff from tier 2 to tier 1 comes out 128 / 96 / 64; the cartridge's own medians for
the three north siblings are 122/96/67, 122/95/66 and 126/97/65.

### Ramps

Only 11 of 38 SLOPE templates carry a walkable notch and **S14 is the only one that is
8-connected crossable in both axes** — the only self-contained ramp block in the game,
and it faces east. So the tool offers two ways up:

* **Graded pass** (any direction): leave the cliff art off the block and let the
  corner heights carry the climb. This is what the cartridge does on **81.4%** of the
  5,438 passable edges where it gains a tier.
* **Ramp** (east only): dress it with S14. Its icon 3 has no art in either bank; the
  cartridge fills that cell with plain CLEAR1, which is passable anyway, and so does
  this.

The reachability linter must be **8-connected**: shipped maps have a median largest
passable component of 93.7% 4-connected against 99.6% 8-connected, and a 4-connected
check would fail the cartridge's own maps (SCG14EA 42.5% → 98.9%).

### What the tool will not do

* **It will not touch a map until you say so.** 41.44% of playable corners across the
  55 heightmapped maps are off-rung; painting quantises them. The panel says so and
  requires one click before anything is editable.
* **It only owns what you paint.** An earlier version rebuilt all 1,024 blocks from
  the tier field, which quantised heights nobody had touched and — far worse —
  overwrote the sea, the shore, the roads and the tiberium with plain ground, because
  a tier field knows about steps and knows nothing about water. It now writes only the
  painted blocks grown by one in each direction (a mask is read off corners shared
  with its neighbours), and only the corner heights those blocks own.
* **It refuses three things outright**, with the reason in the status line: a step of
  more than one rung (no art exists; terrace it), a diagonal saddle (n=5 in the whole
  cartridge), and a low area narrower than three blocks (raised ground claims the
  cliff outside itself, so a narrow channel would silently fill in — 1.26% of shipped
  playable cells sit in exactly such a channel).

### Bridges

the project owner: *"they should only be possible to place on proper rivers where they fit."*

The first rule derived for this was thrown out under adversarial check: its counts did
not reproduce, its mouth table listed deck cells as water and omitted water cells, and
its endpoint test could never fail because 100% of endpoint neighbours are CLEAR. What
replaced it is a single measurement over the 221 complete bridge and ford placements
in the 100 shipped maps: the (icon, direction) pairs where the cell just outside the
footprint is water in **every** instance.

| template | mouths | unanimous over |
|---|---|---|
| BRIDGE1 | icon 4 W, icon 8 W, icon 15 E | 24 / 24 |
| BRIDGE2 | icon 9 E, icon 10 W, icon 15 W | 27 / 27 |
| BRIDGE3 | icon 12 W, icon 23 E | 15 / 15 |
| BRIDGE4 | icon 12 W, icon 18 W | 26 / 26 |

BRIDGE4's eastern mouth is 25 of 26 and is deliberately left out: a rule that is
unanimous is evidence of intent, one that is 96% is evidence of a habit. **FORD1 and
FORD2 get no rule at all** — not one (icon, direction) pair is unanimous across their
147 placements, and refusing to guess is the point.

A crossing is placed by a click, never smeared by a drag: once the first one lands its
own water cells would satisfy the mouth test one cell along, and a held drag would
chain bridges across dry ground. The destroyed variants BRIDGE1D–4D are not offered;
two have no art in either bank and the only *D cells in any shipped map are fragments
of a bridge something blew up mid-mission.

### Infantry now use the 1995 DOS sprites

The viewer drew infantry as the cartridge's billboards: five stored facings mirrored
to eight. The PC game stores all eight and mirrors nothing, and CNC3D itself already
ships the DOS art (`game/dosinfantry.pack`), so `tools/heightmap-viewer/export_dosinf.py`
bakes the same sprites out of the same `CONQUER.MIX` with the same `TEMPERAT.PAL`,
importing the game's own `bake_dosinfantry.py` tables rather than retyping them. The
facing pick is the engine's: `HumanShape[Facing32[dir & 255]]`, carried in the JSON so
the browser and the game cannot drift.

Verified texel for texel: 51,024 comparisons against frames decoded fresh from
CONQUER.MIX, zero mismatches, and all 20 crop boxes identical to the shipped pack's
manifest. One bug was caught by the adversarial pass and fixed: the Nod colour band
was being applied to the 13 neutral types, which the game never does — C3 wears
palette 176..191 and a Nod-owned C3 stays gold in the real game. Fixing it also halved
the sheet to 256x256, because a neutral type's two house rows genuinely are one strip.
The assertion now runs in both directions, since the missing half is the hole the bug
went through.

### Still open

The 8-connected sealed-plateau check does not run in the linter yet. The `.HGT` file
the editor now writes has no CLI baker beside it — the recipe is in the save panel but
a one-command wrapper would be better. And whether a 2x2-locked tier brush feels good
to use is the project owner's call, not a measurement.

---

## 0d. THE LAYOUT, REBUILT — 24 Aug 2026

The editor had been growing inside the heightmap viewer's chrome: two permanent side
columns, the 100-map browser as prominent as the tools, and the palette three tiles
wide in a 221px slot. Five directions were drawn as working mockups (a game-sidebar
direction, a Photoshop-style tool rail, floating islands over a full-bleed viewport, a
two-screen browse-then-build split, and a radial menu at the cursor). the project owner picked the
first, and the editor was rebuilt to it.

**The organising idea: the editor speaks the game's own language.** A tall right-hand
sidebar with the cameo grid as its centre of gravity, a radar at the top, and the
loudest control on screen being the one that changes everything — the three edit
modes. What it borrows from C&C is the *structure*, not the 1995 beige.

What changed, and why each one:

* **The viewport went from ~60% of the window to ~80%.** Both side columns are gone;
  the sidebar is one column that clamps between 340 and 460px.
* **The map browser is a tab, not a wall.** You pick a map once a session; it now
  lives behind a tab that takes the whole sidebar while it is open and steps aside
  when it is not.
* **A radar, which the editor never had.** 64×64 cells drawn one texel per cell from
  the engine's own land types, shaded by height, with every object as a dot in its
  owner's colour. Drag it to fly the camera. Three "jump to" buttons find each side's
  centre of mass and the tiberium.
* **The status line became a card in the viewport.** Three fixed lines — what is
  armed, whether it can go where the pointer is, and the keys — with the middle line
  the only one that ever turns red. It used to be a strip of text under the viewport
  with no visual link to the thing it described.
* **The palette is five tiles wide** with the game's own cameo on every tile, a code
  badge, and the footprint in cells.
* **There is no view mode any more.** The left mouse button belongs to the editor from
  the first frame; the camera keeps right-drag, the wheel and WASD.
* **A tool rail with only tools that work**: select/place, move, eyedropper, erase,
  and frame-the-map. A rail of greyed-out promises is worse than a short rail.
* **The GDI eagle in the corner turns on its axis** — the cartridge's own emblem
  (`GDI.png`), cropped square by `export_editor.py`, as two faces and a gold edge in
  CSS 3D. The edge is there because without it a flat sprite vanishes to a hairline
  twice per turn and reads as a flicker rather than a struck coin.

**The rebuild was reviewed adversarially and it needed it.** Thirty-three agents over
three lenses — lost function, dead wiring, layout — found 42 defects, of which the
serious ones were: all four Surface buttons silently unwired (the rebuild renamed
their container and gave the old id to the mode tabs, so `#modes button` matched
nothing and the Tiers view was unreachable by any input at all); the view bar clipping
its own right-hand third at 1280px, taking the vertical-scale group and the entire
class-visibility popup with it; the Map info tab growing until it pushed the save
button off the bottom of the screen with no scrollbar to reach it; the status card
still nailed to the mockup's absolute pixel coordinates; the asset-class chips
rendering lit regardless of state because the mockup's CSS used `.off` and the JS
toggles `.on`; and Undo/Redo staying disabled after an edit because the only code that
re-enabled them ran from `ui()`, which an edit does not call. All are fixed and each
fix is verified in the live page at 1280×800, 1600×1000 and 2560×1440.

The four directions not chosen are kept as working HTML in the scratch work;
they are worth a look before the next big UI decision rather than re-deriving them.

---

## 0e. THE EDITOR GOES NATIVE — 24 Aug 2026

the project owner, after seeing the browser editor working: *"I dont want this to be a browser editor
in the end. In the end it has to be a native Windows and Mac application"*, and *"Play in
editor should launch the map inside the editor window. Not separately."*

Those two together settle the architecture, and they retire a plan made an hour earlier.
Play-in-editor was about to be built as a frame-streaming bridge: run the game headless,
pipe frames into the browser viewport, pipe input back. That bridge exists only to span a
browser gap. In a native application there is no gap — the sim renders into a panel of
the same window — so every line of it would be thrown away, and it is the fiddliest work
on the board.

**The end state is one native application: cnc_eyes with an edit mode.** It already
renders the whole world from a baked pack, ticks the GPL brain, and builds for macOS and
for Win98/Windows. It also already has the editor's hardest interaction, written in C++:

* `game/cnc_sidebar.h` — "the buildable list with its cameo art, and the placement cursor
  with the engine's own valid/invalid cell overlay". That is arm-a-thing-and-see-where-
  it-can-go, done.
* `game/fx_panel.h` — a working panel with toggles, sliders and a SAVE button.
* `game/dosbar.h`, `game/dosopt.h` — 8-bit drawing and dialog primitives.

### What the browser prototype was for, and what survives it

It was not wasted and it should not be mourned. Its job was to settle the design and to
derive the rules against the cartridge, and both are done and adversarially verified.
What ports:

* **`editor_tables.json`** — the ground table, 216 templates, the per-theater slot
  tables, the cameo index, display names, scenery occupy lists, sea-hole slots. Generated
  by Python from EA's GPL source and the cartridge's own banks. The native editor reads
  the same file, or it gets baked into the pack.
* **The rules**, all measured and all recorded in this document: occupancy and the
  five-per-cell infantry exception; wall connectivity through `CellClass::Wall_Update`;
  the scenery occupy offsets that put a tree a row south of its stored cell; the twelve
  cliff masks and their templates; the bridge mouths; the tier ladder and the bilinear
  corner rule.
* **The writers** — `.INI`, `.BIN` and the 4,225-byte `.HGT` — which are small and
  mechanical.
* **The layout**, approved as draft 1: a game-shaped sidebar, cameo-first, with the mode
  strip as the loudest control and the status card in the viewport.

What does not port is three.js and the DOM, and neither needs to: the game renders the
world better than the browser did.

### What stays useful in the meantime

`bake5 --heights` and `stage-skirmish-maps.sh --from` are not browser features. Any
editor, native or not, has to turn an authored map into a pack, and that path is now
proved to carry authored elevation one byte per corner. `serve.py` and its Play button
are a stopgap: they bake and launch the game in its own window, which is useful today and
becomes redundant the moment edit and play share one window.

---

## 1. THE ONE-SCREEN PITCH

The map editor is the heightmap viewer with a write path bolted on: the same 3D scene, the same cartridge art, the same orbit camera, but now you can put things in it and get a file out. You pick a thing from a palette on the left, a ghost of it follows your cursor across the terrain snapped to the cell grid, you click, and it is placed, owned by whoever the big coloured button at the top says. A hatched wash tells you where nothing may stand, and it is not our opinion: it is the game engine's own table, read cell by cell out of the GPL source. Everything is undoable, everything autosaves, and one button writes the mission file. The core loop is: **arm a thing, point at ground, click, look, undo if wrong, save, play.**

Terrain painting joins that loop unchanged in slice 3: the palette just gains blocks of ground instead of buildings. Elevation does not join it in v1, and section 4 says why in detail.

---

## 2. WHAT IT FEELS LIKE TO USE

the project owner opens `localhost:8931`, which is the same page he already looks at maps on. There is a new button in the corner: **Edit**. He clicks it.

The camera stops responding to the left mouse button. That is the first thing he notices, and it is deliberate: the left button now belongs entirely to the editor, and the camera moved to the right button and the wheel. The status line at the bottom, which until now said things like `cell 31,44 SLOPE3 icon 2, corners 64 64 128 128`, now says: `Nothing armed. Click a palette item to place. Right-drag to look around.`

A panel has appeared on the left. It has four tabs across the top: **Buildings, Units, Infantry, Scenery**. Above them is a row of four fat colour chips: **Yours** (gold), **Enemy** (red), **Neutral** (grey), **Special**. Gold is lit.

He is looking at SCM01EA, a multiplayer map, so it is empty of everything but trees. He clicks **Buildings**, and a grid of the game's own building thumbnails appears, every one of them drawn in GDI gold because gold is the active side. He clicks the **Construction Yard**.

A translucent gold Construction Yard appears under his cursor, standing on the ground, tilted to nothing, following him as he moves the mouse across the map. Under it, four cells light up faintly green: its footprint. He drags it over a patch of rock at the edge of a cliff and three of those cells go red, and the red draws *over* the building rather than behind it, so it punches through the art and he can see exactly which corner is the problem (this is OpenRA's trick, `FootprintOverPreview = PlaceBuildingCellType.Invalid`, https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/Traits/Buildings/ActorPreviewPlaceBuildingPreview.cs). The status line now reads: `GDI Construction Yard, 3x2 plus bib. Cannot place: rock at 22,17. Click to place, Tab changes side, Esc cancels.`

At the same moment, and without him asking, a fine diagonal hatch has faded in over about a fifth of the map: every cell where nothing can be built. It appeared when he armed the brush and it will fade out when he disarms. He can see the whole cliff line and the whole shore, and he can see the *gaps* in the cliff line, because the ramp cells are not hatched. That is not a nicety. It is the engine's own answer, per cell, per icon, and it is why the map he is about to make will be playable.

He moves left onto flat ground. All four cells go green. He clicks. A real Construction Yard appears, solid gold. The ghost is still on his cursor: the brush stays armed, the way it does in OpenRA and in the 1994 editor. He clicks four more times and there are five Construction Yards, which is silly, so he presses **Ctrl+Z** five times and they vanish one at a time.

He places one properly, then a Power Plant next to it, then a Barracks. He clicks the red **Enemy** chip. Every thumbnail in the palette turns Nod red, and so does the ghost still hanging on his cursor. He walks to the far corner of the map and lays down the same three buildings in red. He never touched an ownership dialog.

He switches to the **Scenery** tab and picks Tiberium. Now he holds the left button down and drags, and tiberium paints along the drag like a brush stroke rather than needing a click per cell. He fills two patches, one near each base.

He notices a tree sitting exactly where he wants a refinery. He presses **Esc** to disarm, then clicks the tree. It gets a white box around it. He presses **Delete**. There is also a visible eraser tool in the palette, because "how do I delete this" is the single most-missed thing in the official Remastered editor's own forum threads (https://steamcommunity.com/app/1213210/discussions/0/2290590708534653641/), and hiding it on the right mouse button is how you get those threads.

Then he grabs a whole Barracks and drags it three cells left. It moves with him, gripped where he took hold of it rather than snapping its corner to the cursor, and it does not budge at all until he has moved a few pixels, so his ordinary clicks never nudge anything.

He clicks **Save**. A panel drops down and says, in words:

```
SCM01EA  Skirmish, TEMPERATE
2 start positions   OK
GDI base pad at 12,40 clear 3x3   OK
Nod base pad at 51,20 clear 3x3   OK
Tiberium on 34 buildable cells    OK
Playable rectangle 1,1 62x62      OK
Terrain unchanged: pack will be borrowed from SCM01EA
Download SCM01EA.INI
```

That list is the linter, and it is the most valuable thing on this page. Every failure it catches is otherwise completely silent: a map with no pack does not error, it simply never appears in the menu (`app/cnc3d.cpp:288-292` and `:396-400`, both of which comment that an unplayable entry is worse than an absent one). An illegal tile is not rejected, it is quietly turned to clear (`brain/vanilla/tiberiandawn/map.cpp:1157`). Nothing anywhere cross-checks that the pack's theater matches the INI's.

He downloads the file, drops it into `playable/missions/`, launches CNC3D, and his map is in the skirmish lobby.

Twenty minutes, and he has a playable skirmish map.

**What is not in that story, and when it arrives.** Terrain painting arrives in slice 3 and slots into the same loop: the palette gains a **Ground** tab with six entries (Clear, Rough, Road, Water, Shore, Cliff), the ghost becomes a 2x2 or 2x3 block of tile art instead of a building, and the same drag, the same anti-smear, the same undo. Raising and lowering ground is not in v1 at all, and when he clicks where he expects it the tool should say so plainly rather than offering a broken version. Section 4 explains that decision.

---

## 3. THE MVP FEATURE LIST

### MUST

| Feature | One line |
|---|---|
| Arm-and-stamp ghost using the real mesh at 50% alpha | The pointermove raycast and `groundAt` already do the snapping (`tools/heightmap-viewer/public/app.js:620`, `:1098-1126`); this is the cheapest item on the board and it is literally what the project owner asked for. |
| Footprint snap with per-cell valid/invalid decals, invalid drawn over the ghost | Validity is a mixture, never one verdict (`brain/vanilla/tiberiandawn/cell.cpp:1114-1123` picks TRANS.ICN frame 0 or 2 per cell), and drawing red on top is one `renderOrder` field for the whole readability win. |
| Bib correction on the footprint: `Width x (Height+1)` when `IsBibbed` | `brain/vanilla/tiberiandawn/bdata.cpp:4467-4494`; without it a 3x2 Construction Yard looks legal in the editor and is refused in game. |
| Four-slot owner selector recolouring palette and ghost instantly, plus Tab to cycle | Measured: no shipped map uses more than four houses (distribution 0:16, 1:2, 2:31, 3:48, 4:3), so a ten-slot Multi1..6 UI is dead weight. |
| Place, delete, move; move gated by a drag threshold and preserving the grab offset | OpenRA's `MinMouseMoveBeforeDrag = 32` exists so ordinary clicks do not nudge objects (https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/EditorBrushes/EditorDefaultBrush.cs). |
| A visible eraser tool, not only right-click | "How do I remove things" is the top complaint in the Remastered editor's own forum (https://steamcommunity.com/app/1213210/discussions/0/2290590708534653641/). |
| Undo/redo as Do/Undo action objects, one per edit, one per drag | Thirty lines now, a rewrite later; FA2's undo was patched from 15 steps to unlimited to per-multiselect over three years (https://raw.githubusercontent.com/secsome/FA2sp/master/CHANGELOG.md). |
| Derived no-go hatch, per `(template, icon)`, auto-shown while armed, pinnable on a key | The rule is `CellClass::Recalc_Attributes` (`cell.cpp:479-527`) plus `Ground[]` (`const.cpp:245-258`), and it is already written in Python in this repo at `game/missions/make_skirmish_map.py:246-297`. |
| Mode-aware status line | Mobius's bottom bar is the one genuinely good idea in the official editor and it costs nothing (https://raw.githubusercontent.com/Nyerguds/MobiusMapEditor/master/MANUAL.md). |
| `.INI` writer with a download button | This is the feature. There is currently no write path of any kind in the viewer: no Blob, no download, no localStorage, no POST anywhere in `app.js` or `index.html`. |
| The linter, run on every save | Highest value per hour on the whole board, because every failure it prevents is silent. |
| Autosave to localStorage | The browser tab is the entire application. Ten lines. |
| Ship the slot to `(template, icon)` table (973 + 758 entries, ~3.5 KB) in `index.json` | Unblocks the no-go overlay and everything terrain-related. |
| Fix the `padHeights` contamination before any write path exists | See the data contract; this is a silent data-loss bug waiting for a save button. |

### SHOULD

| Feature | One line |
|---|---|
| Terrain art stamping on a six-entry semantic palette, never a 216-item TemplateType list | Every complaint thread in thirty years of these tools is ultimately about being made to think in tile ids; FinalSun's own palette is six semantic types (FSLanguage.ini `GroundObList`, https://web.archive.org/web/20220628053535id_/https://rampastring.cnc-comm.com/tsupdates/Map%20Editor/FSLanguage.ini). |
| Anti-smear drag, variant randomisation, flood fill, block-aware eraser | Mobius has all four as explicit options because multi-cell template painting is hard, and the eraser must clear the template's real occupy cells rather than its bounding box (https://raw.githubusercontent.com/Nyerguds/MobiusMapEditor/master/CHANGELOG.md v1.4.0.2). |
| `.BIN` writer plus the CLI bake script | The moment one tile changes, the borrowed pack is a lie and the map must be baked. |
| A user-maps source in `export.py` | It hard-filters its corpus to `SCG`/`SCB` prefixes (`tools/heightmap-viewer/export.py:405-413`), so the editor currently cannot reopen its own output. |
| PNG import/export of the 65x65 heightmap | The cheapest possible elevation answer, and demonstrably the workflow the original artists used (the 191 rung beats 192 thirteen to one, `docs/terrain-elevation-grammar.md` section 1). Ship this *instead of* a height brush. |
| Eight-step facing on units and infantry via Ctrl+wheel | Measured: only `{0,32,64,96,128,160,192,224}` ever occur, and 96.3% of structures face 0, so structures need no control at all. |

### LATER

Corner-mask cliff autotiling on new maps only, with the ramp swap in the same commit or not at all. Connected-tiles path drag for cliffs. Tier painting with derived art and derived heights. Water and shore painting. Region copy/paste. Visible undo history list. Agent-drivable command layer (free if the commands are a clean data layer over the two arrays; https://github.com/Rampastring/TSMapEditor/releases v1.9.0).

### KILLED, including where that is surprising

- **The AutoLevel reconciler, and the "where do art and height disagree" checker.** One thread called this "the core feature" and "not optional". It is neither, and this is the most surprising cut on the list. Under a tier-first architecture the two layers are both derived from one authored layer and cannot disagree, so the feature is dead code. Under an art-first architecture it is not implementable: the modal per-`(template, icon)` corner-delta stencil explains 4.4 to 4.9% of the roughly 10,000 to 20,000 shipped SLOPE cells, and only 61 to 65% even after quantising the deltas to 64-byte buckets. It either does nothing or it invents numbers and calls them cartridge grammar.
- **Free-form or per-corner height editing.** FinalSun has shipped it for twenty-five years labelled "Raise single field (Not recommended!)" in its own string table. That is the verdict.
- **Any rule engine** (LDtk auto-layers, Tiled AutoMapping, Unity Rule Tiles). You would build a system to discover rules that have already been measured, and then need a rule-ordering UI and a "why did my rule not fire" debugger. Tiled's own terrain page calls this path "more flexible, but also more complicated" (https://doc.mapeditor.org/en/stable/manual/terrain/).
- **Blob / 47-tile / Wang edge sets, transition chains, probability tuning UI.** The art is corner-matched and has two terrains. No tiles exist for the other families.
- **Selection algebra, magic wand, mirror tools, stamp slots.** Built for maps too big to see. This one is 4096 cells on one screen. Note that Mobius still lists copy/paste as a future feature after four years, so its absence is survivable.
- **A generic layer stack with eye/lock/opacity.** The four layers are not peers. Let the selected tool be the layer, porymap-style (https://huderlem.github.io/porymap/manual/editing-map-collisions.html).
- **Slope as a hard no-go.** OpenRA does this and it is right for OpenRA. Here it would forbid an extra 13% of every map, about 532 cells, and contradict `padHeights` (`app.js:573`), which grades pads on purpose.
- **Three separate pathing overlays in the StarCraft II style.** Buildable and passable differ on about 0.5% of cells here. One hatch plus a lighter beach wash.
- **The bridge/ford build ban** that most C&C editors reproduce. It is inside `#ifdef ADVANCED` at `brain/vanilla/tiberiandawn/cell.cpp:438-458` and `defines.h:44` reads `//#define ADVANCED`. Porting it would make the editor disagree with the engine.
- **Native HTML5 drag-and-drop.** It cannot repaint the drag image as the ghost snaps, which is the one thing this needs (https://www.sam.today/blog/html5-dnd-the-api-that-is-gaslighting-you).
- **Per-object instancing / incremental mesh rebuild.** `buildAssets` merges everything into one geometry (`app.js:652`), and there are about 200 objects on a map. Rebuild the lot on mouse-*up*, not mouse-move, and the refactor disappears. Measure before optimising.
- **In-browser pack baking.** 7.7 of every 7.8 MB a pack contains is global. Keep it a CLI step.
- **"Beginner mode" that hides features.** FinalSun's own community tutorial tells new users to turn it off first (https://forums.cncnet.org/topic/9934-finalsun-complete-tutorial/).
- **Triggers, teamtypes, taskforces, celltags.** A separate product. This is where every editor in this family drowns.

---

## 4. THE HARD PARTS

### 4a. Cliff autotiling

**Simplest defensible answer.** Do not autotile from art. Author a **tier** on a coarse grid, take each coarse cell's four corner tiers, form a four-bit high/low mask, and look the direction up in a twelve-entry table. This is marching squares, and it is not an approximation of the cartridge's art: it is a restatement of it. The measurement, taken over the 55 maps that ship a heightmap and restricted to each map's playable rectangle, is that the modal mask per template is exactly the compass already written in `docs/terrain-elevation-grammar.md:184-194`: HHLL north, LHLH east, LLHH south, HLHL west, the four single-high cases are the *outer* corners SLOPE29 to 32, the four single-low cases are the *inner* corners SLOPE33 to 36. The outer/inner identification is new and should be written into the grammar doc regardless of whether the tool ever ships.

**What it gives up, honestly.** Three things.

1. **The confidence is not uniform.** The mean modal share is 85.2%, but the minimum is **37.5%**: SLOPE8 at 37.5%, SLOPE22 at 44.4%, SLOPE15 at 52.6%, SLOPE1 at 55.6%, SLOPE7 at 62.2%. Those five are almost certainly end-caps or transition pieces rather than plain runs, and nobody has measured their neighbour context. **Emit only the high-consistency siblings (SLOPE2-5, 9-13, 17-19, 23-27) and never emit SLOPE1/8/15/22.** The compass is reliable for *direction*; it is not reliable as a unique template choice.
2. **Six templates cannot be placed at all.** SLOPE6, 14, 16, 20, 28 and 32 have icons missing from the cartridge's own tile bank, identically in both theaters. Blacklist them; each has a same-direction sibling. Note the cruel detail: **SLOPE14 carries three walkable ramp icons, the most of any cliff, and one of them is among the missing.**
3. **It cannot open a shipped map.** Blocks are not on a two-cell lattice (anchor parity is near-uniform: `(1,0) 2583, (1,1) 2609, (0,0) 2353, (0,1) 2463`) and 21 to 27% of shipped cliff blocks are partly overwritten by a later stamp. The atomic operation is "stamp a WxH block at an arbitrary cell, overwriting"; block identity on read is a hint, never a fact.

**The trap that will bite regardless of approach.** Only **11 of 38** SLOPE templates carry a walkable LAND_CLEAR notch, and **none of the ten corner pieces does**. An autotiler that always picks the plain variant seals every plateau it makes, and it will look perfect in the 3D view. So: the ramp swap ships in the same commit as the cliff tool, and a flood fill over the derived per-`(template, icon)` LandType runs in the linter and refuses to save a sealed plateau.

**Escape hatch.** If the tier tool turns out worse than expected, fall back to what the community actually does: region copy/paste of a working cliff formation out of a shipped map, height offset included. That is the documented FinalSun workflow, prominent enough to have its own tutorial chapters (https://forums.cncnet.org/topic/6410-final-sun-video-tutorials/), and WAE made paste height-aware with PageUp/PageDown for exactly this reason.

### 4b. The height grammar

**Simplest defensible answer: do not ship elevation editing in v1 at all.** Three independent facts force this.

1. **It cannot reach the game today.** `game/bake5.py:259` defines `bake_heights(scen)` with **no parameter and no override**: it looks the scenario name up in the ROM and falls back to `FLAT.IMG`, 4225 zero bytes, when the name is not a cartridge one. `build(scen, outpath, verbose=True)` at `game/bake5.py:1507` calls it at `:1647` with nothing but the name. I verified the consequence on shipped artefacts rather than by reading: `playable/SCM01EA.pack` and `SCM03EA.pack` each contain exactly **one distinct height value, 0**, in their 4225-byte heightmap block, while `SCG01EA.pack` contains 68 values from 28 to 130. **The nine converted skirmish maps are flat in the game right now.** Any elevation feature shipped before this is fixed is unplayable by construction.
2. **Art to height is not recoverable.** See 4a. There is no rule.
3. **Height to art is clean, but a tier model cannot round-trip a shipped map.** Over the 55 heightmapped maps, inside the playable rectangle only: 148,495 corners, **255 distinct byte values, 58.7% exactly on the 0/64/128/191/255 ladder, 72.8% within four**. The first time the project owner opens `SCB01EA`, raises one square and saves, a tier tool must requantise up to 40% of that map's corners in places he never touched. He will read that as the editor being broken and he will be right.

**What that gives up.** The half-rung insets, the deliberate art/height departures two shipped scenarios actually use (SCG05EA and SCG05WA have byte-identical tile maps and 2259 differing corners, `docs/terrain-elevation-grammar.md` section 6), and per-variant cliff choice.

**Escape hatch, and it is cheap.** PNG import/export of the 65x65 heightmap on a five-value grey palette. It is how the original artists worked, it is how Spring and Beyond All Reason still do elevation (https://springrts.com/wiki/Mapcomponents:_heightmap), it is a power tool we do not have to build, and it is the only way to express anything the tier tool cannot. Ship the PNG round trip before the height brush, and possibly instead of it.

**When elevation does come back, the policy is:** tier authoring on **new maps only**, art and heights both derived from the tier layer (the StarCraft 1 ISOM pattern, http://staredit.net/wiki/index.php/Terrain), the departure from the cartridge registered in `known-gap notes` per this repo's own rule, and the UI saying so in words rather than letting the project owner discover it.

---

## 5. THE DATA CONTRACT

### Reads

**Per map, `tools/heightmap-viewer/public/data/<SCEN>.json`, about 24 KB, all arrays base64:**

| field | shape |
|---|---|
| `scenario`, `theater` | strings; theater is `TEMPERAT` or `DESERT` |
| `playable` | `[X, Y, Width, Height]` from `[MAP]` |
| `heights` | 4225 bytes, 65x65 per-corner, row major. World Y = byte x 4; cell pitch 256 |
| `tiles` | 4096 uint16 little-endian **atlas slot** ids |
| `tmpl` | 4096 bytes, TemplateType only, 255 = clear |
| `objects` | 8-byte records; `kind = b & 15`, `house = b >> 4` (`app.js:639`, `:730`) |
| `holes` | per-cell bitmask of sea cells, precomputed server-side from atlas alpha |
| `atlas`, `atlasWidth/Height`, `tileSize`, `gutter`, `cols` | atlas geometry; `col = slot % 32`, `row = slot / 32`, 24 px tile, 1 px gutter, 26 px pitch (`app.js:264-268`) |

**Global, `index.json` plus `models.json` / `models.bin` / `assets.bin` / `<theater>_tiles.png`.** Already carries `templates` (216 names), `familyOf`, `houses`, `objectTypes`, and `footprints` (66 buildings, raw `BSIZE_wh` from `tools/heightmap-viewer/export_assets.py:134-143`).

**Four additions the editor needs, all static, all small:**

1. **Slot to `(template, icon)`, per theater.** DESERT 945 4bpp + 28 8bpp = 973 slots; TEMPERAT 315 + 443 = 758. Both are **perfect bijections**, zero collisions, verified by inverting the theater TL4/TL8 tables. About 3.5 KB. **This is not a re-export.** `export.py:450` does drop the icon (`t = ti[0] if ti else 255`), but the *slot* is already shipped in every per-map JSON and the slot recovers the pair, so the fix is one static file, not a hundred regenerated maps. Three of the five research threads called this a blocking prerequisite; it is a download.
2. **A 216-row template table** from `brain/vanilla/tiberiandawn/cdata.cpp` (ctor at `:1715-1733`, ordered by `Pointers[]` at `:1475`): `{ini name, theater mask, Land, Width, Height, AltLand, AltIcons[]}`. 216 Pointers entries, 216 unique constructors, no aliasing. `game/missions/make_skirmish_map.py:246` already parses this and returns 216 entries; it just does not keep Width and Height, which is a one-line change to the tuple.
3. **The per-theater holed-slot set**, replacing the per-map `holes` bitmask, because any terrain edit invalidates the latter. `hole_slots(root)` already exists in `export.py`; it is simply not shipped.
4. **The `IsBibbed` flag per building type.** Not every building has a bib (`bdata.cpp:4467`, gated on `IsBibbed && !Special.IsRoad`) and the viewer's data does not carry it at all.

### Writes

**`<CODE>.INI`, plain text.** `[Basic]` with `Name=` and `Player=` (defaults to `HOUSE_GOOD` if absent, `scenarioini.cpp:369`). `[MAP]` with `Theater=` and all four of `X/Y/Width/Height` (the MEGAMAPS defaults are `X=1 Y=1 W=126 H=126`, wrong for a 64-space map). Note that **theater lives in `[MAP]`, not `[Basic]`** (`display.cpp:1291`), and `n64_terrain.py` matches `THEATER=` anywhere in the file, so a misplaced line bakes one theater and plays another with nothing cross-checking. Then `[TERRAIN] cell=TYPE`, `[STRUCTURES] idx=house,TYPE,health,cell,facing,trigger`, `[UNITS]` the same plus a mission, `[INFANTRY]` the same plus a sub-cell, `[OVERLAY] cell=TYPE`, `[SMUDGE] cell=TYPE`, `[Waypoints]`. Cells are `y*64 + x`.

**`<CODE>.BIN`, exactly 8192 bytes**, 4096 pairs of `(TType, TIcon)`. Only written when terrain art was edited.

**An editor-native project file** (whatever JSON is convenient) so a work in progress survives, since the INI and BIN together are lossy about intent.

### What has to happen for the map to be PLAYABLE and not merely viewable

Three gates, and the third is where maps die silently.

1. **The name must match a scanner pattern.** Skirmish: `SCM%02dE[ABC]`, n from 1 to 98 (`app/cnc3d.cpp:375-405`). Spec Ops: `SC[GB]%02d[EW][ABC]`, n from 20 to 89 (`app/cnc3d.cpp:265-292`). Anything else is invisible.
2. **`<CODE>.INI` and `<CODE>.BIN` go in `playable/missions/`.** A missing `.BIN` is silent: the map loads all-clear.
3. **`<CODE>.pack` must exist next to the executable, or the map never appears in the menu.** Both scanners probe for it and `continue` on failure, by design.

**Two routes to that pack, and they are not interchangeable.**

- **Route A, borrow (works today, zero new tooling).** Copy a theater-matched pack and rename it. This is what `game/missions/make_testmap.py` and `make_skirmish_map.py` already do with `--pack`. **Valid only if you changed no tiles.** The pack carries the per-map terrain cell records (4096 x 21 bytes, `game/bake5.py:1764-1772`), so an edited `.BIN` with a borrowed pack means the collision layer and the rendered art disagree, invisibly. This route is the entire objects-and-ownership MVP.
- **Route B, bake (needed the moment one tile changes).** `tools/bin_to_n64map.py` (`.BIN` to `.MAP`, self-tested bit-exact on 78 of 78 cartridge maps), then `n64_terrain.py`, then `game/bake5.py`. Needs `data/rom/cnc_eu.z64` plus the extracted asset tree, takes about 5.5 s, and the authored map must be staged into `tools/bakery/sharecopy/assets/extracted/` first because `n64_terrain.py:76` and `:265` read from there. **The baked heightmap will be flat**, per section 4b.

**One correction to the requirement.** The pack does not carry a per-map terrain composite. `game/bake5.py:1513` opens `terr["atlas"]`, which is always `TEMPERAT_tiles.png` or `DESERT_tiles.png`. The atlas is **per theater**. What is per map is the 4096 cell records, the 65x65 heightmap, the 65x65 CM tint and the 16-byte name, which is why two same-theater packs of 7,810,842 bytes differ in only about 25,000 bytes, the first at 0x10 and the rest in the last 102,506. This makes both routes easier than the requirement implies, and it makes a **pack byte-patcher** plausible as a third route. Nobody has built one, so that stays UNPROVEN.

### One bug to fix before any write path exists

`show()` overwrites the authored heightmap in place: `app.js:963` sets `m.h = b64(m.heights)`, then `app.js:988` sets **`m.h = padHeights(m)`**. From that moment `MAP.h` is the renderer's cosmetic pad-levelled array, not the map's real elevation, and everything downstream reads the contaminated copy. The original survives only as the base64 string in `m.heights`. **An editor that saves `MAP.h` would write the renderer's building pads to disk as authored terrain.** Keep the authored heights in a field `show()` never touches. One hour, and it prevents a class of silent data loss.

---

## 6. BUILD ORDER

**Slice 1: the ghost. One evening. Proves the interaction is not fighting the browser or the camera.**
Load a shipped map as today. An Edit button. A palette of object types. Click to arm; the real mesh follows the cursor at 50% alpha, snapped by the existing raycast; left click stamps; brush stays armed; Esc disarms. Footprint decals green and red, red drawn over. Clamp the anchor to `[0, 64-w] x [0, 64-h]` so it can never hang off the map (`brain/vanilla/tiberiandawn/display.cpp:940-1010` does exactly this). Four-button owner selector. Delete and drag-to-move. Every edit a Do/Undo object, Ctrl+Z. Fix `padHeights` first.
*Browser hygiene, all named because each has bitten someone:* set `controls.mouseButtons.LEFT = null` while editing (OrbitControls calls `setPointerCapture` on this exact canvas at `public/vendor/OrbitControls.js:1550`); attach pointermove and pointerup to `window`, never capture (pointer capture stole a click in the project owner's own GamedevTycoon map, commit `5c97631`); add `touch-action: none` to the canvas, which `index.html` currently lacks; clean up on `pointercancel` as well as `pointerup`; `preventDefault` the contextmenu OrbitControls binds at `:502`. Rebuild `buildAssets` on mouse-**up**.
**Shows the project owner:** a building that follows his cursor and lands where he clicks.

**Slice 2: the truth layer and the file. One evening. Proves a browser-authored map reaches the lobby.**
Ship the four static tables. Compute the no-go hatch in the browser and **diff it cell for cell against `make_skirmish_map.py`'s Python answer on all 94 shipped maps; zero disagreements or the port is wrong.** Auto-show while armed, pin on a key. The `.INI` writer and download. The linter, lifting the checks already in `make_skirmish_map.py`'s `check()` (the 3x3 buildable start pad for MCV deploy, tiberium only on bare buildable cells per `brain/vanilla/tiberiandawn/overlay.cpp:253`, the contiguous waypoint run, which `app/cnc3d.cpp:317-370` already computes correctly and `scenarioini.cpp:1567` indexes without a bound check). Autosave. Mode-aware status line.
**Shows the project owner:** he makes a skirmish map and plays it. **Stop here and confirm that before writing another line.**

**Slice 3: ground. A few days. Proves terrain art round-trips.**
Six-entry semantic Ground palette restricted to bank-present `(template, icon)` pairs, which is provably safe: the maps use 960 of DESERT's 973 pairs and 745 of TEMPERAT's 758, and exactly **six cells in the entire 94-map corpus** reference a pair the bank lacks (ROAD33 and ROAD43). Block ghost, anti-smear drag, randomisation, flood fill, block-aware eraser. `.BIN` writer. A user-maps source in `export.py`. A shell script for the bake, not a button.
**Shows the project owner:** he paints a road and it is there in the game.

**Slice 4: the bake5 parameter and the PNG. Half a day plus an afternoon. Proves elevation can reach the game at all.**
`build(scen, outpath, heights=None, cmvals=None)`, defaulting to the ROM lookup; insertion points `game/bake5.py:1647` and `:1657`, write at `:1797-1798`. Roughly five lines, and it is the highest-value change in this whole document, because until it runs every elevation feature anyone proposes is unshippable. Then PNG import/export of the 65x65 map on a five-value grey palette.
**Shows the project owner:** a map with hills in it.

**Slice 5 and beyond.** The corner-mask cliff tool, on new maps only, with the ramp swap and the reachability flood fill in the same commit. Then water. Then, if ever, path tiling with preview and re-randomise, and copy/paste.

**Write down before slice 1 ships,** because these are the expensive findings and they outlast every brush: the twelve-entry corner-mask table with SLOPE29-32 as outer corners and SLOPE33-36 as inner, plus the per-template confidence figures, into `docs/terrain-elevation-grammar.md`; and the 4.4% stencil null result, the 58.7% off-ladder figure, and the flat-heightmap consequence of `bake_heights` into `known-gap notes`.

---

## 7. THE OPEN QUESTIONS FOR the project owner

1. **Does v1 have to produce a map you can play, or is a map you can design and export enough?** If playable, slice 2 is the finish line and terrain painting waits; if export-only is fine, we can go straight at the ground brushes and worry about packs later.
2. **Does the first version edit existing maps, or author new ones?** Objects can do both. Terrain and elevation effectively cannot: a tier tool would requantise about 40% of a shipped map's corners the first time you save it.
3. **Are you happy for the first hilly map you make to be a NEW map, with shipped cartridge maps stayed object-only forever?** This is the same question one layer down, and it is the single decision that most changes what gets built.
4. **Is flat terrain acceptable in the first playable maps?** It is what the nine converted skirmish maps already are today, and fixing it is a five-line change to `bake5.py` that nobody has run.
5. **Skirmish maps or campaign missions first?** Skirmish is the proven path to the lobby and needs no ownership at all, since all sixteen shipped multiplayer maps place zero owned objects. Campaign needs the ownership work you asked for.
6. **When the cliff tool cannot honour a request, should it refuse out loud or quietly do the nearest legal thing?** 27 of 38 cliff templates have no walkable notch, so "put a way through here" will sometimes be impossible.
7. **How much do you care about visual variety in cliffs?** Letting the tool pick one variant per direction is much simpler than a variant picker, and five of the templates are too inconsistent in the shipped data to emit safely at all.

---

## Appendix: what a first pass measured before the plan was written

Two numbers were checked by hand in the session that commissioned this, and both are
load-bearing enough to record here rather than leave in a transcript.

**Template block layouts are recoverable from the shipped maps.** Scanning every
placement of every template across the 79 cartridge maps and recording where each icon
sits relative to its block anchor recovers **37 of the 38 SLOPE templates**, and the
layout is row-major in every case: icon = dy * width + dx, for blocks of 2x2, 2x3, 3x2
and a couple of 1x2 and 2x1. 168 of the 216 templates appear in the shipped maps at all.
So a block painter has what it needs without decoding the tile art. The caveat is that
the anchor-finding heuristic is ambiguous where two instances of a template touch, which
is why 13 of the 37 came back flagged inconsistent even though their recovered layout is
the ordinary row-major one; the layout should be taken from the icon count and the
observed bounding box, not from the anchor vote.

**The 51 campaign scenarios are not editable terrain in any honest sense.** They are the
only maps with real heightmaps, and section 4b's measurement is that a tier tool would
requantise up to 40 percent of a shipped map's corners on first save. That is the single
finding that most shapes the build order: the editor should learn to author NEW maps
before it learns to edit old ones.
