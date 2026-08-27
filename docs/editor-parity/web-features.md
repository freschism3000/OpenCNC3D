# CNC3D WEB Mission Editor -- complete feature inventory

Files: `tools/heightmap-viewer/public/index.html` (shell + all CSS + SVG icon sheet), `tools/heightmap-viewer/public/app.js` (renderer / read path, 1688 lines), `tools/heightmap-viewer/public/editor.js` (write path, 2365 lines), `tools/heightmap-viewer/serve.py` (companion server).

Architecture note for the rewrite: `app.js` owns the scene and exports a single `API` object (bottom of app.js); `editor.js` is loaded dynamically at the end of `main()` and reaches the renderer **only** through that API. The editor opens **in edit mode** -- there is no view-only mode (`setMode(true)` is called unconditionally in `init()`), and the left mouse button is taken from OrbitControls (`A.controls.mouseButtons.LEFT = null`, editor.js:2008).

---

## 1. Global modes and tool rail

| Feature | What it does | Invoked by | Implementation |
|---|---|---|---|
| **Three edit modes: Objects / Terrain / Elevation** | Swaps the sidebar pane, the viewport badge, and the whole UI accent colour (`--mode` CSS var: gold / green / cyan) | Three big `.mtab` buttons in `#modes`; keys `1`, `2`, `3` | `wireChrome()` editor.js:2258; `ui()` editor.js:1450; keys in `onKey()` |
| Mode switch side effects | Clears `armed`, clears `brush`, clears ghost; lazily runs `elevInit()` the first time Elevation is opened | same | `wireChrome()` |
| **Tool: Select and place** (default) | Left-click stamps the armed object; with nothing armed, click+drag picks up a placed object and moves it | Rail icon `i-select`, key `V` | `setTool('place')`, `onDown()` |
| **Tool: Move** | Left-click on a placed object begins a move-drag (does not place anything) | Rail icon `i-move`, key `M` | `onDown()` branch `tool === 'move'` |
| **Tool: Pick (eyedropper)** | Picks up whatever is under the cursor, arms it, switches owner to that object's house, switches the palette tab to that object's kind, and reverts the tool to Place | Rail icon `i-drop`, key `I` | `onDown()` branch `tool === 'pick'` |
| **Tool: Erase** | Deletes the topmost object under the cursor on click | Rail icon `i-erase`, key `X` | `onDown()` branch `tool === 'erase'` |
| **Camera / frame playable rect** | Re-frames the camera on the map's playable rectangle | Rail icon `i-camera`, key `0` | `A.frameMap()` → `frame()` app.js |
| Tool-change side effect | Any tool other than Place clears `armed` and `brush` | -- | `setTool()` editor.js:2313 |
| Rail tools only apply in Objects mode | Terrain/Elevation have their own brush gesture | -- | `onDown()` guard `mode === 'objects' && tool !== 'place'` |

**Gap to note:** the rail's "select" tool has no selection state and no selection highlight -- clicking a placed object only enables the drag-to-move gesture. The status card's copy ("click a placed object to select it") overstates it.

---

## 2. Object placement

### Palette
| Feature | Detail | Implementation |
|---|---|---|
| **5 category tabs** | Buildings (22 types), Units (16), Infantry (13), Scenery (19), Overlay (10 = 5 tiberium + 5 wall types). Hardcoded type lists. | `TABS` editor.js:44; `ui()` builds `#cats` |
| **Palette grid (5 across)** | One tile per type; click to arm, click again to disarm (toggle) | `ui()` → `#palgrid` |
| **Real cameo art on tiles** | Uses the game's own sidebar cameo sheet (`T.cameos`), GDI or Nod variant chosen by current owner | `tileArt()` editor.js:158 |
| **Offscreen mesh thumbnail fallback** | Types with no cameo (trees, rocks, tiberium, civilians) get the actual cartridge mesh rendered to a 96×96 offscreen render target and cached as a data URL; marked with a "3D" badge | `meshThumb()` editor.js:74, cached in `thumbCache` |
| Tile badges | 4-letter code chip (recoloured for Nod owner), footprint badge `WxH` on structures, display name (suppressed when non-unique within the tab, e.g. ten "Tree"s) | `ui()` |
| Tooltips | `Name (CODE) - WxH cells`, or "no mesh on the cartridge" | `ui()` |
| Missing-mesh marking | `.miss` class when `A.MODELS.meta.types[t] < 0` | `ui()` |

