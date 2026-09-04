All line numbers are against `game/cnc_eyes.cpp` at md5 `1e6409d2e13ddfe88d24b8e0c30889f3`, 18320 lines. **Warning: a concurrent edit is editing this file right now** -- it grew from 17477 to 18320 lines mid-analysis, and `game/cnc_eyes.cpp` + `game/edit_mod.h` are both dirty in the worktree. Re-anchor before editing.

## 1. Which function draws the pointer, and from where

**`draw_cursor_top(int fbw, int fbh, bool dialogOpen = false)` -- cnc_eyes.cpp:9512-9531.** It calls `cur_draw()` (cursors.h:279) wrapped in its own `begin_overlay`/`end_overlay` pair (9522 / 9530).

It is called **from inside `draw_frame()`**, at the very end, in two mutually exclusive branches:

```
12895:    const bool pointerNow = !g_optOpen && !g_gameOver.valid;
12896:    if (pointerNow && !fxp_is_open())
12897:        draw_cursor_top(fbw, fbh);
12903:    fx_present();
12904:    fxp_draw(fbw, fbh);
12905:    if (pointerNow && fxp_is_open())
12906:        draw_cursor_top(fbw, fbh);
```

`draw_frame` ends at 12907. The live loop (`game_loop`, 17100) calls `draw_cursor_top` separately **only for the pause dialog**, at 18085.

Two other pieces of the same subsystem:
- `update_cursor(fbw, fbh)` -- 9533. Called once per frame from the live loop at **18046**, *before* `draw_frame` (18074). It picks the shape and sets `g_cursorOnMap` / `g_cursorWX/WZ` / `g_cursorScale`.
- The **3D world cursor** -- `c3d_draw(cur_type(), g_cursorWX, g_cursorWZ)` at **12813**, inside `draw_frame`'s world pass (guard 12807-12815). Over the map, this is the pointer; the 2D sprite deliberately stands down (9520).

## 2. SDL_ShowCursor

Hidden in **`game_boot`, cnc_eyes.cpp:16687**, and only if the DOS cursor art loaded:

```
16685:        if (cur_init(g_dbPack, cerr, sizeof(cerr))) {
16687:            SDL_ShowCursor(SDL_DISABLE);
16688:        } else {
16689:            g_cursorOn = false;   /* OS arrow stays, loud warning */
```

Re-hidden defensively at 10788 (`cheat_show`), 10831 (`opt_show`), 16996 (`game_visuals_open`).

Re-enabled at **17071** (exit of `game_visuals_open`) and **18215** (`game_shutdown`). **Never re-enabled during a live mission** -- so during gameplay there is no OS pointer to fall back on.

The comment at 12881 ("SDL_ShowCursor in main") is stale; the call lives in `game_boot`.

## 3. Does the editor panel cover the pointer? -- CONFIRMED, and for two independent reasons

**(a) Ordering.** `game_loop`:

```
18074:        draw_frame(dw, dh);
18077:        if (g_editOn) {
18078:            edit_update_hover(...);
18079:            edit_draw_footprint(dw, dh);
18080:            edit_draw_overlay(dw, dh);
18081:            eui_draw(dw, dh);              /* the editor panel, over everything */
18082:        }
```

Both pointer draws (12897 / 12906) happen inside `draw_frame`, which returns at 18074. `eui_draw` (edit_mod.h:509) then paints the panel onto the default framebuffer at 18081. The panel wins. The comment at 12889-12891 records that this exact failure was already found once for the F5 panel and fixed by deferring past `fx_present` -- the editor panel is drawn even later, outside `draw_frame` entirely, so that fix does not reach it.

**(b) Worse: over the editor panel there is usually no 2D sprite to cover.**

`--edit` sets `g_sbOn = false` (15870; also `edit_mod.h:1513` on Ctrl+P round-trips). `sb_over_panel` then returns false immediately (`cnc_sidebar.h:1044`), so `update_cursor`'s early-out never fires in edit mode:

```
9544:    if (sb_over_panel(col, row, fbw, fbh) || sb_placing() || fxp_over_panel(col, fbh)) {
9545:        g_cursorOnMap = false;
9546:        cur_set(MOUSE_NORMAL);
9547:        return;
9548:    }
9550:    const PickInfo p = pick_at(col, row, fbw, fbh);
9553:    g_cursorOnMap = p.onMap;
```

`update_cursor` knows about the DOS sidebar and the F5 panel, and **nothing about the editor panel**. With `g_sbOn` false the tactical viewport is the whole window (`sb_tactical_right` returns `fbw`, cnc_sidebar.h:990), so the pick under the panel hits terrain and `g_cursorOnMap` stays **true**. `draw_cursor_top` then bails at its first guard:

```
9520:    if (g_cursorOnMap && !dialogOpen && c3d_have()) return;
```

So over the panel the only pointer drawn is the 3D mesh at 12813, standing in the world underneath everything. **Simply moving the call after `eui_draw` will not work** -- it will early-return.

