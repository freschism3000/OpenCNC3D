## Verdict up front

The click path is **not** broken. Clicks reach `eui_hit`, and `eui_hit`'s rectangles agree with `eui_draw`'s pixels exactly. What is broken is the **other end**: several of the panel's most button-looking controls are hit-tested and then thrown away, the handler prints nothing at all, and one very natural keystroke (ESC) silently puts a modal in front of the whole panel.

Also, **both files were changing during this analysis.** `edit_mod.h` changed twice while it was being read (`EUI_ACT` and a DPI scale factor `L.s` appeared mid-analysis, and a `--uitest` harness is uncommitted). Line numbers below are **live as of this moment**; my analysis snapshot is `<scratch>/cnc_eyes.snap.cpp` (sha1 `be86abb`) and `.../edit_mod.snap.h` (sha1 `c163239`).

---

## Hard evidence from an actual run

`playable/Editor.log` is the 22:51:04 run (170 s, ended 22:53) -- the one being reported. It proves several things:

- The binary was **not stale**: no `WARNING a source file is newer than this binary` line, and `strings playable/cnc_eyes` contains `MISSION EDITOR`, `NOTHING ARMED`, `REVERT`, `PLAY IN THIS WINDOW`. The panel commit `7273618` (21:44) is an ancestor of HEAD and was compiled in.
- `edit: missions/SCG01EA.BIN -- 3586 of 4096 cells are buildable ground` ⇒ `g_editOn` was **true** (that line only runs inside `if (g_editOn)` at `cnc_eyes.cpp:16565`).
- The drawable is **1280 wide** (`...65.9 px/cell horizontally at 1280 wide`, printed from `SDL_GL_GetDrawableSize`) ⇒ **not a Retina display, `mscaleX == mscaleY == 1.0`**. The whole drawable-vs-window-pixel class of bug is off the table on this machine.
- Mouse events **did** arrive and **were** dispatched: `SELECT|at=684.0,354.0`, `SELECT|at=683.0,330.0`, `SELECT|at=621.0,425.0`, three `CANCEL|deselect`, two `UNITREQ|stop`. All three x values are < 940 (= `sideX`), i.e. those particular clicks were on the map, not the panel.
- Every one of those map clicks reports `hit=none|cleared` -- reached via the `else { lpress = true; }` fall-through, which means **`g_editArmed` was `-1` at each of them**. Nothing was ever armed in 170 seconds.
- No `OPTIONS|` line ⇒ the pause dialog was not resumed/aborted in that run.

---

## 1. The SDL_MOUSEBUTTONDOWN handler, left-button branch

`game/cnc_eyes.cpp:17752` opens the branch; the left arm begins at `:17765`:

```c
} else if (e.button.button == SDL_BUTTON_LEFT) {
    float wx, wz;
    /* The editor takes the left button before the game does: its panel
       is drawn over everything, and a click on the map is a placement,
       not an order. eui_hit answers with one of its own controls, or
       EUI_MAP to say the click belongs to the world. */
    int earg = -1;
    const int ehit = g_editOn
        ? eui_hit((float)mc, (float)mr, dw, dh, &earg) : EUI_MAP;
    if (g_editOn && ehit != EUI_MAP) {
        switch (ehit) {
            case EUI_ITEM:
                g_editArmed = (g_editArmed == earg) ? -1 : earg;
                break;
            case EUI_TABC:
                if (g_editTab != earg) { g_editTab = earg; g_editScroll = 0; }
                break;
            case EUI_OWNER: g_editOwner = earg; break;
            case EUI_RADAR:
                /* jump the camera to that cell */
                g_camX = (float)(earg % 64) + 0.5f;
                g_camZ = (float)(earg / 64) + 0.5f;
                break;
            case EUI_SAVE: edit_save(); break;
            case EUI_PLAY: edit_request_play(); break;
            default: break;      /* MODE, DEAD: nothing yet */
        }
    } else if (g_editOn && g_editArmed >= 0 && g_editOverMap) {
        edit_place(g_editCellX, g_editCellY);
    } else if (minimap_hit((float)mc, (float)mr, dw, dh, &wx, &wz)) {
        g_camX = wx; g_camZ = wz;         /* radar jump */
    } else if (sb_click((float)mc, (float)mr, dw, dh, false)) {
        if (sb_take_options_request())
            opt_show(o->scen);
    } else if (sb_placing()) {
        int cx, cy;
        if (screen_to_cell((float)mc, (float)mr, dw, dh, &cx, &cy))
            sb_place_at(cx, cy);
    } else {
        lpress = true;
        pressC = mc; pressR = mr;
        g_bandOn = false;
    }
}
```

