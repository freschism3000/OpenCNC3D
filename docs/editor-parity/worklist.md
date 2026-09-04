# Native C&C map editor: worklist

> **Revised 25 Aug 2026.** Everything above the line below has been rebuilt since the
> original recon, and most of what that recon called missing is now built. The five
> recon reports beside this file are HISTORY: they describe a tree that no longer
> exists. Read this file, and treat their line numbers as archaeology.
>
> Verify before believing any of it: `./playable/cnc_eyes --uitest` must print
> `UITEST|TOTAL|0 failures`, and `sh tools/gate-scripttest.sh` must print
> `SCRIPTGATE: 95 passed, 0 failed`.

## Built since the recon

Bugs B1-B8, and parity items P1-P7 and P14, are done. So is a whole layer the recon did
not contemplate, because at the time the native editor could not author a script at all:

| | where |
|---|---|
| Undo / redo / revert, delete, the tool rail, move | `edit_mod.h` |
| TERRAIN mode with the real theater atlas, the bridge-mouth rule | `edit_mod.h` |
| ELEVATION mode, the tier model, the conversion gate, the `.HGT` writer | `edit_mod.h` |
| Unreal camera (WASD + right-drag), the horizon, the view bar | `cnc_eyes.cpp` |
| The in-editor menu, New Map, Open Map, user maps, SP/MP kinds | `edit_mod.h` |
| Player starts for both map kinds | `edit_mod.h` |
| SCRIPT: rules (17 events, 18 actions), zones, object tags | `edit_mod.h` |
| SCRIPT: the TeamType editor and its order chain | `edit_mod.h` |
| SCRIPT: the **Enhanced** tier -- see `docs/scripting/enhanced.md` | `enhanced_mod.h` |
| TRACE: play headless, report the tick each rule fired on | `cnc_eyes.cpp` |
| The live script feed while playing out of the editor (F7) | `cnc_eyes.cpp` |
| CHECK MISSION: ~20 validation rules, audited against all 95 shipped missions | `edit_mod.h` |
| `--uitest` rewritten: 8 frame sizes x 2 backings, reachability not just visibility | `edit_mod.h` |

Gates: `--uitest`, `--picktest`, `--undotest`, `--painttest`, `--elevtest`,
`tools/gate-scripttest.sh` (95 missions), `tools/gate-enhanced.sh`.

## The 25 Aug scripting-UX rebuild (after the project owner's "redo the entire scripting system")

Done in this pass, and the shape future work must keep:

- **The panel's text is ef_text (edit_font.h), never sb_text.** DejaVu Sans baked to a
  size ladder by tools/gen_editor_font.py, drawn 1:1. FONT.SHP remains the GAME's face.
  Anything added to the panel that calls sb_text will look wrong and reads as a bug.
- **Choices open a PICKER (eui_open_row_popup), never click-to-cycle**, for anything over
  four options. Numbers are TYPED (eui_open_num_edit). Adding a cycling control is a
  regression.
- **Wires reach the world**: rule cards carry a gold TEAM pin, a red WATCHES pin and a
  green ZONE pin; drops land on team boxes, map objects and ground respectively; a team
  wire dropped on nothing opens the team menu at the pointer. The selected rule draws
  wires to its tagged objects and zone centroid via world_to_screen.
- **New Map modal is a flow (eui_modal_flow_y)** -- never add a fixed-Y row to a modal.

## Done 26 Aug (the "fully featured" pass)

Order chains reorder by drag with Loop retargeting; "+ add order" on the chain; waypoint
arguments pick from a list of placed waypoints and Move/Unload orders draw wires to
their actual cells (unplaced = a red "nowhere" stub); the Enhanced view has a carriers
column with wires, pin drags and "+ new carrier"; zone clauses carry a draggable pin;
shift+drag sweeps a rectangle region; names rename in place with reference propagation;
every no-selection click toasts; the canvas carries a pin-colour legend; --uitest probes
the new chips, boxes and rename regions; AUTHGATE drives the pickers.

## Still open, in the order I would take them

1. **P9 the status card and hover probe.** The readout shows what is armed and the map's
   counts, but a REFUSED placement is still silent: the cell verdict, the land family and
   the corner heights under the pointer are not shown. This is the legibility rail and
   it is the biggest remaining daily-loop gap.
