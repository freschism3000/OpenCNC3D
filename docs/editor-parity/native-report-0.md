All line numbers below are against `game/cnc_eyes.cpp` at sha1 `be86abb` (18320 lines). **Caveat: this file was changing during the analysis** -- it grew from 17477 to 18320 lines while it was being read, so re-anchor on the quoted text if the numbers have drifted. No files were changed.

---

**1. Default window size: 1280x720**

- `game/cnc_eyes.cpp:15844` -- inside `game_parse_args`: `int fbw = 1280, fbh = 720;`
- Overridden by `--w N` / `--h N` at `cnc_eyes.cpp:15958-15959`.
- Copied out at `cnc_eyes.cpp:16213`: `out->w = fbw; out->h = fbh;`
- A second, identical default lives in the same function's sibling in the shipping shell, `app/cnc3d.cpp` (that binary owns the real window; `cnc_eyes.cpp`'s `main` is the standalone renderer, guarded by `#ifndef CNC3D_NO_MAIN` at `cnc_eyes.cpp:18218`).

**2. `SDL_CreateWindow` and flags**

`game/cnc_eyes.cpp:18246-18247`:
```c
SDL_Window* win = SDL_CreateWindow("CNC3D eyes", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, o.w, o.h, flags);
```
Flags assembled at `cnc_eyes.cpp:18239-18245`:
- `SDL_WINDOW_OPENGL` -- always (18239)
- `SDL_WINDOW_HIDDEN` -- iff `o.shot` (headless PNG mode) (18240)
- `SDL_WINDOW_FULLSCREEN_DESKTOP` -- iff `fs_start_fullscreen && !o.shot` (18244)
- `SDL_WINDOW_RESIZABLE` -- **only** `if (g_resizeW > 0 && g_resizeH > 0)` (18245), i.e. only when the `--resize W H` test flag was passed with two positive integers (parsed at `cnc_eyes.cpp:16057-16058`, declared at `cnc_eyes.cpp:4037`). In a normal run the window is **not** user-resizable; `--resize` exists purely so gate G37 can reproduce the fullscreen "drawable != asked-for size" condition without seizing the display (comment at `cnc_eyes.cpp:16051-16056`).

**`SDL_WINDOW_ALLOW_HIGHDPI` is never set anywhere in the repo** -- I grepped all `.c/.cpp/.h`; the only hit is a comment in `menu/dosmenu_shell.c:77` explicitly noting "neither screen asks for `SDL_WINDOW_ALLOW_HIGHDPI`". So on macOS today drawable == window points and every mscale is 1.0; the conversion code is defensive, not active.

**3. Drawable size vs window size; mscaleX/mscaleY**

The stated invariant (`cnc_eyes.cpp:4051-4054`, and again at `cnc_eyes.cpp:17176-17179`): SDL delivers mouse coordinates in **window points**; every screen coordinate in the camera, picking, sidebar and HUD code is in **drawable pixels**. `mscale` bridges the two.

Computed per frame at the top of `game_loop`, `game/cnc_eyes.cpp:17205-17211`:
```c
int dw = fbw0, dh = fbh0;
SDL_GL_GetDrawableSize(win, &dw, &dh);
int winw = fbw0, winh = fbh0;
SDL_GetWindowSize(win, &winw, &winh);
const float mscaleX = winw > 0 ? (float)dw / (float)winw : 1.0f;
const float mscaleY = winh > 0 ? (float)dh / (float)winh : 1.0f;
```
So `mscale = drawable / window`, and it is used as a **multiplier on incoming mouse coords** (window points → drawable pixels): `cnc_eyes.cpp:17226-17227`, `17740-17743`, `17810-17811`, `17953-17954`, and the wheel/hit-test calls around `17734-17742`. The one inverse use is `opt_push_abort_click` at `cnc_eyes.cpp:10160-10172`, which *divides* to synthesize a click back in window points.

Two more copies of the same ratio under different names:
- Options-dialog modal loop, `cnc_eyes.cpp:17004-17011` (`sx`/`sy`).
- Menu shell, `menu/dosmenu_shell.c:73-80` (`DMS::px`/`py`), used inverted in `dms_item_window_rect` (`dosmenu_shell.c:96-99`) to go drawable → points.

`o->w/o->h` are deliberately renamed `fbw0/fbh0` in `game_loop` (`cnc_eyes.cpp:17101-17114`) so that using the asked-for size where the actual drawable is needed is a compile error -- the comment records that exact bug in the band-select drag under fullscreen.