### Owner / house
| Feature | Detail | Implementation |
|---|---|---|
| **4-way owner strip** | GoodGuy "Yours" (gold), BadGuy "Enemy" (red), Neutral (grey), Special (purple) | `HOUSES` editor.js:36; `#edOwners` in `ui()` |
| **Tab cycles owner** | Wraps 0→3; live-refreshes the ghost | `onKey()` `ev.key === 'Tab'` |
| Owner shown in viewport badge | "placing for **Yours**" | `ui()` → `#vpowner` |
| Owner forced for scenery | Terrain-kind objects are always written house 2 (Neutral) regardless of the owner strip | `stamp()` editor.js:1884 |

### Placement gesture and validity
| Feature | Detail | Implementation |
|---|---|---|
| **Arm-then-stamp loop** (deliberately not HTML5 drag-and-drop) | Arm from palette → ghost follows cursor → click stamps → stays armed for repeat placement | `onDown()` / `stamp()` |
| **3D ghost preview** | The real mesh, at 50% alpha, standing on the ground at the current vertical exaggeration | `showGhost()` editor.js:451, `ghostMaterial()` editor.js:424 |
| **Per-cell footprint validity wash** | Green wash under legal cells (renderOrder 7, *under* the ghost), red wash over illegal cells (renderOrder 9, *over* the ghost so red always punches through) | `showGhost()`, `flatGeom()`, `washMat()` |
| **Ghost rebuilt once per cell crossed**, not per pixel | `onMove()` compares `hoverCell` | `onMove()` editor.js:1843 |
| **Buildability rule from EA GPL source** | `Ground[Land_Type()].Build`, keyed on (TemplateType, icon) with the `AltIcons`/`AltLand` exception list (11 of 38 cliff templates walkable). Structures need `build`; everything else needs `passable`. | `landAt()`, `canBuild()`, `canWalk()` editor.js:174-186; table ships in `editor_tables.json` |
| **Bib-aware footprints** | Bibbed buildings occupy W × (H+1) | `footprint()` editor.js:191 |
| **Scenery occupy-list anchoring** | Trees are *stored* one cell north of where they stand (27 of 32 terrain types); editor works in standing cells and converts on write | `occupyOf()`, `anchorOffset()`, `coveredCells()`, `standingCell()` editor.js:207-236 |
| **Occupancy / collision** | One solid thing per cell; buildings blocked by buildings, trees, walls, tiberium | `occupancy()`, `blockedBy()`, `isSolid()` |
| **Infantry sub-cells** | 5 men per cell, each auto-assigned the lowest free sub-cell slot (`StoppingCoordAbs`) | `INFANTRY_PER_CELL`, `freeSubcell()` |
| **Tiberium is ground cover** | Refuses only a second tiberium on the same cell; blocks nothing else | `isTiberium()`, `blockedBy()` |
| **Edge clamping** | Placement is clamped so nothing hangs off the 64×64 map (including the stored cell for north-stored scenery) | `placement()` editor.js:281 |
| **Drag-paint for overlays and scenery** | Holding the button and dragging stamps tiberium/walls/trees one per cell crossed; other kinds are click-only | `paintable()` editor.js:1881, `onMove()` |
| **Wall auto-connection** | After every edit, walls recompute the engine's 4-bit N/E/S/W same-overlay mask (`CellClass::Wall_Update`); the renderer turns the mask into piece + rotation via the cartridge's 16-way table | `reconnectWalls()` editor.js:487; `WALL_VARIANT` app.js:216 |
| **Move a placed object** | 4px drag slop, gripped at the cell you grabbed (not snapped by corner), validated on drop, Esc cancels | `onDragStart()`, `onMove()` dragging branch, `onUp()`, `moveObject()` |
| **Delete under cursor** | Delete or Backspace removes the topmost object at the hovered cell | `onKey()`, `objectAt()`, `removeObject()` |
| **Esc cancels** | Clears armed, brush, and any in-flight drag | `onKey()` |
| **No-go overlay** | Orange hatch wash on every cell that cannot be built on; auto-visible whenever something is armed, plus a pin toggle with a live count | `rebuildNoGo()` editor.js:355, `syncOverlays()`, `#edNogo` button, key `N` |