2. **P10 the no-go overlay.** Hatch every unbuildable cell while something is armed, with
   a live count. Cheap, and it makes P9's refusals predictable rather than surprising.
3. **P11 the 3D ghost preview.** The real mesh at 50% alpha with a green/red wash. Wanted,
   but only after 1 and 2, which answer the same question in cheaper ways.
4. **P8's terrain half.** The mission check validates the world's objects and the whole
   script; it does not check the playable rectangle, tiberium on buildable ground, or the
   elevation refusals. See `known-gap notes`.
5. **P12 surface modes and view toggles.** The view bar exists; Texture/White/Tile-type/
   Tiers, wireframe, pads, border, sea plane and the exaggeration slider do not.
6. **P13 radar.** Jump-only. Missing the camera frustum rectangle, drag-to-fly and the
   three centre-of-mass jumps.
7. **P16 palette presentation.** 4 columns of cameo-or-blank; no mesh thumbnails for the
   cameo-less types, no footprint badge, no owner-tinted code chip.
8. **P17 the keyboard map.** Ctrl+S, Ctrl+P, F7 and the tool keys are bound; the mode
   keys, Tab, Delete, Ctrl+Z/Y and the view keys are not.

## Delete the browser editor?

Not yet, and the reason is narrow: `tools/heightmap-viewer/` is still the only place the
web editor's own measurements were made (the elevation median-error figure, the byte-for-
byte `.BIN` comparison). Once the native elevation work has reproduced those numbers in
its own gate, the browser editor has nothing left that native lacks and should go.

---

# HISTORY BELOW THIS LINE
#
# What follows is the original worklist as written against the pre-rebuild tree. It is
# kept because its ANALYSIS is still often right even where its line numbers are not --
# in particular the web-editor behaviour it documents is the specification for items 1-8
# above. Do not act on its "still broken" claims without checking them first.

## Read this first: the recon reports are stale

All five reports were written against a tree that a a concurrent edit has since rewritten. I re-verified everything below against the **current working copy** (`game/cnc_eyes.cpp` = 18,411 lines, `game/edit_mod.h` = 1,668 lines, both dirty on `main` at `f4c3a53`). What changed since the reports were written:

| Reported as broken | Actual state now | Evidence |
|---|---|---|
| Window not resizable | **Fixed.** `flags \|= SDL_WINDOW_RESIZABLE;` unconditionally | `cnc_eyes.cpp:18325` |
| `SDL_WINDOW_ALLOW_HIGHDPI` never set anywhere | **Fixed for the editor.** `if (o.edit) flags \|= SDL_WINDOW_ALLOW_HIGHDPI;` | `cnc_eyes.cpp:18331` |
| Window too small (1280x720) | **Fixed.** Edit mode with no `--w/--h` opens at 86% x 90% of the display's usable bounds, floor 1280x800 | `cnc_eyes.cpp:18300-18316` |
| UI too small / no DPI factor exists | **Fixed.** `eui_scale()` = `g_uiBacking * (1 + (fbh/1000 - 1)*0.25) * g_uiZoom`, clamped [1.0, 3.0]; all layout is in design units x `L.s` | `edit_mod.h:306-317`, `426-471`; backing written at `cnc_eyes.cpp:17237` |
| Clicks dead / 6 controls discarded | **Fixed.** `case EUI_MODE:` and `case EUI_ACT:` exist; every panel click logs `edit: click at X,Y ... -> hit N arg N` | `cnc_eyes.cpp:17809-17838` |
| No cursor over the panel | **Fixed, but not by any of cursor-path's options A/B/C.** The panel draws its own two-pass black/white arrow from `g_euiMouseX/Y` whenever `g_euiHotKind != EUI_MAP` | `edit_mod.h:815-832`, hover fed at `cnc_eyes.cpp:17919` |

`playable/cnc_eyes` is dated 24 Aug 23:10 and **already contains this code** (it answers `--uitest`, which did not exist when the reports were written). `./cnc_eyes --uitest` currently prints `UITEST|TOTAL|0 failures` across six frame sizes.

**So: if the project owner still sees the old symptoms, the first thing to check is staleness, not code.** The launcher prints `WARNING a source file is newer than this binary` into `playable/Editor.log` when the staged binary is behind. Rebuild is `cd game && ./build.sh && cp cnc_eyes ../playable/`.