## 4. Cleanest places to fix

### Option A (recommended, 2 edits -- mirrors the F5-panel precedent exactly)

**A1. Teach `update_cursor` about the editor panel.** At **9544**, add a third term to the existing disjunction so `g_cursorOnMap` goes false and the shape becomes `MOUSE_NORMAL` over the panel. This is precisely what `fx_panel.h:140-144` documents itself as doing ("It also clears `g_cursorOnMap`, which stops the world cursor being drawn on the ground under the panel and stops the pick running at all").

The natural predicate already exists: `eui_hit(mx, my, fbw, fbh, &arg) != EUI_MAP` (edit_mod.h:815-861 -- `EUI_MAP` = over the map, `EUI_DEAD` = title bar, everything else = a control).

> **Ordering hazard.** `update_cursor` is at 9533, but `#include "edit_mod.h"` is at **10179** and `g_editOn` is declared at **edit_mod.h:56**. Neither `g_editOn` nor `eui_hit`/`eui_layout` is in scope at 9544. You need a forward declaration alongside the ones already at **8173-8174** (`static void begin_overlay(int,int); static void end_overlay(void);`) -- the same trick the file already uses for the overlay helpers, with the body staying in edit_mod.h.

**A2. Defer the draw.** Exclude the editor panel from `pointerNow` at **12895**, and add `draw_cursor_top(dw, dh);` immediately after **18081**. With A1 done, `g_cursorOnMap` is false over the panel, so the 9520 guard passes and the sprite lands on top; over the map the guard still fires and you keep the 3D mesh at 12813 unchanged.

> If you skip the 12895 gate you get two blits per frame (one buried under the panel, one on top). Not visually wrong, but wasteful and the buried one goes through the post chain.

### Option B (1 edit, no header re-ordering)

After **18081**, inside the `if (g_editOn)` block:

```
int a;
if (!g_optOpen && !g_gameOver.valid &&
    eui_hit((float)mouseC, (float)mouseR, dw, dh, &a) != EUI_MAP)
    draw_cursor_top(dw, dh, /*dialogOpen=*/true);
```

Everything named is already in scope at 18081 (edit_mod.h included at 10179). `dialogOpen=true` forces past the 9520 guard. `mouseC`/`mouseR` are framebuffer pixels kept in lockstep with `g_mouseScrC/R` (17507-17511, 17870-17872), so they are the right coordinates.

**Hazards:** (i) `dialogOpen=true` is a lie about the state -- it means only "force the sprite"; either comment it or rename the parameter. (ii) The *shape* will still be whatever `update_cursor` picked from the world under the panel -- you get a move/attack reticle sitting on the editor UI. You need `cur_set(MOUSE_NORMAL)` before the call, or A1. (iii) Still leaves the 12897 blit happening underneath.

### Option C (avoid, or at least place it carefully)

Drawing it from inside `eui_draw` (edit_mod.h). `draw_cursor_top` *is* visible there (defined 9512, header included 10179), but:

> **Do not put it before `end_overlay()` at edit_mod.h:707.** `draw_cursor_top` opens its own `begin_overlay`/`end_overlay` (9522/9530), and `end_overlay` (10166-10173) re-enables `GL_DEPTH_TEST` and `glDepthMask(GL_TRUE)`. Nested inside `eui_draw`'s own overlay pass (opened at edit_mod.h:516), every panel quad drawn after that point would start depth-testing against whatever the world pass left in the depth buffer. If you go this way, put the call **after** line 707, outside the pairing.

### State notes that apply to all options

- `begin_overlay` (10143-10156) disables depth test, depth mask, `GL_TEXTURE_2D` and `GL_ALPHA_TEST`. `end_overlay` (10166-10173) restores **only** depth test and depth mask -- not texture, alpha test, or blend.
- `cur_draw` enables `GL_TEXTURE_2D` + `GL_ALPHA_TEST` itself and turns both off again (cursors.h:327-338). It never touches `GL_BLEND`; its cutout is alpha-test, not blending.
- `eui_draw` disables `GL_BLEND` at edit_mod.h:706 before its `end_overlay`. So a bare `draw_cursor_top()` called after `eui_draw` returns is state-clean -- same conditions the DOS-sidebar path already draws under.
- Drawing after `fx_present()` (12903) is already proven safe by the existing 12906 call, so there is no framebuffer-binding problem at 18081.

### Two other call sites that repeat the same order

If the pointer should also appear in screenshots over the panel, these need the same treatment:
- the script `shot` verb -- `draw_frame` at 15564, `eui_draw` at 15571
- `game_run_shot` -- `draw_frame` at 16884, `eui_draw` at 16887

### One incidental observation

`edit_update_hover` runs at **18078**, *after* `draw_frame` (18074), so `g_editOverMap` / `g_editCellX` that `eui_draw` reads are one frame behind the pick `update_cursor` did at 18046. Harmless for the panel, but if you key the new pointer draw off `g_editOverMap` instead of `eui_hit`, you inherit that one-frame lag.