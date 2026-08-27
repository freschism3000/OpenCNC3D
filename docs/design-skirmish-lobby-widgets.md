# The skirmish lobby: what to reuse, what is missing, and the order to build it

Research only. No code was changed to write this. Every line number below was read out of
the working tree at the time of writing and every negative claim ("there is no X") was
confirmed with a substring count over the whole tree rather than with a single grep.

The question this answers: **what does this project already have for drawing a dialog, and
what would a skirmish lobby need that does not exist yet?**

---

## 0. The headline, because it changes the estimate

`docs/design-skirmish.md` section 6.5 says:

> `menu/dosops.c` gives a list and a plate for free and nothing else: `menu/` contains no
> gauge, check list, drop list, edit box, slider or colour row, so every option widget is
> new work.

That sentence is **literally true and practically misleading**, and it is the reason the
lobby was skipped. It is scoped to the `menu/` directory. One directory over, `game/dosopt.c`
is a 1390 line dialog toolkit that already implements:

* a dialog box, a caption with filigree and an underline,
* a green button with pressed / selected / disabled colourways and auto-sized labels,
* **sliders** with the engine's own value-to-pixel arithmetic, grab offset and drag,
* **check boxes**,
* a **latched two-button radio pair**,
* a **scrolling multi-column list** with a selected row, a keyboard walk and a wheel,
* a **page stack** with per-page item counts, labels, rectangles, disabled rules and Escape,
* a **keyboard focus walk** across mixed control kinds that steps over disabled items,
* a **binding seam** so the screen owns settings and the host owns consequences,
* and it is plain C89 with no SDL, no GL and no allocation, drawing into the same
  `DB_Surface` the menu draws into, with the same palette indices.

It is not in `menu/` because it started life as the pause dialog. Nothing about it is
in-game specific: `game/build.sh:42-44` compiles it standalone precisely to keep it that
way, and `game/gate_optlayout.c` links it with no window at all.

So the honest estimate is not "every option widget is new work". It is:

| Lobby control | Status |
| --- | --- |
| Map list | exists twice (`dosops.c`, and the jukebox list in `dosopt.c`) |
| Check boxes (tiberium, crates, superweapons, bases) | exists, reusable verbatim |
| Side pair (GDI / Nod) | exists as the Classic/Enhanced latched pair |
| AI count, starting credits | slider exists; **printing the value beside it does not** |
| Buttons, dialog, caption, keyboard walk, Escape | exists |
| Map preview panel | the **data** landed today; there is **no reader in C** |
| Colour row | new, and see 2.6: it would currently be a lie |
| Drop list, numeric stepper, edit box | nothing anywhere in the tree |

Two of those are real work. The rest is assembly.

---

## 1. Every reusable primitive and widget, with its address

### 1.1 The surface and the raw primitives (`game/dosbar.c`, `game/dosbar.h`)

C89, no dependencies past libc. Everything else in this table is built on these.

| What | Call | Where |
| --- | --- | --- |
| 8 bit 320x200 surface, cleared | `db_surface_init(s, w, h, storage)` | `game/dosbar.c:191` |
| Surface over storage that must NOT be cleared | see `camp_surface` | `app/campaign.c:712` |
| Clip rectangle, inclusive | `db_clip(s,x0,y0,x1,y1)` / `db_clip_reset(s)` | `dosbar.c:208` / `:200` |
| Filled rect, inclusive corners | `db_fill_rect(s,x0,y0,x1,y1,c)` | `dosbar.c:222` |
| One pixel, clipped | `db_put_pixel(s,x,y,c)` | `dosbar.c:216` |
| Horizontal / vertical line | `db_line_h(s,x0,x1,y,c)` / `db_line_v(s,x,y0,y1,c)` | `dosbar.c:247` / `:252` |
| The four GREY box styles | `db_draw_box(s,x,y,w,h,style,filled)` | `dosbar.c:259`, table at `:261-268` |
| Shape blit, index 0 transparent | `db_draw_shape(s,sh,frame,x,y)` | `dosbar.c:283` |
| Shape blit centred on a point | `db_draw_shape_centered(s,sh,frame,cx,cy)` | `dosbar.c:304` |
| Translucent shape (the build clock) | `db_draw_shape_ghost(...)` | `dosbar.c:317` |
| Text width in pixels | `db_string_width(f,text,xspacing)` | `dosbar.c:383` |
| Print text through a 16 entry class palette | `db_print(s,f,text,x,y,fontpal,xspacing)` | `dosbar.c:393` |
| Flat font palette (`TPF_NOSHADOW`) | `db_font_palette(fp,fore,back)` | `dosbar.c:354` |
| Gradient font palette (`TPF_6PT_GRAD`) | `db_font_palette_grad(fp,fore,back)` | `dosbar.c:370` |
| Load a pack | `db_pack_load(path,err,errlen)` | `dosbar.c:47` |
| Find a shape / a font by name | `db_shape(p,name)` / `db_font(p,name)` | `dosbar.c:167` / `:176` |
| Surface to RGBA, once, at the end | `db_surface_to_rgba(s,pal8,rgba,want_alpha)` | `dosbar.c:716` |

Two properties of `db_print` that a new screen must plan around:

* **It does not truncate.** It clips to the surface and never to a caller's box
  (`dosbar.c:393-413`). A label wider than its button prints out of the button, across
  whatever is next to it, and off the dialog. This has already shipped as a visible bug
  once; `game/gate_optlayout.c:39-47` carries the account and measures against it.
* **Output colour 0 does not paint.** That is how transparency works, in the font printer
  and in the shape blitter alike.