**Gaps:** facing is always written 0 (`stamp()` hard-codes `face: 0`); no rotation control, no copy/paste, no multi-select, no rectangle/flood fill, no smudge placement (SMUDGE is written to the INI but has no palette entry), no waypoint editing.

---

## 3. Terrain painting

| Feature | Detail | Implementation |
|---|---|---|
| **7 drawers by template family** | Ground (CLEAR, P) · Water (W) · Shore (SH) · Rivers (RV, FALLS, FORD) · Roads (D) · Rock (B, BR) · Bridges (BRIDGE). Cliffs deliberately *absent* -- they live in Elevation. | `TERRAIN_GROUPS` editor.js:648; `terrainUI()` editor.js:1548 |
| **Block palette with real art** | Every template's icons cropped from the theater tile atlas and composed at true w×h, cached as data URLs; shown at 18px/cell so a 1×1 patch and a 5×4 river mouth are visually distinct | `blockThumb()` editor.js:694, `terrainList()` |
| **Water underlay for hole tiles** | Alpha-hole (water) tiles would render blank; `water1.png` is laid down first, exactly as the console does | `blockThumb()` + `terrainInit()` |
| **Theater filtering** | Only templates the current theater can actually draw are offered; 4 destroyed-bridge variants (`BRIDGE1D` -- `4D`) are permanently excluded | `terrainList()`, `NO_ART` editor.js:665 |
| **Click to arm / click again to disarm** | -- | `terrainUI()` handler |
| **Drag to paint** | One block stamped per cell **crossed** -- anti-smear, never repainted while the pointer holds still | `onMove()` terrain branch, `strokePaint()` |
| **Bridges are click-only** | A crossing must land whole; a drag would chain bridges across dry ground | `isCrossing()`, `onMove()` guard |
| **Bridge mouth rule** | BRIDGE1-4 refuse to place unless the river continues past their measured mouth cells (unanimous over 221 shipped placements); FORD1/FORD2 get no rule | `BRIDGE_MOUTHS` editor.js:747, `crossingFits()` |
| **CLEAR scatters** | Plain ground writes template 255 with a deterministic per-cell hash choosing one of 16 CLEAR1 icons, so it doesn't visibly tile | `CLEAR_TID`, `clearIcon()` editor.js:730 |
| **Cell preview wash** | The exact cells the block will overwrite, green if it fits / red if it doesn't, plus red on skipped cells | `showBrush()` editor.js:1797 |
| **Skipped-cell reporting** | Cells off the map, or icons the cartridge doesn't carry, are skipped and counted in the status card | `blockCells()`, `status()` |
| **Water mask maintained on paint** | Painting a hole-slot tile sets/clears the sea bit, so painting water actually creates sea | `writeCells()` + `HOLE_SET` editor.js:790 |
| **One stroke = one undo step** | Before-state captured on first touch of each cell, so dragging back and forth still undoes to the start | `strokeBegin()` / `strokePaint()` / `strokeEnd()` editor.js:817-861 |
| Terrain edit flags the map | Sets `m.terrainEdited`, which drives .BIN export and the Pack lint row | `strokeEnd()` |

---

## 4. Elevation / heightmap