`mc`/`mr` are computed at `:17753-17754` as `e.button.x * mscaleX`, `e.button.y * mscaleY`, with `mscaleX = dw/winw` recomputed every frame at `:17200`. `dw`/`dh` are the same locals passed to `eui_draw(dw, dh)` at `:18095`. **The draw and the hit test are fed identical numbers.**

There is **no `printf`/`fprintf` anywhere in that switch** (verified: zero matches in the block). This is the single reason the bug is undiagnosable from the outside -- every other input path in the program logs (`SELECT|`, `CANCEL|`, `UNITREQ|`, `OPTIONS|`, `EDITPLAY|`, `edit: … at x,y`), so a silent panel is indistinguishable from a dead one.

## 2. The else-if chain above it

Order, top-level, inside `while (SDL_PollEvent(&e))`:

| line | guard | can it eat a right-hand-side click in edit mode? |
|---|---|---|
| 17454 | `if (g_autoplay) { … continue; }` | **No** -- `g_autoplay` is not set by `--edit` (`--edit` only sets `edit=1; g_sbOn=false`, `:15871`). Would drop every real-mouse button if it ever were set. |
| 17463 | `if (e.type == SDL_QUIT)` | No |
| 17467 | `else if (fs_handle_event(&e))` | **No** -- `fullscreen.h:53` returns 0 for anything that is not `SDL_KEYDOWN`. |
| 17477 | `else if (fxp_event(&e, dw, dh, …))` | **No, twice over** -- `fx_panel.h:257` returns 0 immediately unless `g_fxpOpen`, and even when open the button branch bails with `if (fx_ >= panelW) return 0;` (`fx_panel.h:311`). The F5 panel is anchored to the **left** edge; the editor panel is on the right. |
| 17479 | `else if (g_optOpen) { … }` | **YES -- this is the one real blocker.** See candidate #2. |
| 17583 | `else if (e.type == SDL_KEYDOWN)` | No |
| 17742 | `else if (e.type == SDL_MOUSEWHEEL)` | No (and it correctly calls `eui_wheel` first) |
| 17752 | `else if (e.type == SDL_MOUSEBUTTONDOWN)` | the handler above |

## 3. `eui_hit()` vs `eui_layout()` vs `eui_draw()`

Both `eui_draw` (`edit_mod.h:582`) and `eui_hit` (`edit_mod.h:903`) call the same `eui_layout` (`edit_mod.h:426`) with the same `fbw/fbh`, and `eui_draw` sets its ortho with `begin_overlay(fbw, fbh)` → `glOrtho(0, fbw, fbh, 0, …)` (`cnc_eyes.cpp:10143`), y-down pixel space, matching the hit test's coordinate convention. `begin_overlay` does **not** set the viewport, but `draw_frame` leaves it at `glViewport(0,0,rw,rh)` with `rw==fbw` when the Tier-2 chain is off (`cnc_eyes.cpp:12166,12177`), and `fx_present` restores `glViewport(0,0,g_fxW,g_fxH)` when it is on (`fx_post.h:886`). No mismatch.

I re-implemented `eui_layout`+`eui_hit` exactly and probed every drawn control (with a standalone simulation of the layout). At 1280×720 (the default, `cnc_eyes.cpp:15844`):