Font spacing is not free either. `DB_FONT6_XSPACING` is `-2` and `DB_FONT6_YSPACING` is
`-1` (`game/dosbar.h:110-114`), derived from `Simple_Text_Print` for
`TPF_6POINT|TPF_NOSHADOW`. Every existing call passes those constants. A new screen that
passes 0 will look subtly wrong beside every other screen.

There is also a grey 1995 text button, `db_text_button` at `dosbar.c:416`, but it is
`static` and it is the sidebar's colourway (Repair / Sell / Map), not the green dialog one.
It is not reachable and it is not what a lobby wants.

### 1.2 The green dialog look (`menu/dosmenu.c`)

These two are already shared deliberately so the main menu and the Special Ops list cannot
drift apart (`menu/dosmenu.h:221-225`). A lobby should call the same two.

| What | Call | Where |
| --- | --- | --- |
| Green raised / pressed / disabled button box | `dm_green_button(s,x,y,w,h,pressed,disabled)` | `menu/dosmenu.c:177` |
| Green bordered dialog box on black | `dm_green_dialog(s,x,y,w,h)` | `menu/dosmenu.c:213` |
| The TITLE.CPS plate on its own | `dm_draw_plate(s,p)` | `menu/dosmenu.c:396` |
| The DOS mouse pointer | `dm_draw_cursor(s,p,mx,my)` | `menu/dosmenu.c:448` |

`dosopt.c` carries private copies of the first two, `dopt_button_box` at `dosopt.c:936` and
`dopt_dialog_box` at `:964`, identical in geometry and using the same palette indices under
different names. That duplication already exists; a lobby should not make it a third copy.

### 1.3 The dialog toolkit (`game/dosopt.c`, `game/dosopt.h`)

This is the richest thing in the tree and the list below is exhaustive.

**Widget kinds it implements, how each is drawn, how each is hit tested**

1. **Button.** Drawn by `dopt_draw_button` (`dosopt.c:1001`): `dopt_button_box` for the
   bevel, then the label centred with `textbtn.cpp`'s own expression
   `x + (w>>1) - 1 - (width>>1)` at `y + 1`. Three ink states: bright when pressed or
   focused, medium otherwise, and a separate disabled palette written out longhand at
   `:1014-1016`. Hit tested by rectangle in `dopt_hit_test` (`:507`), which skips disabled
   items because `gadget.cpp:632` skips them.

2. **Slider (gauge).** Drawn by `dopt_draw_slider` (`dosopt.c:1029`): a `GREEN_DOWN` body,
   the travelled part filled in `DOPT_BRIGHT_GREEN`, then a 4 pixel raised thumb pulled
   back at the top of travel so it cannot hang off the end. The value arithmetic is the
   engine's, in integers: `dopt_value_to_pixel` (`:585`) and `dopt_pixel_to_value` (`:599`)
   over a usable span of `width - 2`. Hit testing is the rectangle again, but the press
   path is special: `dopt_press` (`:633`) recognises a slider by index range, computes a
   grab offset the way `gauge.cpp:288-316` does, walks that offset back so the grab does
   not shift the value by a pixel of rounding, and records `st->drag`. `dopt_motion`
   (`:668`) and `dopt_release` (`:853`) then follow the pointer. Left and right arrows
   nudge by an eighth of travel (`dopt_key`, `:892-907`).

3. **Slider label.** `dopt_draw_slider_label` (`dosopt.c:1069`) implements **two different
   1995 rules** and picks between them by item: full width sliders get their caption above
   and left aligned with `Slower` / `Faster` under the ends, narrow ones get it right
   aligned five pixels to the left and two rows up. The comment at `:1055-1068` explains
   why using one rule for all five was visibly wrong.

4. **Check box.** `dopt_draw_check` (`dosopt.c:1102`). There is no checkbox primitive in
   the 1995 toolkit, so this is the dialog's own inset well: `dopt_button_box(pressed=on)`
   at 7x7, with a `DOPT_BRIGHT_GREEN` fill inside when ticked. The comment at `:1096-1101`
   is the design argument. **Its hit rectangle is the whole row, not the 7 pixel box**
   (`dopt_item_rect`, `:480-486`), which is the right call and should be copied.

5. **Latched radio pair.** Not a widget type, a technique: `dopt_draw_visuals`
   (`dosopt.c:1114`) draws Classic and Enhanced as ordinary buttons, then repaints
   whichever is chosen in the pressed style with the bright label over the top
   (`:1126-1139`). `dopt_activate` (`:691-706`) enforces exclusivity and refuses a click on
   the one already chosen. This is exactly what a Side row needs.

6. **Scrolling list with columns.** The jukebox: the well is drawn at
   `dopt_draw_sound:1282-1293` (black fill plus a `GREEN_BOX` frame), one row at
   `dopt_draw_track` (`:1230`) with three tab stops at 55 / 72 / 90 and a filled highlight
   bar for the selected row. Scrolling state is `trkTop` / `trkSel`, moved by `dopt_scroll`
   (`:242`), kept visible by `dopt_track_show` (`:255`), and walked by the arrow keys when
   the list has focus (`dopt_key:877-888`). The row under the pointer is worked out from
   `st->lastmy` in `dopt_activate` (`:786-803`), and a second click on the already selected
   row activates it.

7. **Glyph button.** `dopt_draw_shapebtn` (`dosopt.c:1208`) draws the button box and then a
   filled square or a triangle in place of a label, for the two shape buttons whose art is
   in no archive we bake. The pattern is worth knowing about: it is how you get an icon
   button without art.