| Feature | Detail | Implementation |
|---|---|---|
| **Tier model, not a height brush** | The tool authors a 32×32 field of tiers over 2×2-cell blocks; corner heights *and* cliff art are both derived from it | `BLOCKS`, `TIER`, `deriveElevation()` editor.js:993 |
| **5-rung ladder** | Heights 0 / 64 / 128 / 191 / 255, named Low / Ground / +1 / +2 / +3 | `RUNG` editor.js:878, `tierOf()` |
| **Conversion gate** | A shipped map with smooth heights is locked until the player accepts a one-time, non-undoable conversion; the gate shows the exact off-ladder corner count and percentage | `seedTiers()`, `elevUI()` gate + `#edConvert` |
| **Tier brush (5 buttons)** | Paints the armed tier | `.tr` buttons in `elevUI()` |
| **Tool: Raise / lower** | Paint the armed tier onto blocks | `elevTool = 'tier'`, `elevPaint()` |
| **Tool: Graded pass** | Toggle: leave cliff art off a block so the ground just slopes up (what the cartridge does 81.4% of the time) | `elevTool = 'pass'`, `PASS` set |
| **Tool: Ramp (east)** | Toggle: dress the climb with S14, the only self-contained ramp block in the game -- east-facing only | `elevTool = 'ramp'`, `RAMPS`, `RAMP_EAST`, `rampable()` |
| **Brush size 2×2 / 4×4 / 6×6 cells** | 1, 2 or 3 blocks radius | `elevBrush`, `brushBlocks()`, `.bsz` buttons |
| **Derived cliff art** | 16-way corner-compass mask → cartridge SLOPE template, with interchangeable siblings chosen by a positional hash so long cliffs aren't visibly tiled | `CLIFF_FOR_MASK` editor.js:917, `pickCliff()` |
| **Three refusal rules** | (a) a step of more than one tier at once, (b) diagonal saddle masks 6/9, (c) a low area narrower than three blocks that would fill in | `deriveElevation()` refusals |
| **Refusals shown three ways** | Red wash on the offending blocks under the brush, a red summary line in the panel, and the specific reason in the status card on hover | `showElevBrush()`, `elevUI()`, `status()` |
| **Dirty-region writes only** | Only painted blocks (grown by 1 in every direction) are re-derived; sea, shore, roads and tiberium outside the painted area are never touched | `writeElevation()` editor.js:1035, `stroke.dirty` |
| **Bilinear corner interpolation** | Verified against the cartridge (median error +0.000 over 12,964 samples) | `coarseCorners()` + `deriveElevation()` |
| **Full stroke undo** | An elevation stroke restores tiles, corner heights, the tier field, and the PASS/RAMPS sets together | `strokeEnd()` editor.js:838-861 |
| Marks map | Sets `m.terrainEdited` **and** `m.heightEdited`, which drives .HGT export | `applyElevation()` |

---

## 5. Walls

Walls are not a separate mode -- they're the 5 wall types in the **Overlay** palette tab (`SBAG`, `CYCL`, `BRIK`, `BARB`, `WOOD`), plus:

- **Drag-paint** (overlay kind is `paintable()`), one per cell crossed.
- **Automatic 16-way connection** on every edit -- `reconnectWalls()` (editor.js:487) recomputes each wall's N/E/S/W mask; `WALL_VARIANT` + `WALL_SUFFIX` in app.js:216 turn it into the right authored piece (`SBAG`, `SBAG_L`, `SBAG_T`, `SBAG_X`) and rotation, with the wall-specific `(256 - face) & 255` inversion.
- **"Walls" asset visibility group** in the Show popup.
- A wall counts as solid for occupancy; a second wall of the *same* type on the same cell is what's refused.

---

## 6. Validation / linting

| Feature | Detail | Implementation |
|---|---|---|
| **Mission check panel** | Always visible in the sidebar foot; failing rows stay expanded, passing rows fold behind an "N more -- …" row | `lint()` editor.js:1338, `paintLint()` editor.js:1608 |
| Tally chips | Green tick count + red X count in the panel header | `paintLint()` |
| Expand/collapse | Click the panel header **or** the "N more" row | `wireChrome()` + `paintLint()` |
| Lint recomputed on every edit | -- | `after()` editor.js:518 |
| **Rule: Playable rectangle** | Must fit inside 64×64 | `lint()` |
| **Rule: GDI start / Nod start** | Each side's MCV needs a clear buildable 3×3 pad; reports how many of 9 cells are bad, or "no MCV placed" | `lint()` |
| **Rule: Tiberium** | Every tiberium cell must be on buildable ground | `lint()` |
| **Rule: Structures** | Every placed structure re-validated where it stands | `lint()` |
| **Rule: Name** | Scenario id must match `SCM\d\d[EW][ABC]` (skirmish) or `SC[GB]\d\d[EW][ABC]` (Spec Ops), else the map never appears in a menu | `lint()` |
| **Rule: Elevation** (only when heights edited) | Reports the count and the first refusal's block coords and reason | `lint()` + `elevProblems()` |
| **Info: Heights** | Names the .HGT output and the exact `bake5.build(...)` call | `lint()` |
| **Rule: Pack** | Warns that a terrain edit invalidates the existing `.pack` | `lint()` |
| **Live per-cell verdict** | The status card's middle line is the only line that turns red; carries the specific refusal (blocking object type, land type, bridge-mouth reason, elevation refusal) | `status()` editor.js:1699, `card()` |