```
sideX=940  grid=317..493  tile=77.8x78.9  rows=2
  top strip / title      1110, 22 -> DEAD    *** NOTHING ***
  radar centre           1013,117 -> RADAR   camera jump
  right of radar (text)  1119, 91 -> DEAD    *** NOTHING ***
  MODE tab OBJECTS        999,213 -> MODE    *** NOTHING (default:) ***
  MODE tab TERRAIN       1110,213 -> MODE    *** NOTHING (default:) ***
  MODE tab ELEVATION     1221,213 -> MODE    *** NOTHING (default:) ***
  owner chip 0            986,258 -> OWNER   set owner
  category tab 0          978,295 -> TABC    switch category
  palette tile 0,0        989,359 -> ITEM    arm/disarm item
  lint panel             1110,540 -> DEAD    *** NOTHING ***
  UNDO / REDO / REVERT   ...,616 -> DEAD     *** NOTHING ***
  SAVE bar               1110,653 -> SAVE    edit_save()
  PLAY bar               1110,696 -> PLAY    edit_request_play()
```

Identical results at 1280×800 and 1920×1080. **The geometry is correct.** The dead controls are dead by *wiring*, not by arithmetic.

## 4. Is `g_editOn` true?

Yes. Set once at `cnc_eyes.cpp:16565` in `game_boot` from `o->edit`; `--edit` sets `edit=1` at `:15871`; the launcher (`playable/Editor.app/Contents/MacOS/editor-launch:230`) passes `--edit`. Proven at runtime by the `edit:` lines in Editor.log. The only thing that clears it is `edit_apply_play_request` (`edit_mod.h`, Ctrl+P), which prints `EDITPLAY|` -- absent from the log.

## 5. Mouse grab / capture / relative mode

**None exists.** Grep across all of `game/*.cpp|*.h`: zero `SDL_SetRelativeMouseMode`, zero `SDL_CaptureMouse`, zero `SDL_SetWindowGrab`. Only `SDL_ShowCursor(SDL_DISABLE)` (visibility, `:10789/:10832/:16700/:17009`) and one `SDL_WarpMouseInWindow` at `:17187` that parks the pointer at window centre once at startup. Ruled out.

## 6. MOUSEBUTTONUP / band-select

`cnc_eyes.cpp:17856`. A panel press takes the `eui_hit` arm and **never sets `lpress`**, so the `else if (… && lpress)` body (band-select / `ui_click_action`) cannot fire for a panel click. `sb_pointer_up()` runs unconditionally but only clears `g_sbPress/g_sbPressCol` (`cnc_sidebar.h:1861`). **Not the cause.** One residual wart: a press that starts on the map and is *released* over the panel still runs `ui_click_action` at panel coordinates, because nothing cancels `lpress` when the pointer crosses into the sidebar.

---

## Ranked candidate causes

**1. Six of the panel's most button-shaped controls are hit-tested and then discarded.** `cnc_eyes.cpp:17786` -- `default: break; /* MODE, DEAD: nothing yet */`. `EUI_MODE` (the three 46px-tall `OBJECTS / TERRAIN / ELEVATION` tabs, drawn at `edit_mod.h:584-597`, the first thing under the radar) and `EUI_ACT` (the `UNDO / REDO / REVERT` row, drawn at `edit_mod.h:776`) both fall into `default:`. `EUI_ACT` did not even *exist* in the build that was run -- the row had no hit rectangle at all; `EUI_ACT` was later added to the enum (`edit_mod.h:330`) and to `eui_hit` (`:944-950`) in the last few minutes, but **the switch in `cnc_eyes.cpp` still has no `case EUI_ACT:` as of right now**, so those three buttons are *still* dead. This alone reproduces "clicking the panel does nothing" for the two rows a user reaches for first.