8. **On / Off toggle button.** Not a separate widget: an ordinary button whose label is
   recomputed each frame (`dopt_item_label:110-111`, drawn at `:1326-1327`).

9. **Caption with filigree and underline.** `dopt_caption` (`dosopt.c:977`): two
   `OPTIONS.SHP` frames centred at the box's top corners, the text centred, and a bright
   rule under it the exact width of the text.

10. **Dialog box.** `dopt_dialog_box` (`dosopt.c:964`), an inset green rectangle on opaque
    black. Same shape as `dm_green_dialog`.

**The machinery around the widgets, which is the part that is genuinely expensive to write
and is already written**

| What | Call | Where |
| --- | --- | --- |
| How many items this page has | `dopt_page_count(st)` | `dosopt.c:85` |
| The label of item i on this page | `dopt_item_label(st,i)` | `dosopt.c:97` |
| Is item i drawn but unclickable | `dopt_item_disabled(st,i)` | `dosopt.c:118` |
| The rectangle of item i on this page | `dopt_item_rect(st,i,&x,&y,&w,&h)` | `dosopt.c:350` |
| What is under the pointer, honouring disabled | `dopt_hit_test(st,mx,my)` | `dosopt.c:507` |
| Keyboard walk, wrapping, skipping disabled | `dopt_next_item(st,item,delta)` | `dosopt.c:523` |
| Auto-size every button to the widest label | `dopt_layout(st,pack)` | `dosopt.c:160` |
| Press / motion / release / key, in DOS pixels | `dopt_press`, `dopt_motion`, `dopt_release`, `dopt_key` | `:633`, `:668`, `:853`, `:871` |
| What the caller must act on | `DOPT_ACT_*` | `game/dosopt.h:556-566` |
| Keys, so the module never includes SDL | `DOPT_KEY_*` | `game/dosopt.h:569-574` |
| The host binding seam (NULL is legal) | `DOPT_Bind` | `game/dosopt.h:613-623` |
| Draw the current page | `dopt_draw(s,p,st)` | `dosopt.c:1331` |

Note the shape of `dopt_page_count`, `dopt_item_label`, `dopt_item_disabled` and
`dopt_item_rect`: four switches on `st->page` that fall through to nothing rather than to
page 0. The comment at `dosopt.c:80-84` says why, and it is a rule a lobby should inherit
whether or not it lives inside this file.

The cheat page (`dopt_draw_cheats`, `dosopt.c:1181`) is the newest page and it is the
cheapest possible template: it is the Advanced page's box, caption, checkbox and button
called with a different list, plus a bottom row of two buttons sized to their own labels
(`dopt_item_rect:445-468`). A new page costs one enum, one label table, one arm in each of
the four switches, one draw function and one arm in `dopt_activate`.

### 1.4 The one list widget in `menu/` (`menu/dosops.c`)

| What | Call | Where |
| --- | --- | --- |
| Init from a caller-owned array | `do_state_init(st,list,count)` | `menu/dosops.c:11` |
| Move the selection, clamped, keeping it visible | `do_move(st,delta)` | `menu/dosops.c:35` |
| Scroll without moving the selection (the wheel) | `do_scroll(st,delta)` | `menu/dosops.c:49` |
| Hit test: a row index, or `DO_HIT_PLAY` / `DO_HIT_CANCEL` / `DO_HIT_NONE` | `do_hit_test(st,mx,my)` | `menu/dosops.c:73` |
| Draw the whole screen over whatever is in the surface | `do_draw(s,p,st)` | `menu/dosops.c:108` |
| A green button with a centred label | `do_draw_button(...)` (static) | `menu/dosops.c:96` |

Geometry: a 288x188 dialog at (16,6), a 272x130 list well at (24,26) giving 16 rows of 8
pixels, and two 60 wide buttons on row 168 (`menu/dosops.h:33-52`).

Three limits worth stating before anyone reaches for this as the lobby base:

* **There is no gadget focus.** `DO_State` (`menu/dosops.h:59-66`) has a selected list row
  and a pressed button, and nothing else. Play and Cancel cannot be reached from the
  keyboard at all; Enter plays the selected row and Escape cancels. A lobby with a dozen
  controls needs `dosopt`'s `selected` plus `dopt_next_item`, not this.
* **There is no disabled state.** `do_draw_button` always passes `disabled = 0`.
* **The list has no scrollbar.** Neither does the jukebox. Both scroll silently. If a
  visible scrollbar is wanted, it is new, and it is small (a track and a proportional
  thumb, both `db_fill_rect`).

### 1.5 The 640x480 HUD (`game/hud640.c`, `game/hud640.h`)

**Nothing here is reusable for a lobby, and that is by design.** It rasterises into a
32 bit RGBA buffer from pre-authored art rather than into the 8 bit palettised surface, for
the reason its own header gives at `game/hud640.h:8-13`: the DOS sidebar is only correct in
8 bit, this is new art at 640x480 with a real alpha channel. Its only text is
`hud640_draw_tab` (`hud640.c:399`), which blits a pre-lettered plate chosen by name or a
digit strip; there is no font, no measurement and no arbitrary string. Its blitters
(`h6_blit`, `:83`) take `H6_Asset` records, not `DB_Shape`.

The one transferable idea is `hud640_row_y` / `hud640_rows_for` (`hud640.c:205`, `:227`):
a layout whose row count is computed from available space rather than fixed. A lobby that
wants to show 6 or 10 map rows depending on how tall the preview panel ends up could copy
that shape.

### 1.6 The debug panel (`game/fx_panel.h`)