---

## 7. Save / load / play

| Feature | Detail | Implementation |
|---|---|---|
| **CHECK AND SAVE** | Runs lint, repaints it, flashes the result, downloads `.INI` always; `.BIN` only if a tile changed; `.HGT` only if heights changed | `save()` editor.js:1650; button `#savebtn`; **Ctrl+S** |
| **.INI writer** | `[Basic]`, `[MAP]` (Theater there, *not* in Basic; X/Y/Width/Height all four required), `[TERRAIN]`, `[OVERLAY]`, `[SMUDGE]`, `[STRUCTURES]`, `[UNITS]`, `[INFANTRY]`, one section per owning house, `[Waypoints]`. CRLF line endings. | `writeINI()` editor.js:1287 |
| **.BIN writer** | 4096 cells × (template byte, icon byte), 8192 bytes; atlas slot → icon via the theater slot table | `writeBIN()` editor.js:1592 |
| **.HGT writer** | 4225 raw corner bytes (65×65), the same payload as `<SCEN>.IMG` after its header | `writeHGT()` editor.js:1605 |
| **.INI preview** | Toggleable scrolling pre-block of the exact text that will be written | `showINI()` editor.js:1668, `#edIniBtn` |
| **PLAY** | Saves the three files to `game/authored/` via the server, kicks off a bake, launches the game, polls the job and streams the log | `play()` editor.js:2211; `#playbtn`; **Ctrl+P** |
| **Server capability probe** | `GET api/hello` at startup; the Play button greys out with an explanatory tooltip when the page is served by anything other than `serve.py`, or when no `cnc3d` binary is built | `probeServer()` editor.js:2168 |
| **Job log panel** | Live streaming log with a title, error styling, and a close link | `jobLog()`, `#joblog`, `#jobclose` |
| **Save-state indicator** | Top strip: "No edits yet" / "N edits unsaved", plus transient flash messages ("Saved", "Saved with N problems", "Launched", "Bake failed") | `saveState()` editor.js:531, `flash()` editor.js:1680 |
| **Autosave** | Objects list serialized to `localStorage['cnc3d-edit-<SCEN>']` after every edit; restored on load | `autosave()`, `restoreIfAny()` editor.js:1447-1466 -- **note: terrain and heights are NOT autosaved** |
| **Revert** | Confirm dialog, clears the autosave key, reloads the page | `#edReset` in `wireChrome()` |
| **Deep link** | `location.hash` is set to the map id and read on startup; trailing-slash redirect guard so relative data paths resolve | `show()` app.js; inline script at the bottom of index.html |
| **Server: POST /api/save** | Writes `<SCEN>.INI` (text) + `.BIN`/`.HGT` (base64); strict `^[A-Z0-9]{4,8}$` scenario-name validation; 32MB body cap | `serve.py` `save()`, `check_scen()` |
| **Server: POST /api/play** | Starts a background job; requires the .INI and .BIN to exist | `serve.py` `play()`, `run_job()` |
| **Server: GET /api/job?id=** | Returns `{state, log}` | `serve.py` `do_GET()` |
| **Server: cartridge-data protection** | Editing a shipped mission auto-derives a free name `SCM91EA`…`SCM99EA`, copies the files, and plays the copy; the shipped mission is untouched. Mapping remembered per session. | `serve.py` `is_canon()`, `derive_name()` |
| **Server: bake + launch** | `sh tools/stage-skirmish-maps.sh --from game/authored <SCEN>` then `playable/cnc3d --scen … --pack …`, launched detached; binds 127.0.0.1 only | `serve.py` `run_job()` |