Everything below is what is *still* wrong or *still* missing as of right now.

---

# BUGS

## B1. ESC opens the 1995 pause dialog on top of the editor
**Highest severity. This is the one remaining "the editor is dead" trap.**

`cnc_eyes.cpp:17629` - `case SDLK_ESCAPE: if (!sim_over) opt_show(o->scen); break;`. `opt_show` (`:10793`) has no `g_editOn` guard; it sets `g_optOpen = true` and `cur_lock(MOUSE_NORMAL)`. From then on `else if (g_optOpen)` sits above the mouse branch in the event chain and eats every button event, while `eui_draw` at `:18127` keeps painting the panel (gated on `g_editOn` only). The only ways out are Resume or Abort, and Abort quits the process, discarding unsaved edits.

The launcher's own comment says *"ESC is how you leave the editor"* (`playable/Editor.app/Contents/MacOS/editor-launch`, near line 236). It is not. That comment is wrong today, and ESC is the first key anyone presses.

Same hole for `*` -> `cheat_show` at `cnc_eyes.cpp:17633-17634`.

**Change:** guard both entry points. Either `if (g_editOn) return;` at the top of `opt_show` and `cheat_show` (matching the precedent already set by `draw_cam_hud` at `cnc_eyes.cpp:10876`), or make `case SDLK_ESCAPE:` in edit mode do the editor thing: disarm if something is armed, otherwise `running = false`.

**Verify:** `./cnc_eyes --edit --scen SCG01EA ...`, press ESC, then click a palette tile and the map. `playable/Editor.log` must contain zero `OPTIONS|` lines and must still show `edit: click at ... -> hit 3` for the tile. Screenshot: no dialog on screen.

---

## B2. A short window swallows UNDO/REDO/REVERT and then SAVE
Now reachable, because the window became resizable in the same session that introduced this.

`edit_mod.h:470` - `if (L->gridH < 60 * S) L->gridH = 60 * S;` while `actY/saveY/playY` stay pinned to the bottom (`edit_mod.h:461-465`). `eui_hit` tests the grid rectangle at `edit_mod.h:932` and returns `EUI_DEAD` for any interior miss, **before** it ever reaches ACT (`:945`), SAVE (`:951`) and PLAY (`:952`).

My arithmetic at S=1: `gridY` lands at ~330px, so once the drawable height drops below roughly 500px the clamped grid overlaps the ACT row, and below ~450px it starts eating SAVE. Treat those numbers as approximate - measure them, do not trust them.

**Change:** either clamp `gridH` to `max(0, lintY - pad - y)` and let the grid vanish instead of overflowing, or reorder `eui_hit` to test ACT/SAVE/PLAY before the grid. Reordering is one line and is the safer of the two.

**Verify:** add `{1280,480}` and `{1280,560}` to the `SIZES` table at `cnc_eyes.cpp:18284`, and add three `eui_expect(..., EUI_ACT, i, ...)` probes to `edit_uitest` (`edit_mod.h:1014`, which currently tests MODE/OWNER/TABC/ITEM/SAVE/PLAY but **not ACT**). `./cnc_eyes --uitest` must print `UITEST|TOTAL|0 failures`. It currently passes only because it never probes the ACT row and never tests a window short enough to trigger the clamp.

---

## B3. The `--uitest` harness gives false results at any scale above 1.0
Depends on B2 being done in the same pass, since B2's fix is verified through this harness.

Two defects in `edit_uitest` / `eui_expect` (`edit_mod.h:997-1080`):