Also **not reusable**, also deliberately. Its own 5x7 font, its own textured quads, drawn
directly in GL after the post chain, Tier 2 only. Its header says at `fx_panel.h:20-25`
that it does not borrow the DOS font printer on purpose, because a debug instrument that
fails when a pack fails to load cannot be used to diagnose the pack. Its rows are generated
by walking a parameter table, which is a good idea for a settings page and a bad fit for a
lobby whose controls are heterogeneous.

### 1.7 Text entry, the only one that exists (`app/campaign.c:1672`)

`camp_input_name` is the score screen's hall-of-fame entry, transcribed from
`score.cpp:1607-1689`. It is a fixed 6 pixel pitch, a blinking underscore cursor, a zooming
letter animation per keystroke and a blocking loop with a hard-coded harness key script.
It is not extractable as a general edit box without rewriting it. There is no `EditClass`
anywhere in the tree (confirmed by substring count: the string appears only in comments).

### 1.8 The bridge that lets campaign screens use the DOS primitives

`camp_surface` (`app/campaign.c:712`) wraps an existing 320x200 plate in a `DB_Surface`
**without clearing it**, and `camp_print16` (`:729`) prints into it with a verbatim 16 slot
palette. The comment at `:709-711` records what happens if you use `db_surface_init` here
instead: it clears the storage and three prints in a row leave a black screen with only the
last line standing. Any new screen that draws over existing pixels wants this pattern.

---

## 2. What a lobby needs that does not exist

The lobby's job is to fill the skirmish block of `GameOpts` (`game/cnc_game.h:66-86`), which
`arm_skirmish` (`game/cnc_eyes.cpp:11838`) turns into `CNCMultiplayerOptionsStruct` and six
`CNCPlayerInfoStruct` rows. Every field already has a command line switch behind it
(`--skirmish --side --ai --credits --notiberium --crates --nosuper --starts --unitcount
--build`, all parsed in `cnc_eyes.cpp`), so each control below can be verified against a run
that sets the same value from the command line. That is a strong testing position and it
should be used.

### 2.1 A value printed beside a slider. NEW, and tiny.

Every slider in the tree is unlabelled by value: Game Speed, Scroll Rate and the three
volumes all show position only. Starting credits and AI count are numbers a player must
read. Nothing prints a slider's current value anywhere.

**Extend, not new.** One helper of roughly ten lines beside `dopt_draw_slider_label`:
`sprintf` the value, `db_string_width` it, print it right aligned at
`x + w - width, y - 2` in the same font palette. **Half an hour.**

### 2.2 A numeric stepper (a `< 3 >` row). NEW, or avoid it.

There is no stepper anywhere (confirmed: "stepper" and "spinner" appear zero times in any
`.c`/`.h`/`.cpp` in the tree). AI count is 1..5 and credits is a coarse range; both fit a
slider with a small maximum, and `dopt_ctrl_max` (`dosopt.c:552`) already returns 6 for
Game Speed, so small-range sliders are a solved case.

**Recommendation: do not build a stepper.** Use a slider plus 2.1. If a stepper is wanted
anyway it is two small buttons flanking a printed value, all three of which exist; call it
**half a day** including the hit test and the keyboard walk.

### 2.3 A drop list. NEW, and not needed.

Nothing in the tree resembles one (confirmed: "droplist", "drop list", "combo" appear zero
times). A drop list is genuinely expensive here because it needs an overlay layer, and the
whole drawing model is one immediate pass into one surface with no z ordering: a popup
would have to be drawn last, and every hit test would have to consult it first.

**Recommendation: do not build one.** Everything a lobby would put in a drop list is a
2-to-6 way choice, which is a latched button row (2.4) or a slider. If one is ever
unavoidable, budget **two days**, most of it in the hit-test ordering, not the drawing.

### 2.4 A latched button row of more than two. EXTEND. Small.

The Classic/Enhanced pair (`dosopt.c:1126-1139`) is a two-way radio. Generalising it to N
is turning `const int sel = ...enhanced ? A : B` into an index and drawing N buttons on one
row instead of a column. **An hour.** This covers Side (GDI / Nod), and it would cover
difficulty or AI personality if either is ever offered.

### 2.5 Check boxes. REUSE VERBATIM.

`dopt_draw_check` (`dosopt.c:1102`) plus the whole-row rectangle rule
(`dopt_item_rect:480-486`) plus the label print at `dopt_draw_advanced:1172-1174`. The
cheat page is five of them and the whole loop that draws all five, labels included, is ten
lines (`dosopt.c:1190-1199`). Tiberium, Crates and Superweapons are three of these.
**An hour.**

Bases is the fourth and it must be drawn **disabled**, not omitted: the interlock and its
reason are already recorded at `game/cnc_game.h:73-76` (with bases off nobody has a
building or an MCV, so the early-win rule declares every house dead about a second into the
match). A disabled gadget that is drawn is the 1995 answer and the toolkit already does it.

### 2.6 A colour row. NEW, and it would currently be a lie.

`CNCPlayerInfoStruct::ColorIndex` is already set (`cnc_eyes.cpp:11876`, `p.ColorIndex = i`),
and **the renderer ignores it completely**. Both places that pick a colour key on the SIDE,
not the house:

* `house_colour` (`cnc_eyes.cpp:3445-3455`): GDI gold, Nod red, neutral grey, and a
  fallback blue.
* the radar blips (`cnc_eyes.cpp:9065-9077`): `DB_YELLOW` for side 0, `DB_RED` for side 1.