**2. ESC opens the 1995 pause dialog on top of the editor and swallows every subsequent click.** `cnc_eyes.cpp:17593` -- `case SDLK_ESCAPE: if (!sim_over) opt_show(o->scen); break;`. `opt_show` (`:10793`) has **no `g_editOn` guard** and sets `g_optOpen = true` at `:10830`. From then on `else if (g_optOpen)` at `:17479` sits above the mouse branch and consumes 100% of button events, while `eui_draw` at `:18095` keeps drawing the panel (it is gated on `g_editOn` only). ESC is exactly what a user presses to get out of the editor -- the launcher's own comment says *"ESC is how you leave the editor"* -- so this is a one-keystroke trap into a totally dead panel. Not the cause of the 22:51 run specifically (no `OPTIONS|` line), but the highest-severity latent one. The same hole exists for `*` → `cheat_show` (`:10779`), also unguarded.

**3. Arming is a toggle, so a double-click disarms.** `:17777` -- `g_editArmed = (g_editArmed == earg) ? -1 : earg;`. When nothing appears to happen, the reflex is to click again; the second click un-arms. The log's three `SELECT|…|hit=none|cleared` prove `g_editArmed == -1` at every map click in that session -- consistent with "armed a tile, clicked it again, then clicked the map and nothing was placed."

**4. Zero instrumentation in the editor's click path.** No `printf` in the entire `switch (ehit)` block. Combined with #1 and #3, a working click and a dropped click are literally indistinguishable in `Editor.log`. Every sibling input path logs; this one does not. This is why the report is "does NOTHING" rather than "the TERRAIN tab does nothing".

**5. Weak visual acknowledgement on the controls that *do* work.** `EUI_OWNER` changes a 2px colour strip and a text colour (`edit_mod.h:600-607`); `EUI_ITEM` changes a fill and a frame colour. There is no press state, no hover state, no click sound. At 1280×720 the palette shows only **2 rows × 4 = 8 of 61 buildings**, and the only hint that there are more is a small `1-8 OF 61  WHEEL SCROLLS` caption.

**6. `edit_update_hover` does not exclude the sidebar.** `edit_mod.h:78-84` -- `g_editOverMap = (mouseC >= 0.0f) && screen_to_cell(...)` with no `mx < sideX` test. The cell cursor and footprint keep being computed and drawn for a map cell *behind* the panel while the pointer is over the panel. Harmless for clicks (the `ehit != EUI_MAP` arm wins first) but it makes the panel look unresponsive-yet-alive, and it is the mirror image of the bug that would occur if the branch order were ever reversed.

**7. `gridH` clamp can swallow SAVE/PLAY on a short window.** `edit_mod.h:465` -- `if (L->gridH < 60 * S) L->gridH = 60 * S;` while `saveY/playY` stay pinned to the bottom. `eui_hit` tests the grid rectangle **before** SAVE and PLAY (`:932` vs `:951-952`) and returns `EUI_DEAD` for any grid-interior miss, so below roughly 463 drawable pixels of height the grid rect overlaps the SAVE bar and eats it. Not the reported case at 720p, but a real trap once the window is resizable.

**8. `--autoplay` would silently drop every real click.** `:17454-17459` filters `e.button.which != AP_WHICH`. Not reachable from `--edit`, listed for completeness.

**Ruled out:** stale binary; `g_editOn` false; drawable-vs-window pixel mismatch (drawable is 1280, scale 1.0); `fs_handle_event`; `fxp_event`; mouse grab/capture/relative mode; MOUSEBUTTONUP or band-select cancelling the action; viewport left in an FX render-target size.

## Cheapest fixes, in order

1. Add `case EUI_ACT:` and `case EUI_MODE:` to the switch at `cnc_eyes.cpp:17786` -- even if only to `fprintf(stderr, "edit: %s is not implemented yet\n", …)`. A control that says "not yet" is not a broken control.
2. Guard `opt_show` (and `cheat_show`) with `if (g_editOn) return;`, or make ESC in edit mode leave the editor.
3. Log one line per panel hit: `fprintf(stderr, "EUI|hit=%d|arg=%d|at=%d,%d\n", ehit, earg, mc, mr);` -- that single line would have answered this question in ten seconds instead of a full audit.
4. Make `EUI_ITEM` set rather than toggle, with an explicit right-click/ESC to disarm.

No files were changed.