---

## 8. Camera / view

| Feature | Detail | Implementation |
|---|---|---|
| **Orbit rig** | three.js OrbitControls with damping, polar clamp at ~89° | app.js scene setup |
| **Mouse** | Left button is taken by the editor while editing. Middle-drag = dolly. **Right-drag = PAN** (OrbitControls default `RIGHT: MOUSE.PAN`) -- note the top-strip pill claims "RIGHT-DRAG ORBIT", which is wrong. Wheel = zoom. Ctrl/Shift/Meta+drag swaps rotate→pan. | `vendor/OrbitControls.js`, `setMode()` |
| **WASD pan** | Camera-relative, flattened to the ground plane; speed scales with zoom distance and real elapsed time; held keys keep moving; cleared on blur | `panCamera()`, `PAN_KEYS`, `HELD` app.js:1560-1600 |
| **Frame playable rect** | Fits the playable square in both axes, 36° pitch | `frame()` app.js; key `0`, rail camera button |
| **Prev / next map buttons** | Bottom-right of the viewport; walk the *filtered* list and scroll it into view | `stepMap()` app.js:1394; `#prevmap`/`#nextmap`; also **Arrow Left/Right** |
| **Vertical exaggeration slider** | 0.2× to 5.0×, drives both the terrain group's Y scale and the `ASSET_EXAG` uniform so buildings keep true proportions | `setExag()` app.js:1382; `#exag` |
| **"Snap true" button** | Sets exaggeration to exactly 1.0 (64 height bytes = one cell width) | `#trueScale` |
| **Surface modes (4)** | Texture (real atlas) · White (unlit relief) · Tile type (family colours) · Tiers (5-rung colour ramp with smooth blends between rungs) | `setMode()` app.js:1272, `paint()`, `FAM_COLOR`, `TIER_COLOR`; `#modesSurface`; key `T` toggles Texture↔White, `F` selects Tile type |
| **Legend** | Bottom-left; auto-shows for Tile type and Tiers, listing family colours / tier rungs | `legend()` app.js:1290 |
| **Show: 3D assets** (`O`) | Master on/off for the object layer, with a `7/7` count | `toggle('tAssets')` |
| **Show: asset class popup** | 7 independent checkboxes -- Buildings, Trees/rocks, Walls, Tiberium, Vehicles, Infantry, Craters/decals. Opens on hover *or* on caret click; closes on outside click. | `buildAssetGroups()` app.js:1358, `ASSET_GROUPS`, `groupOf()` |
| **Show: Level building pads** (`L`) | Toggles the cut-and-fill grading under buildings (renderer's own generalisation of what the cartridge artists did by hand) | `toggle('tPads')`, `padHeights()` app.js |
| **Show: Wireframe** (`R`) | Cyan wireframe over the terrain | `toggle('tWire')` |
| **Show: Map border** (`B`) | Gold line loop around the playable rectangle, riding the terrain | `toggle('tBorder')`, `buildBorder()` |
| **Show: Sea plane** (`P`) | Animated water -- the cartridge's own two-texture modulate + seabed, 15Hz clock, per-vertex UV warp | `toggle('tWater')`, `buildSea()`, `seaClock()` |
| **Show: No-go** (`N`) | Pin the unbuildable hatch on permanently, with a live cell count | `#edNogo`, `finishUI()`, `rebuildNoGo()` |
| **Hover probe readout** | Top-right of the viewport: cell x,y · outside-border warning · template name + land family · all four corner heights · vertical drop · every object standing there | `pointermove` handler app.js:1245, `assetIndex()` |
| **Status card** | Bottom-centre of the viewport, three lines: what is armed (with icon + size/meta), the verdict for the cell under the cursor (the only thing that goes red), and the relevant key hints + current cell/block coords | `card()`, `status()` editor.js:1690 |
| **Mode badge** | Top-left of the viewport: mode name, mode icon, current owner | `ui()` → `#vpbadge` |
| **Error overlay** | Full-viewport message if the map data fails to load | `#err`, `main()` app.js |

---

## 9. Radar / minimap

| Feature | Detail | Implementation |
|---|---|---|
| **64×64 one-texel-per-cell radar** | Land-type colour, shaded by height so relief reads, dimmed outside the playable rectangle | `drawRadar()` editor.js:2028, `RADAR_LAND` |
| **Object dots** | Owner-coloured pips over the terrain (gold/red/grey, green for tiberium); scenery excluded | `drawRadar()` |
| **Live viewport rectangle** | White box computed by casting the four viewport corners at the ground plane; hides itself when the horizon is in shot | `drawRadarFrame()` editor.js:2075, per-frame in `init()`'s tick |
| **Click / drag anywhere on the radar to fly there** | Keeps the current camera angle and distance | `wireRadar()` editor.js:2124, `lookAtCell()` |
| **Jump: Your base / Enemy base / Tiberium** | Flies to the centre of mass of that side's non-scenery objects; flashes an error if there are none | `sideCentre()`, `wireRadar()` jump buttons |
| **Radar footer + mini stats** | "PLAYABLE x,y · W×H", "64×64", height range, structure/unit/infantry/scenery counts | `drawRadar()` |

---

## 10. Map browser and map info (the `#topbox` three tabs)

| Feature | Detail | Implementation |
|---|---|---|
| **Tab: Radar / Missions / Map info** | Exactly one visible at a time | `wireChrome()` boxtab handler |
| **Browser takes over the sidebar** | Opening Missions hides the mode tabs and edit panes (`body.browsing`) and grows the box | same |
| **Map chip in the top strip** | Shows id, name, theater, size, relief, object count, unresolved-tile warning, and a side dot; clicking it opens the Missions tab | `hud()` app.js:1121, `$('mapchip').onclick` |
| **Text filter** | Matches id, name, or label; live hit count | `#filter`, `buildList()` app.js:1516 |
| **Filter chips (13, in 4 groups)** | Mode: Singleplayer / Multiplayer / Spec Ops / Covert Ops / Dinosaurs / Bonus / Unused · Side: GDI / Nod · Media: Cartridge / DOS CD · Elevation: Heightmap / No heightmap. OR within a group, AND across groups. Each chip carries a per-chip count and a provenance tooltip citing ROM addresses / GPL source. | `FILTERS` app.js:1427, `passes()`, `buildChips()` |
| **Clear filters** | Appears only when filters are active | `#chipclear` |
| **Grouped, sorted list** | 9 headed groups (Spec Ops, GDI campaign, Nod campaign, Covert Ops, Bonus, Debug rows, Named by nothing, Dinosaurs, Multiplayer); each row shows theater dot, id, italic name, relief multiplier or "flat" | `buildList()` |
| Empty state | "Nothing matches those filters." | `buildList()` |
| **Briefing panel** | The mission's real briefing text plus its source tag (`ENG/MISSION.ENG`), with a note when the cartridge files it under Special Ops | `showBriefing()` app.js:1229 |
| **Map info readout** | Sectioned: Heightmap (source, byte range, relief in cell widths, corner count, playable rect, a terrace histogram of height bytes covering ≥2% of the playable rect) · Objects on this map (per-category counts) · How things are drawn (mesh count, wall count, tiberium count, infantry sprite source, sea cell count, list of types with no cartridge mesh) | `hud()` app.js:1121 |

---

## 11. Undo / history

| Feature | Detail | Implementation |
|---|---|---|
| **Undo / Redo stacks** | Command objects with `label`, `redo()`, `undo()`, and a `terrain` flag; capped at 200 entries; redo cleared on any new edit | `pushEdit()`, `undo()`, `redo()` editor.js:559-585 |
| **Buttons** | `#edUndo` / `#edRedo`, greyed out (`.dis`) when the corresponding stack is empty | `wireChrome()`, `finishUI()` |
| **Keys** | Ctrl/Cmd+Z undo, Ctrl/Cmd+Shift+Z redo, Ctrl/Cmd+Y redo | `onKey()` |
| **Edit kinds recorded** | place object, delete object, move object, paint terrain (one stroke), raise ground (one stroke, restoring tiles + heights + tier field + PASS/RAMPS together) | `addObject`, `removeObject`, `moveObject`, `strokeEnd()` |
| **Post-edit refresh pipeline** | `after()` re-snaps walls → rebuilds terrain or object layer → rebuilds no-go → restores the ghost under the pointer → status → radar → button states → lint → save-state → autosave | `after()` editor.js:518 |
| **Stacks reset on map load** | -- | `A.onMapLoaded` hook at the end of `init()` |

---

## 12. Complete keyboard map

| Key | Action | Where |
|---|---|---|
| `1` `2` `3` | Objects / Terrain / Elevation mode | editor `onKey()` |
| `V` / `M` / `I` / `X` | Place / Move / Pick / Erase tool | editor `onKey()` |
| `0` | Frame the playable rectangle | editor `onKey()` |
| `Tab` | Cycle owner house | editor `onKey()` |
| `Esc` | Cancel armed item / brush / drag | editor `onKey()` |
| `Delete`, `Backspace` | Delete object under cursor | editor `onKey()` |
| `Ctrl/Cmd+Z` | Undo | editor `onKey()` |
| `Ctrl/Cmd+Shift+Z`, `Ctrl/Cmd+Y` | Redo | editor `onKey()` |
| `Ctrl/Cmd+S` | Check and save | editor `onKey()` |
| `Ctrl/Cmd+P` | Play | editor `onKey()` |
| `N` | Pin the no-go overlay | editor `onKey()` |
| `W` `A` `S` `D` | Pan camera (held; reserved -- both key handlers explicitly yield them) | app.js `panCamera()` |
| `O` | Toggle 3D assets | app.js keydown |
| `L` | Toggle building pads | app.js keydown |
| `R` | Toggle wireframe | app.js keydown |
| `B` | Toggle map border | app.js keydown |
| `P` | Toggle sea plane | app.js keydown |
| `T` | Toggle Texture ↔ White surface | app.js keydown |
| `F` | Tile-type surface mode | app.js keydown |
| `←` `→` | Previous / next map in the filtered list | app.js keydown |

Both key handlers bail out when the event target is an `INPUT`. `Tiers` surface mode has no shortcut.

---

## 13. Mouse / pointer behaviours (including right-click)

- **Right-click**: context menu suppressed on the canvas (`cv.addEventListener('contextmenu', e => e.preventDefault())`, editor.js:2351). Right-**drag** = OrbitControls pan.
- **Left button** belongs to the editor; OrbitControls' LEFT action is nulled while editing.
- `pointermove` / `pointerup` are bound on **window**, not the canvas, and nothing calls `setPointerCapture` -- deliberate, documented at editor.js:1834 as the fix for a capture-steals-the-click bug.
- `pointercancel` cleans up drag state identically to `pointerup`.
- Canvas has `touch-action: none` so a touch drag doesn't scroll the page.
- 4px drag slop separates a click from a move-drag (`DRAG_SLOP`).
- Hover updates are throttled to one rebuild **per cell crossed**.
- Radar drag installs its own temporary window-level `pointermove`/`pointerup` pair.
- Asset-class popup: opens on CSS hover *or* on caret click; a document-level click closes it.

---

## 14. Things a native rewrite would inherit as gaps

Worth listing explicitly on the parity checklist so they don't get re-invented as "missing":

1. No object **facing** editing -- `stamp()` always writes `face: 0`.
2. No **waypoint** editing -- `m.waypoints` is passed through to the INI unchanged.
3. No **playable-rectangle** editing, no map resize, no new-map creation, no save-as/rename.
4. No **house/credits/triggers/teamtypes/briefing** editing -- `[GoodGuy]`-style sections are written with hardcoded values (`Credits=5000, MaxBuilding=150, MaxUnit=150, Allies=self, Edge=North`).
5. No **smudge** placement (written on save, never placeable).
6. **Autosave only covers objects**, not terrain tiles or heights.
7. No selection model, no copy/paste, no multi-select, no rectangle/flood-fill tools.
8. The top-strip hint pill says "RIGHT-DRAG ORBIT" but right-drag pans.
9. Elevation conversion is one-way and explicitly "cannot be undone".
10. Cliffs cannot be painted directly by design (no Cliffs drawer); they only appear as a consequence of tier edits.