Both carry comments explaining that keying on the house NAME turned everything the wrong
colour in a skirmish, because every house is `Multi1`..`Multi6`. Worse, `arm_skirmish`
forces the sides opposite (`cnc_eyes.cpp:11872-11875`) because the cartridge carries exactly
two house texture sets, so two houses on the same side would field two identical armies.

**A colour picker in the lobby is therefore not a widget problem, it is a renderer
problem.** Offering one before the renderer can honour it puts a control on screen that
does nothing, which is the failure mode this project has already written up twice. Size, if
it is wanted: **small in the lobby, large in the renderer** (per-house tint in
`house_colour`, per-house blip colour in the radar, and an answer for the two-texture-set
limit). Recommend deferring and saying so on the screen by not drawing the control at all.

### 2.7 A map preview panel. The DATA EXISTS. The READER DOES NOT.

`tools/bake_map_previews.py` produces `game/mappreview.pack` and
`game/mappreview.manifest.json`: nine maps, 120x120 frames, magic `MAPPREV1`, fixed-size
records so the n-th is at a computable offset. Each record carries the scenario code, the
`[Basic] Name=`, the theater, the playable cell rect, where the map sits inside the frame,
the count of legal start positions, the tiberium cell count, up to 26 start positions with
both their map cell and their pixel position in the frame, and **two 120x120 planes**: one
terrain-only and one with start diamonds already drawn.

Critically, it is **8 bit indices in the menu's own palette** (the 256 entries embedded in
TITLE.CPS, which is what `dosmenu.pack` carries), and the margin around a non-square map is
index 0, which the shape blitter does not paint. So a preview blits into a menu plate with
no palette change, no remap and no conversion.

There is **no C reader**: `MAPPREV1` appears in exactly two files in the tree, the baker and
its own manifest (confirmed by substring count over every `.c`, `.h`, `.cpp`, `.py`, `.sh`,
`.md` and `.json`).

**New work, and it is the second of the two real jobs.** It is a pack loader in the exact
shape of `db_pack_load` (`dosbar.c:47`): open, read, check magic and version, walk fixed
size records, keep pointers into the blob rather than copying. Roughly 120 lines including
the record accessor, plus a blit that is `db_draw_shape` with a `DB_Shape` synthesised over
the plane pointer, or a ten line loop of its own. **A day, including the border rectangle
around the map's own sub-rect and clicking a start diamond.**

The baker has already drawn the layout it expects, at 1:1 on a 320x200 screen
(`footprint_panel`, `tools/bake_map_previews.py:851-871`): a dialog inset at (8,8) to
(311,191), the preview at x = 188, y = 12, and ten list rows 150 wide at x = 16 stepping 12
rows from y = 16. That is wider than the Special Ops dialog (288x188 at (16,6)) and it
leaves about 80 rows under the preview for the option controls and the buttons.

### 2.8 An edit box. NEW, and out of scope.

A player name matters when there is a network. There is none. `camp_input_name` is not
reusable (1.7). **Do not build it.** `arm_skirmish` writes `"PLAYER"` and `"COMPUTER"` at
`cnc_eyes.cpp:11871` and nothing reads either.

### 2.9 A scrollbar. NEW, small, optional.

Neither existing list draws one. If the lobby's list is ten rows over nine maps it will
never scroll and the question is moot. If it grows, a track plus a proportional thumb is two
`db_fill_rect` calls and a drag that reuses the slider's own grab arithmetic. **Two hours.**

### 2.10 What the lobby returns. NEW, and it is a design decision, not a widget.

Both existing modal screens return a single `int`. A lobby returns a whole settings block.
See 3.4.

---

## 3. How a modal screen is run, and how it returns a result

There are exactly two patterns in the tree. A lobby should use the first.

### 3.1 Pattern A: a screen on the menu shell (`dms_special`)

The shell owns the window, the GL context, the texture, the letterbox, the pointer, the
score and the pack. A screen borrows all of it. The seam is one pointer on `DMS`:

```c
/* menu/dosmenu_shell.h:84-87 */
const struct DO_State* ops;
```

`dms_redraw` (`menu/dosmenu_shell.c:235-250`) branches on it:

```c
memset(s->screen, DB_TBLACK, sizeof s->screen);
if (s->ops) { dm_draw_plate(&s->surf, s->pack); do_draw(&s->surf, s->pack, s->ops); }
else        { dm_draw_menu(&s->surf, s->pack, &s->st); }
dm_draw_cursor(&s->surf, s->pack, s->mx, s->my);
db_surface_to_rgba(&s->surf, s->pack->pal8, s->rgba, 0);
dms_upload(s, s->rgba);
```

and `dms_special` (`menu/dosmenu_shell.c:511-623`) is the whole modal, in this exact order:

1. build the state from a caller-owned array, `do_state_init(&ops, list, count)`;
2. `s->ops = &ops;` then `SDL_ShowCursor(SDL_DISABLE)` (the DOS pointer is drawn into the
   surface, not by the OS);
3. `dms_layout(s); dms_redraw(s);`
4. `for (;;)` with `SDL_PollEvent`, `fs_handle_event` first so fullscreen still works,
   then `SDL_QUIT` -> return `DMS_QUIT`, keys, motion, wheel, button down and button up;
5. mouse coordinates go through `dms_to_menu(s, e.button.x, e.button.y, &cx, &cy)`
   (`:104`), which converts window points to 320x200 menu pixels and deliberately answers
   far off the plate for a click on the letterbox bars;
6. the action fires on **release over the same control the press went down on**, which is
   `gadget.cpp`'s `LEFTRELEASE` and what the main menu does;