1. **Probe points use unscaled paddings.** e.g. `L.sideX + EUI_PAD + i * (L.modeW + EUI_PAD)` where `eui_layout` actually uses `EUI_PAD * S`. At S=1.05 (1920x1200) the drift at i=2 is ~1px and it passes by luck. At S=3 (Retina 2x plus a tall frame, which is exactly the project owner's machine) the drift is 42px, well outside a tab. Same for the literal `4`, `3`, `5`, `6` gaps.
2. **`g_uiBacking` is never varied.** It defaults to 1.0 and `--uitest` runs before `SDL_Init` (`cnc_eyes.cpp:18275-18287`), so the harness has never once exercised the Retina path that the whole `eui_scale` work exists to serve.

**Change:** multiply every probe offset by `L.s`; add an outer loop over `g_uiBacking ∈ {1.0f, 2.0f}` and print it in the `UITEST|frame` line.

**Verify:** `./cnc_eyes --uitest` prints twelve `UITEST|frame` blocks (6 sizes x 2 backings) and `TOTAL|0 failures`. Deliberately break one rectangle by 5px in `eui_layout` and confirm the harness reports it at both backings.

---

## B4. The cell cursor keeps tracking a map cell behind the sidebar
`edit_mod.h:78-84`, `edit_update_hover`: `g_editOverMap = (mouseC >= 0.0f) && screen_to_cell(...)` with no `mx < L.sideX` test. With `g_sbOn` false in edit mode the tactical viewport is the whole window, so a pointer parked on the palette still resolves a cell and `edit_draw_overlay` (`:93`) plus `edit_draw_footprint` (`:1173`) keep drawing a cyan cell outline and a footprint out in the map. It reads as the editor pointing at something you are not pointing at.

Harmless for clicks (the `ehit != EUI_MAP` arm at `cnc_eyes.cpp:17813` wins first), purely visual.

**Change:** in `edit_update_hover`, compute `EuiLayout L; eui_layout(fbw, fbh, &L);` and require `mouseC < L.sideX && mouseR >= L.topH` before calling `screen_to_cell`.

**Verify:** `--edit`, sweep the pointer from the map onto the sidebar. Screenshot with the pointer over the palette: no cyan cell outline and no footprint quad anywhere on the map.

---

## B5. `update_cursor` is still blind to the editor panel
This is the *true* residual of cursor-path's finding, which survived the fix that was actually taken.

`cnc_eyes.cpp:9534-9548`: the early-out tests `sb_over_panel() || sb_placing() || fxp_over_panel()` and knows nothing about `eui_hit`. Because `--edit` sets `g_sbOn = false` (`:15873`), `sb_over_panel` returns false immediately, so with the pointer parked on the palette the engine still runs a full `pick_at` every frame, sets `g_cursorOnMap = true`, and picks a move/attack/deploy shape from the terrain hidden behind the panel. `c3d_draw` at `~:12813` then draws the 3D world cursor mesh out in the world under the panel.

Not visible today (the 2D sprite self-suppresses at `:9520` on `g_cursorOnMap`, and the panel draws its own arrow), but it is a wasted pick per frame and it is a landmine: any future change to the draw order or to `draw_cursor_top`'s guard produces two pointers.

**Change:** add a third term to the disjunction at `:9544`. `g_editOn` and `eui_hit` are declared in `edit_mod.h`, which is `#include`d at `cnc_eyes.cpp:10179`, i.e. *after* `update_cursor`. Use the same forward-declaration trick the file already uses for `begin_overlay`/`end_overlay` at `:8173-8174`, or add a tiny `static bool edit_over_chrome(float,float,int,int);` forward decl beside them.

**Verify:** `fprintf` the pick count per second with the pointer held on the palette, before and after. Should drop to zero. Visually nothing must change.

---

## B6. The panel arrow freezes when the pointer leaves the window
`g_euiMouseX/Y` and `g_euiHotKind` are only written by `eui_hover` on `SDL_MOUSEMOTION` (`cnc_eyes.cpp:17919`). `SDL_WINDOWEVENT_LEAVE` is handled (`~:17910` region) but only clears the game cursor, so the editor's own arrow stays painted at the last position it saw. Also means a stale hover highlight sits on whatever control the pointer left over.

**Change:** in the `SDL_WINDOWEVENT_LEAVE` arm, set `g_euiMouseX = g_euiMouseY = -1.0f; g_euiHotKind = EUI_MAP; g_euiHotArg = -1;`, and skip the arrow draw at `edit_mod.h:815` when `g_euiMouseX < 0`.

**Verify:** `--edit`, move the pointer off the window edge, screenshot. No arrow, no lit control.

---

## B7. `g_uiZoom` exists but nothing writes it
`edit_mod.h:304` - `static float g_uiZoom = 1.0f;` with the comment "the project owner's own zoom, so the panel can be sized without resizing the window." There is no key bound to it anywhere in the tree (grep confirms: the only reads are `eui_scale`). If the project owner's complaint after B1-B6 is still "too small", this is the escape hatch and it is unreachable.

**Change:** bind `Cmd/Ctrl +` / `Cmd/Ctrl -` / `Cmd/Ctrl 0` in the edit-mode key handler near `cnc_eyes.cpp:17758`, stepping `g_uiZoom` by 0.1 over [0.7, 1.6] and logging the new value.

**Verify:** press the keys, watch `L.s` change in a debug line, and confirm `eui_hit` still agrees with `eui_draw` at the new zoom (B3's harness with `g_uiZoom` swept is the real test).

---

## B8. Unverified, carried forward from the click-path report
A press that starts on the map and is *released* over the panel still runs `ui_click_action` at panel coordinates, because nothing cancels `lpress` when the pointer crosses into the sidebar. The report placed this in the `SDL_MOUSEBUTTONUP` arm around old line 17856. **I did not re-verify this at current line numbers** and the surrounding code has moved. Confirm before acting: check whether the `MOUSEBUTTONUP` handler tests `eui_hit(...) != EUI_MAP` before firing the band-select/order path.

---

## B9. `app/cnc3d.cpp` has its own window and none of this applies to it
`app/cnc3d.cpp:986` creates the shipping window with its own flags. `playable/Editor.app` runs `./cnc_eyes --edit` (launcher line ~230), so today the editor is entirely on the `cnc_eyes` path and B1-B7 land in the right place. **If the editor ever moves into the shipping binary, every window-level fix above (RESIZABLE, ALLOW_HIGHDPI, the edit-mode default size) must be repeated in `app/cnc3d.cpp`.** Flagging so it does not get silently lost.

---

# PARITY GAPS

Ordered by dependency, then by how much of the web editor's daily loop each one unblocks. Everything here is confirmed absent from the current tree.

## Tier 1 - the safety net, needed before anything else lands

**P1. Undo / redo / revert.** `eui_action_live()` at `edit_mod.h:346` hardcodes `return false;` and the switch prints `"edit: %s is not wired up yet"` (`cnc_eyes.cpp:17834-17836`). The web editor has a 200-entry command stack with `label`/`redo()`/`undo()`/`terrain` (editor.js `pushEdit()`), redo cleared on any new edit, and Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y. Every mutating feature below needs this first.
*Verify:* place three objects, undo three times, confirm `g_objects.size()` returns to the loaded count and the INI written after the undos byte-matches the INI written before the placements.

**P2. Delete / erase.** No `SDLK_DELETE` or `SDLK_BACKSPACE` handler exists anywhere in `game/*` (grep is empty), and there is no erase tool. Right now a misplaced building is permanent until you quit without saving.
*Verify:* place, hover, Delete, confirm the object leaves both `g_objects` and the rendered scene, and that P1's undo restores it.

**P3. The left tool rail: Select / Move / Pick / Erase / Camera, keys V M I X 0.** The native panel has no rail at all; placement is implicit whenever something is armed. Web behaviour is documented in web-features §1, including the side effects (any tool other than Place clears `armed` and `brush`; Pick also switches owner and palette tab and reverts to Place).
*Verify:* extend `edit_uitest` with a rail rectangle per tool; screenshot each `.on` state.

**P4. Move a placed object.** 4px drag slop, gripped at the cell you grabbed rather than snapped by corner, validated on drop, ESC cancels. Depends on P3's Move tool and P1's undo.

## Tier 2 - the two unbuilt modes

**P5. TERRAIN mode.** `case EUI_MODE:` accepts only `earg == 0`; 1 and 2 print `"edit: %s is not built yet"` (`cnc_eyes.cpp:17829-17833`). The web version is 7 drawers by template family (Ground/Water/Shore/Rivers/Roads/Rock/Bridges, cliffs deliberately excluded), a block palette cropped from the theater atlas at 18px/cell, drag-to-paint one block per cell *crossed*, click-only bridges with the measured bridge-mouth rule, CLEAR scatter over 16 icons by per-cell hash, water-mask maintenance on hole-slot tiles, and one-stroke-one-undo.
*Verify:* paint a river across dry ground, save, and diff the resulting `.BIN` against a `.BIN` produced by the web editor doing the same strokes on the same map. The two writers must agree byte for byte.

**P6. ELEVATION mode.** Entirely absent. Web has the tier model (32x32 field of 5 rungs 0/64/128/191/255 over 2x2 blocks), the one-way conversion gate for shipped smooth-height maps, three tools (Raise/lower, Graded pass, Ramp east), three brush sizes, derived 16-way cliff art with positional-hash sibling selection, three refusal rules, and dirty-region-only rewrites.
*Verify:* the web editor's own claim is a median error of +0.000 over 12,964 bilinear samples against the cartridge. Reproduce that measurement natively before calling it done.

**P7. `.HGT` writer.** `edit_save` writes INI and BIN only (`edit_mod.h:1483-1512`, `1546`). Web writes 4,225 raw corner bytes for `<SCEN>.IMG`-after-header. Blocked on P6 having something to write.

## Tier 3 - judgement and feedback (the fog/legibility half)

**P8. Mission check / lint.** What the native panel calls "lint" (`edit_mod.h:748-774`) is a stats readout: armed item, structure/object/wall/tiberium counts, and a key hint. It performs **zero validation**. Web runs eight rules (playable rect fits 64x64; GDI and Nod MCV each need a clear buildable 3x3 pad, reported as "N of 9 cells bad"; every tiberium cell on buildable ground; every structure revalidated in place; scenario name matches `SC[MGB]\d\d[EW][ABC]`; elevation refusals; the `.HGT` info row; the pack-invalidation warning), with green/red tally chips, failing rows expanded and passing rows folded.
*Verify:* load a map, delete the GDI MCV, confirm the panel shows a red tally of 1 and the row names the missing MCV. Load `SCG01EA` clean and confirm zero red.

**P9. The status card and the hover probe.** Web has a bottom-centre three-line card (what is armed with icon and size, the per-cell verdict which is the only line that turns red and carries the specific refusal, key hints plus live cell/block coords) and a top-right probe readout (cell x,y, tile name, land family, all four corner heights, vertical drop, every object standing there). Native has neither. This is the legibility rail: right now a refused placement is silent.
*Verify:* hover a cliff cell with a structure armed. The card must read red and name the land type that refused it.

**P10. The no-go overlay.** Web hatches every unbuildable cell, auto-visible whenever something is armed, plus a pin toggle (`N`) with a live count. Native draws only a per-placement footprint quad (`edit_draw_footprint`, `edit_mod.h:1173`).

**P11. 3D ghost preview.** Web shows the real mesh at 50% alpha standing on the ground at the current exaggeration, with a green wash under legal cells (renderOrder 7) and a red wash over illegal ones (renderOrder 9, so red always punches through). Native shows a flat footprint quad.

## Tier 4 - view and navigation

**P12. Surface modes and view toggles.** All absent: Texture / White / Tile type / Tiers with the auto-showing legend; wireframe (`R`); building pads (`L`); map border (`B`); animated sea plane (`P`); the seven-way asset-class popup with its `7/7` count (`O`); the 0.2x-5.0x vertical exaggeration slider with "Snap true".

**P13. Radar.** Native radar is jump-only: click a cell, camera teleports (`cnc_eyes.cpp:17822-17826`). Missing the live camera frustum rectangle (four NDC corners raycast onto y=0, hidden when fewer than 4 hit), drag-to-fly, the three "Your base / Enemy base / Tiberium" centre-of-mass jumps, and the height-range and object-count mini stats.

**P14. Mission browser.** Native is one map per process (`--scen`). Missing the whole `#topbox` Missions face: filter box with live hit count, 13 filter chips in 4 groups (OR within a group, AND across groups, per-chip counts), the 9-group sorted list, the empty state, the mission briefing panel with its source tag, prev/next map buttons and Arrow Left/Right. Also missing the Map info face (heightmap source/range/relief/corners/playable, the >=2% terrace histogram, per-kind object counts, the "how things are drawn" prose).

## Tier 5 - persistence and chrome

**P15. Save-state, autosave, INI preview, job log.** Missing: the "No edits yet / N edits unsaved" indicator with 2600ms flash messages; `localStorage`-equivalent autosave of the object list restored on load; the toggleable `.INI` preview showing the exact text that will be written; the streaming job log for PLAY. `edit_request_play` (`edit_mod.h:1584`) exists and works, but there is no log surface.

**P16. Palette presentation.** Native is 4 columns of cameo-or-blank (`eui_layout` sets `L->cols = 4`; `eui_cameo` at `edit_mod.h:386` fills `0x0d1520` when `sb_cameo` misses). Web is 5 columns with a 96px offscreen mesh-thumbnail fallback for cameo-less types (trees, rocks, tiberium, civilians) marked with a "3D" badge, plus a 4-letter code chip recoloured for Nod ownership, a `WxH` footprint badge on structures, and per-tab name suppression when a display name is not unique.

**P17. Keyboard map.** Native binds only Ctrl+S and Ctrl+P (`cnc_eyes.cpp:17758`, `17765`). Missing `1 2 3` (mode), `V M I X` (tool), `0` (frame), `Tab` (cycle owner), `Esc` (cancel armed/brush/drag - see B1), `Delete`/`Backspace`, `Ctrl+Z`/`Ctrl+Y`, `N` (no-go), and the view keys `O L R B P T F` plus arrows.

## Already at parity, do not re-implement

- **Wall auto-connection.** `edit_wall_mask` / `edit_wall_put` (`edit_mod.h:856`, `871`) implement the 4-bit N/E/S/W same-overlay mask, same as web's `reconnectWalls()`.
- **Buildability from the GPL land table**, occupancy, and bib-aware footprints: `edit_land_at` / `edit_can_build` / `edit_cell_occupied` / `edit_cell_ok` (`edit_mod.h:205-240`).
- **Click-to-arm-then-click-to-place, with the second click on the same tile disarming.** The click-path report called the toggle a bug; it is not, the web editor does exactly the same thing (editor.js: "click to arm, click again to disarm"). Do not change it without asking the project owner.
- **Cartridge-data protection.** The web server derives `SCM91EA`..`SCM99EA` when you edit a shipped mission. Native writes into `game/authored/` via `edit_save`; confirm with the project owner whether the derived-name rule should be replicated natively, since the two paths currently differ.

## Gaps the web editor also has - do not "fix" them into existence natively

Carried straight from web-features §14, so they are not misread as native regressions: object **facing** is always written 0; no waypoint editing; no playable-rectangle editing, map resize, new map, or save-as; no house/credits/trigger/teamtype/briefing editing (those sections are hardcoded); no smudge placement (written on save, never placeable); autosave covers objects only, not terrain or heights; no selection model, copy/paste, multi-select, or fill tools; elevation conversion is one-way; cliffs cannot be painted directly by design.

---

## Points where the reports disagree or I am uncertain

1. **cursor-path recommended Option A (teach `update_cursor` about the panel, then defer `draw_cursor_top` past `eui_draw`). That is not what shipped.** The panel draws its own arrow instead (`edit_mod.h:815-832`). The visible symptom is gone, but A1's underlying observation is still true and is B5 above. Do not apply A2 (deferring the draw) on top of the panel arrow, or you get two pointers over the sidebar.
2. **dpi-path's two central claims are now false**: `ALLOW_HIGHDPI` *is* set (`:18331`) and the window *is* resizable (`:18325`). Its analysis of `mscale` as the bridge between window points and drawable pixels is still correct and is what `g_uiBacking` is fed from at `:17237`.
3. **B2's exact height threshold is my arithmetic, not a measurement.** I derived ~500px for the ACT row and ~450px for SAVE from the layout constants. Measure it with the extended `--uitest` rather than trusting those numbers.
4. **B8 is unverified** at current line numbers. The surrounding code moved during the a concurrent edit's edits.
5. **Palette tab sizes differ between the two editors** and I cannot tell whether that is deliberate. Native carries 61 buildings / 22 units / 20 infantry / 32 terrain / 5 walls (`game/edit_tables.h:462-464`); web carries 22 / 16 / 13 / 19 / 10 with hardcoded curated lists. Native is offering the full cartridge table, web is offering a subset. That is a design question for the project owner, not a defect on either side.
6. **`game/cnc_eyes.cpp` and `game/edit_mod.h` are dirty and a concurrent edit may still be in them.** Every line number above is live as of this reading; re-anchor on the quoted text before editing, and coordinate file scope before touching `edit_mod.h`.