**4. Retina, drawable = 2x window: a drawable-pixel-sized element is PHYSICALLY SMALLER, by a factor of 2 (half the size).**

One drawable pixel is half a window point wide. An element authored as N drawable pixels covers N/2 points; an element authored as N points covers 2N drawable pixels. Generally the physical size ratio is `1 / mscale`. So UI sized in drawable pixels must be multiplied by `mscale` to match a point-sized element -- which is precisely why the sidebar derives its magnification from the drawable height rather than from points (`game/cnc_sidebar.h:939-961`: `int s = fbh / H6_BAR_H;`, whole-number only).

**5. Resize events: never handled. Nothing is needed, because everything is re-read per frame.**

- The only `SDL_WINDOWEVENT` handled in the file is `SDL_WINDOWEVENT_LEAVE` at `cnc_eyes.cpp:17910-17913` (clears the cursor). There is **no** `SDL_WINDOWEVENT_RESIZED` or `SIZE_CHANGED` case anywhere in `cnc_eyes.cpp`, and none in the repo.
- The trailing comment at `cnc_eyes.cpp:17473` (`else if (fs_handle_event(&e)) { /* window resized, nothing else to do */ }`) is about the fullscreen toggle, not a resize event. `game/fullscreen.h:45-79` only inspects `SDL_KEYDOWN` (CMD+F / ALT+ENTER) and calls `SDL_SetWindowFullscreen`.
- Viewport and projection are rebuilt **every frame from the freshly-queried drawable size**, which is what makes resize handling unnecessary:
  - `game_loop` re-queries at `cnc_eyes.cpp:17206` and passes `dw/dh` to `draw_frame(dw, dh)` at `cnc_eyes.cpp:18074`.
  - `draw_frame` sets `g_fbW/g_fbH` (`cnc_eyes.cpp:12154`), calls `glViewport(0, 0, rw, rh)` (`cnc_eyes.cpp:12177`), then `clamp_camera(fbw, fbh)` and `set_camera(fbw, fbh)` (`cnc_eyes.cpp:12208-12210`) -- the comment there says "Zoom changes and window resizes are covered for free."
  - The options dialog's own loop does the same: `SDL_GL_GetDrawableSize` at `cnc_eyes.cpp:17004`, `glViewport(0, 0, dw, dh)` at `cnc_eyes.cpp:17052`.
  - Tier-2 post-chain render targets reallocate on size change via `fx_rt_init`, `game/fx_gl.h:350-351` (`if (rt->fbo && rt->w == w && rt->h == h) return 1;` then free+recreate).
- The `--resize` path at `cnc_eyes.cpp:18252-18259` calls `SDL_SetWindowSize` + `SDL_PumpEvents` and just *prints* the resulting drawable; it installs no handler.

**6. Existing DPI scale factor a UI layer could use: no global one exists.**

What exists:
- `mscaleX` / `mscaleY` -- function-local `const float`, `cnc_eyes.cpp:17210-17211`, alive only inside `game_loop`'s frame body. Not exported.
- `sx` / `sy` -- the same ratio, local to the options loop, `cnc_eyes.cpp:17007-17008`.
- `DMS::px` / `DMS::py` -- per-shell struct fields, `menu/dosmenu_shell.c:78-79`. Also not global.
- `g_fbW` / `g_fbH` -- `cnc_eyes.cpp:9841` (`static int g_fbW = 1280, g_fbH = 720;`), refreshed each frame at `cnc_eyes.cpp:12154`. These are the **drawable size**, file-static, and are the nearest thing to a globally readable DPI-aware quantity.
- `g_dbScale` / `sb_scale()` -- `game/cnc_sidebar.h:558` and `:974`, with `g_h6Scale` at `:530`. This is a whole-number *art* magnification derived from drawable height in `sb_layout` (`cnc_sidebar.h:919-971`), not a DPI factor -- but it is the existing precedent for "UI size follows drawable pixels," and it is already global-ish and read all over the HUD code (e.g. `cnc_eyes.cpp:9923`, `12870-12874`).

If UI code needs a DPI factor, there is nothing to reuse today: it would have to be added (the natural home is beside `g_fbW/g_fbH` at `cnc_eyes.cpp:9841`, written in `draw_frame` at `cnc_eyes.cpp:12154` from the same `SDL_GetWindowSize` ratio the loop already computes). Note again that with `ALLOW_HIGHDPI` unset it would read 1.0 on every current platform.