7. `if (done) break; if (dirty) dms_redraw(s); dms_pump_audio(s); dms_present(s);
   SDL_Delay(16);`
8. on the way out: `s->ops = NULL; dms_redraw(s);` and return.

Return convention (`menu/dosmenu_shell.h:39-47`):

* `DMS_QUIT` = `-1`, the window was closed, close the program;
* `DMS_CANCEL` = `-2`, the user backed out, show the menu again;
* `>= 0`, the index of what was chosen.

**A hazard to know about before writing a third screen: `DMS_NONE` is also `-2`**
(`dosmenu_shell.h:47`). `dms_run` gets away with it because it only ever tests `chosen >= 0`.
The header already records that `DMS_CANCEL` and `DMS_QUIT` were the same number until
v0.6.0 and that Cancel therefore closed the game. Do not add a third meaning to `-2`.

The caller side is `app/cnc3d.cpp:1024-1070`. Today the `DM_MULTI` arm scans for maps, calls
`camp_side_select(&camp)` (`:1039`) for the side, then `dms_special` (`:1043`) for the map,
then copies the scenario and pack into static buffers because `opt.scen` outlives the
screen, and sets `opt.skirmish`, `opt.side`, `opt.build` and `g_camp.active = 0`. A lobby
replaces the two calls at `:1039` and `:1043` with one.

### 3.2 Pattern B: a screen run by the renderer (`game_visuals_open`)

`game/cnc_eyes.cpp:15284-15430`. Same idea, different owner: it loads `dossidebar.pack` if
it is not loaded, calls `dopt_open_visuals`, pushes the host's truth in with
`dopt_set_visuals`, binds the apply callback, sets `g_optOpen`, locks the DOS cursor, and
then runs a loop that converts SDL events into `dopt_key` / `dopt_press` / `dopt_motion` /
`dopt_release` and acts on the returned `DOPT_ACT_*`. It clears to black rather than drawing
a plate, because in game there is a battlefield behind it.

Its header comment at `:15311-15326` is worth reading before writing any new screen: it
records that this button was dead from the day it was added because a single global flag
was set in only one code path, and that no gate could see it because the gates drove the
other path. Two lessons for a lobby: **do not gate drawing on a flag that only one entry
point sets**, and **make the gate drive the path a player drives**.

### 3.3 Which one the lobby should use

**Pattern A.** The lobby is a menu screen: it stands on the title plate, it runs before any
mission exists, and the shell already owns everything it needs. Pattern B exists because the
pause dialog has to draw over a live frame.

But the lobby's *module* should be shaped like `dosopt` and not like `dosops`, because it
needs gadget focus across mixed control kinds, per-item disabled rules and a keyboard walk,
and `dosops` has none of those (1.4).

### 3.4 The signature, and the reason it is not an `int`

Every other screen answers one question. A lobby answers ten. The precedent for that is
`camp_mapsel` (`app/campaign.h:157`), which returns 0 or -1 and fills two out parameters.
So:

```c
int dms_lobby(DMS* s, const SK_Map* maps, int count, SK_Lobby* out);
/* returns DMS_QUIT, DMS_CANCEL, or 0 with *out filled */
```

`SK_Lobby` holds exactly the fields of the skirmish block of `GameOpts`
(`game/cnc_game.h:66-86`), and the caller copies them across. It must not hold pointers into
the map list, for the reason `app/cnc3d.cpp:1050-1058` already records: `opt.scen` outlives
the screen because the match can be restarted from the pause dialog, and the next visit to
the list frees what a pointer would point into. Copy the strings.

### 3.5 How a modal screen is driven headlessly

Two mechanisms already exist and both should be used.

* **At the menu level**, `dms_item_window_rect` (`menu/dosmenu_shell.c:92`) returns an
  item's rectangle in window points, and `harness_click` (`app/cnc3d.cpp:455`) pushes a
  synthetic `SDL_MOUSEBUTTONDOWN` / `UP` pair at a point. That is how `--harness` clicks
  Test Map (`app/cnc3d.cpp:934-937`) and how `--flowtest` clicks Start
  (`:949-953`). A lobby wants the equivalent of `dms_item_window_rect` for its own items.
* **At the dialog level**, the script verbs `optclick LABEL` (`cnc_eyes.cpp:14135-14159`)
  and `optslider LABEL FRACTION` (`:14160`) look an item up by its printed label through
  `dopt_item_label`, take its rectangle from `dopt_item_rect`, and drive
  `dopt_press` / `dopt_release`. That is why every button carries a name even when it prints
  a glyph instead (`dosopt.c:72-74`). A lobby that exposes `label`, `rect`, `press` and
  `release` in the same shape gets scriptability for free.

---

## 4. The 1995 look, collected

Everything below is already cited in the source against the GPL Tiberian Dawn tree. A new
screen should use these numbers, not new ones.

### 4.1 Which box for which job

| Job | Style | Call |
| --- | --- | --- |
| The screen's outer dialog | `BOXSTYLE_GREEN_BORDER`, an inset rectangle on opaque black, no bevel | `dm_green_dialog` (`menu/dosmenu.c:213`) |
| A button, raised | `BOXSTYLE_GREEN_RAISED`: fill 141, shadow 140 bottom/right, highlight 159 top/left, corners 141 | `dm_green_button(..., pressed=0, disabled=0)` |
| A button, held | `BOXSTYLE_GREEN_DOWN`: the shadow and highlight swap | `dm_green_button(..., pressed=1, ...)` |
| A button, disabled | `BOXSTYLE_GREEN_DIS_RAISED`: fill 13, shadow 12, highlight 14, corners 13. **Same bevel geometry, only the ink changes.** It is a grey slab, not a dim green one | `dm_green_button(..., disabled=1)` |
| A slider body | `BOXSTYLE_GREEN_DOWN` drawn inline | `dopt_draw_slider` (`dosopt.c:1036-1041`) |
| A list well | black fill with a `GREEN_BOX` (159) frame | `dopt_draw_sound` (`dosopt.c:1282-1293`) |
| A grey 1995 box (sidebar look, not dialog) | `db_draw_box` with `DB_BOX_RAISED` etc. | `dosbar.c:259` |

The full engine walk-through for the disabled colourway, both the box and the text, is at
`menu/dosmenu.h:62-102`. It is worth reading once because the conclusion is
counter-intuitive: a disabled button leaves the green ramp entirely.

### 4.2 The palette indices

These are **positions in whichever palette is loaded**, not RGB. Both `dosmenu.pack` (the
TITLE.CPS palette) and `dossidebar.pack` (the mission palette) carry the same values at
these indices, verified by reading the packs:

| Name | Index | Colour | Source |
| --- | --- | --- | --- |
| `CC_GREEN_SHADOW` | 140 | (40,68,36) | `defines.h:2881` |
| `CC_GREEN_BKGD`, `CC_GREEN_CORNERS` | 141 | (48,84,44) | `defines.h:2882-2883` |
| `CC_LIGHT_GREEN`, `CC_GREEN_BOX` | 159 | (60,152,56) | `defines.h:2884-2885` |
| `CC_BRIGHT_GREEN` (the underline, the tick, the slider fill) | 167 | (140,200,8) | `defines.h:2886` |
| medium button text | 41 | (84,176,36) | `dialog.cpp:366` `_textpalmedium[CC_GREEN]` |
| bright button text (pressed or focused) | 4 | (84,252,84) | `dialog.cpp:368` `_textpalbright[CC_GREEN]` |
| disabled button text | 3 | (0,168,0) | `CC_GREEN` itself, ungraded |
| disabled fill / corners | 13 | (84,84,84) | `DKGREY` |
| disabled shadow | 12 | (0,0,0) | opaque `BLACK` |
| disabled highlight | 14 | (168,168,168) | `LTGREY` |
| transparent | 0 | not painted | `DB_TBLACK` |

Declared at `menu/dosmenu.h:49-60` and `:98-102`, and again at `game/dosopt.h:335-359`.
Reuse one of those two sets rather than writing a third.

### 4.3 The font

**`GRAD6FNT`, always**, for every label, caption, list row and slider caption on a green
dialog. It is in `dosmenu.pack` and in `dossidebar.pack`. Print it with
`db_font_palette_grad` and `DB_FONT6_XSPACING` (`-2`).

The other four faces in `dosmenu.pack` have exactly one job each and none of them is a
dialog: `6POINT` prints the version line over the title art with `FULLSHADOW`
(`menu/dosmenu.c:277`), `SCOREFNT` prints the copyright line and every campaign screen's
teletyped text (`menu/dosmenu.c:301`, and note the trap recorded at `:312-315`: it is a
shaded face whose glyphs never use class 1, so mapping only class 1 prints nothing),
`8POINT` and `3POINT` are unused by any current screen.

The filigree is `OPTIONS.SHP`, 32 frames. The pair a dialog uses depends on which caption
the engine drew: frames 0 and 1 for the main menu's `TXT_NONE` case
(`menu/dosmenu.h:104-107`), frames 2 and 3 for the Options and Game Controls captions
(`game/dosopt.h:361-365`). Both are drawn `SHAPE_CENTER` at `(x + 12, y + 11)` and
`(x + w - 14, y + 11)`.

### 4.4 Button sizing, so labels stay inside their boxes

`textbtn.cpp:81` sizes a button to `String_Pixel_Width + 8`, and `goptions.cpp:156` floors
it at `MAX(maxwidth, 90 * resfactor)`. `dopt_layout` (`dosopt.c:160-189`) does exactly that:
it measures every label on the page, takes the widest, adds 8 and floors at 90. Because
`db_print` does not truncate (1.1), **a lobby that hard-codes a button width will eventually
print a label across the map preview**. Size from the label.

The label is centred with `textbtn.cpp:359`'s own expression, verbatim in three places
already: `x + (w >> 1) - 1 - (db_string_width(f, label, DB_FONT6_XSPACING) >> 1)`, printed
at `y + 1`.

### 4.5 The pointer

`MOUSE.SHP` frame 0, hotspot at the top left pixel, drawn into the surface at the pointer
position unshifted (`menu/dosmenu.c:448-455`). The OS cursor is hidden for the duration
(`SDL_ShowCursor(SDL_DISABLE)` at `dosmenu_shell.c:520`). Any new modal must do both or it
gets two pointers, or none.

---

## 5. A build order

The rule for the order below is that **something is on screen after step 1** and every step
after that is separately provable. It also keeps the module SDL-free and GL-free from the
first line, because that is what makes the headless gate possible.

### Step 0. Decide the two boundaries, before writing anything

* **The module goes in `menu/`, beside `dosops.c`**, and is plain C89 with no SDL and no GL,
  exactly like `dosopt.c` (`game/build.sh:42-44` explains the discipline and
  `game/gate_optlayout.c:21-25` explains the payoff). The shell half goes in
  `dosmenu_shell.c` where the modal loops already live.
* **A new source file must be added to four places or the build breaks on the other
  machine**: `app/build.sh:62`, `tools/win/sources.sh:25`, and `menu/build.sh:24` if the
  standalone preview should carry it. `tools/win/check-sources.sh` compares the Mac and
  Windows lists and fails hard on a difference. A standalone gate binary additionally needs
  a line in that script's exclusion list (`check-sources.sh:10-20`), which already carries
  `gate_optlayout.c` as the precedent.

### Step 1. The spine: a box, a caption, OK and Cancel, and the modal loop

Write `menu/doslobby.h` and `menu/doslobby.c` with the `dosopt` five: `count`, `label`,
`disabled`, `rect`, `hit_test`, plus `next_item`, `press`, `release`, `key`, `draw`. Two
items only: Start and Cancel. Draw with `dm_green_dialog` and `dm_green_button`.

Add `dms_lobby` to `dosmenu_shell.c` as a copy of `dms_special` (`:511`), and a second
pointer beside `s->ops` with a second branch in `dms_redraw` (`:238`).

Point the `DM_MULTI` arm (`app/cnc3d.cpp:1024`) at it, keeping the existing scan and the
existing `camp_side_select` / `dms_special` calls behind it for now so nothing regresses.

**Provable now:** Multiplayer Game opens it, Cancel returns to the menu without closing the
program, Escape does the same, closing the window closes the program. That is the whole
`DMS_CANCEL` / `DMS_QUIT` contract, which has been got wrong once already
(`dosmenu_shell.h:41-45`).

### Step 2. The headless layout gate, before the screen gets complicated

Copy `game/gate_optlayout.c`. It links two objects, opens the same pack the game opens and
answers in milliseconds with no display. Its five legs are the right five for a lobby too:
every rectangle inside its dialog, every printed label inside its own button and inside the
dialog, no two items overlapping, two rows at least 2 apart, two columns at least 2 apart.
Take its two anti-vacuity rules with it (`gate_optlayout.c:49-64`): **a missing pack or font
is exit 2, not a default**, and **the number of rectangles checked is itself asserted**.

Grow this gate with every step below. It costs almost nothing per control and it is the only
thing that can see the failure mode this screen is most likely to hit.

### Step 3. The map list

Move the rows the caller already builds (`app/cnc3d.cpp:1032-1044`, `g_skirmishRows`) into
the lobby. The list body is `do_draw`'s (`menu/dosops.c:135-172`) or the jukebox's
(`dopt_draw_track`, `dosopt.c:1230`); take the jukebox's, because it has tab stops and a
playing marker and the lobby will want columns.

**Provable now:** the same map starts as before. The change is a no-op on the outcome, which
is the cheapest possible assertion and the existing skirmish gates already make it.

### Step 4. The side pair

Lift the latched pair from `dopt_draw_visuals` (`dosopt.c:1126-1139`) and the exclusivity
rule from `dopt_activate` (`:691-706`). Delete the `camp_side_select` call from the
`DM_MULTI` arm (`app/cnc3d.cpp:1039`); it stays where it is for the campaign.

**Provable now:** a gate that picks Nod in the lobby and asserts the `HOUSE` line the brain
reports, against a run that sets `--side 1` from the command line. Two paths, one answer.

### Step 5. The check boxes

Tiberium, Crates, Superweapons live, Bases drawn disabled with the interlock reason from
`game/cnc_game.h:73-76`. `dopt_draw_check` verbatim, whole-row rectangles, labels at a fixed
column.

**Provable now:** a `--script` run that reads back the flags `arm_skirmish` was handed
(`cnc_eyes.cpp:11849-11859` already prints a summary line at `:11887`), compared against the
same run driven by `--notiberium` / `--crates` / `--nosuper`.

### Step 6. The two numeric controls

AI count and starting credits as sliders, plus the printed-value helper from 2.1. This is
the first genuinely new drawing code in the whole plan and it is about ten lines.

Take `dopt_press`'s grab arithmetic with the slider (`dosopt.c:644-661`); it is the
difference between a slider that jumps a pixel when you touch it and one that does not.

**Provable now:** the gate asserts the printed value's extent is inside the dialog at both
ends of travel, which is exactly the class of bug leg (b) exists for.

### Step 7. The map preview panel

Write the `MAPPREV1` reader (2.7) in the shape of `db_pack_load` (`dosbar.c:47`), blit the
`marked` plane at the panel position, and draw a one pixel border around the map's own
sub-rect from the record's `img_x/y/w/h`. The baker's own 1:1 layout
(`tools/bake_map_previews.py:851-871`) is the geometry to start from.

Then, optionally, make the start diamonds clickable using the recorded pixel positions, and
write the chosen one into `start_wp[0]`.

**Provable now:** a PNG of the lobby with a map selected, plus an assertion that the preview
for the selected scenario is the record whose `scenario` field matches. And a negative
assertion worth having: a pack that is missing must degrade to an empty panel with a printed
reason, not to a silent grey rectangle.

### Step 8. Retire the temporary route and write it down

Remove the `camp_side_select` plus `dms_special` pair from the `DM_MULTI` arm entirely,
update `docs/design-skirmish.md` section 6.5 (which currently says every option widget is
new work), and add the gate to the suite.

### Deliberately not in this plan

* A colour row (2.6): blocked on the renderer, not on widgets.
* A drop list (2.3): nothing needs one.
* An edit box (2.8): nothing reads a player name.
* A numeric stepper (2.2): a small-range slider is the same information in existing code.
* Bases ON/OFF as a live control: the engine's early-win rule has to be interlocked first,
  and that is a brain question, not a lobby question.
