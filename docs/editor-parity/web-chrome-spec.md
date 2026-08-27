# CNC3D WEB MISSION EDITOR -- EXACT CHROME SPECIFICATION

Source files (repo-relative):
- `tools/heightmap-viewer/public/index.html` (lines 6-590 = the entire stylesheet; lines 592-879 = markup). There is **no separate .css file**.
- `tools/heightmap-viewer/public/app.js` (viewer, top-bar chip, mission list, filters, legend, view-bar toggles)
- `tools/heightmap-viewer/public/editor.js` (sidebar panes, palettes, radar, lint, status card)

---

## 1. COLOUR TOKENS (`:root`, index.html:7-16) -- verbatim

| Token | Hex | Role |
|---|---|---|
| `--bg` | `#0b0f14` | page ground |
| `--panel` | `#151b24` | box fill |
| `--panel2` | `#1c2430` | control fill |
| `--panel3` | `#242e3c` | active control fill |
| `--line` | `#2a3543` | default border |
| `--line2` | `#374557` | hover/strong border |
| `--ink` | `#d5dee9` | primary text |
| `--dim` | `#96a1ad` | secondary text |
| `--dimmer` | `#7f8d9e` | tertiary text |
| `--faint` | `#5f6c7b` | labels/captions |
| `--gold` | `#e0b070` | brand / GDI |
| `--cyan` | `#8fe4ff` | view-state accent |
| `--green` | `#7ee0a0` | terrain accent |
| `--danger` | `#ff6a5a` | error / Nod |
| `--ok` | `#5ad46a` | pass |
| `--mode` | `#e0b070` initial | **recoloured at runtime per edit mode** |

**`--mode` is the single global accent.** `ui()` (editor.js:1463-1467) writes it from the active mode tab's `data-c`:
- Objects → `#e0b070`
- Terrain → `#7ee0a0`
- Elevation → `#8fe4ff`

Consumers of `--mode`: `#view` top border, `.vp-badge` border + text, `#status` border + `.l1` icon, `.tool.on`, `.mtab.on`, `.cat.on`, `.tile.on`, `.dw.on`, `.tr.on`, `.tbtn.on`, `.bsz.on`, `.blk.on`.

Hard-coded hexes not in tokens (all real, quoted):
`#000` (top/rail/stage/side hard edges), `#05080c` (rail/stage/side inner edges), `#0b1016` (inset trough: `.seg4`, `.cats`, `.iogroup`, `.srch`), `#0c1119` (list/grid wells), `#0e141c` (tab strip + lint/joblog headers), `#101822` (popup), `#070a0e` (radar well), `#111823` (`.mhd`), `#131a24` (row separator), `#141d29` (row hover), `#121a24` (tab hover), `#17262f` + `#3c5f6e` (cyan "on" fill/border), `#2c384a` (scrollbar thumb), `#1a222c` (lint row separator), `#4a5c72` (mapchip hover border), `#6f9fb2` (io key on), `#2a1c1c`/`#6b3c34`/`#ff9a8c` (warn toggle on), `#ffb3a8` (error text), `#bfe8fb` + `rgba(20,44,58,.9)` (3D badge), `#dfe8f2` + `rgba(0,0,0,.62)` (footprint badge), `#0d1117` (badge/PLAY text), `rgba(224,176,112,.88)` / `rgba(208,138,106,.9)` / `rgba(154,167,180,.88)` (code chip GDI/Nod/mesh), `#3f5064`→`#22303f` (generic bars), `#8a6a3f` (facts bars bottom), `#3d4c60`→`#26313f` + `#4b5c72` (tier bar), `#6b5326`/`#221c11`/`#1a160e`/`#f0c070`/`#4a3a1a`/`#8a6a2c`/`#f5d9a0`/`#5c4820` (convert gate), `#c79a5c`/`#e8bd80`/`#c9944d`/`#1a1206`/`#f2c98d`/`#d6a058` (SAVE), `#8fe4ff`/`#4aa8cc`/`#b6efff` (PLAY), `#3a2b17`/`#e8c98f`/`#8a6a3c` (coin edge).

Data colours:
- Houses (editor.js:37-42): GoodGuy `#e0b070`, BadGuy `#ff6a5a`, Neutral `#9aa7b4`, Special `#b98fe4`.
- Theater dot in mission list (app.js:1536): DESERT `#c9a24a`, else `#5c9a5a`.
- Legend `TIER_COLOR` (app.js:20): `['#2f62ad','#22a7a0','#a8c342','#f0913a','#f7ece0']`.
- Legend `FAM_COLOR` (app.js:12-17): CLEAR `#b9a887`, ROAD `#8a6f4c`, PATCH `#93894f`, BRUSH `#5c8a4a`, BOULDER `#8d8d8d`, SLOPE `#e8622a`, RIVER `#2f7fd8`, WATER `#1b4a8f`, SHORE `#37b8c9`, FORD `#12a08a`, FALLS `#a05fd8`, BRIDGE `#e8c832`.

---

## 2. FONTS

```
--mono: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace
--ui:   system-ui, -apple-system, "Segoe UI", sans-serif
```
`body{font:12px/1.4 var(--mono)}` -- **mono is the default for the entire chrome.**
`var(--ui)` is used only for prose and proper names: `.mapchip .nm` (12px italic), `.mrow .mnm` / `.mlist .row .nm` (11px italic), `.brief p` (11px/1.5), `.rsec p` (11px/1.5), `.tile .nm` (9.5px/1.15), `.derived` (10px/1.45), `.gate p` (10px/1.4), `.lrow .d` (10px/1.35).

Full font-size inventory in use: 8px, 8.5px, 9px, 9.5px, 10px, 10.5px, 11px, 12px, 13px. Weights used: 400 (default), 500 (`#savebtn .k`), 600 (`.mark`, `.vp-badge .m`, `#status .l1 .nm`, `.mtab .t`, `.rsec p b`, `.derived b`), 700 (`.mtab.on .t`, `#savebtn`, `#playbtn`).

Two shared surface effects:
```
.bev  box-shadow: inset 0 1px 0 rgba(255,255,255,.075),
                  inset -1px 0 0 rgba(0,0,0,.42),
                  inset 0 -1px 0 rgba(0,0,0,.55),
                  inset 1px 0 0 rgba(255,255,255,.028)
.hatch background-image: repeating-linear-gradient(45deg,
                  rgba(255,255,255,.016) 0 2px, transparent 2px 6px)
```
`.bev` is applied to: `#top`, `.mapchip`, `.navbtn`, `#topbox`, `.lint`, every `.act`. `.hatch` is applied to `#side` only.

---

## 3. ROOT LAYOUT

```
html,body { width:100%; height:100%; margin:0; overflow:hidden }
#app  { display:grid; grid-template-rows: 48px minmax(0,1fr); height:100% }
#body { display:flex; min-height:0; min-width:0; overflow:hidden }
```

Three columns inside `#body`, left → right:

| Region | Width | Flex |
|---|---|---|
| `#rail` | **56px** | `flex:0 0 56px` |
| `#stage` | remainder | `flex:1; min-width:0` |
| `#side` | **`clamp(340px, 24vw, 460px)`** | `flex:0 0 auto; overflow:hidden` |

Note: `#side` is declared twice. Line 191 says `width:400px;flex:0 0 400px`; the later override at line 500 wins → **`clamp(340px,24vw,460px)`**. 400px only applies at viewport width 1666px.

`#stage` is a column: `#view` (`flex:1`) above `#viewbar` (`flex:0 0 auto; min-height:44px`).

---

## 4. TOP BAR -- `#top`, height **48px**

```
display:flex; align-items:center; gap:14px; padding:0 12px 0 14px;
background: linear-gradient(#1a222d, #121820);
border-bottom: 1px solid #000;
box-shadow: 0 1px 0 rgba(255,255,255,.05) inset;
class="bev"
```

Left → right, separated by the 14px flex gap:

1. **`.mark`** -- flex, gap 8px, colour `var(--gold)`, `letter-spacing:.16em`, `font-size:11px`, `font-weight:600`.
   - **`.coin`** -- 34×34px, `flex:0 0 34px`, `perspective:260px`, `transform-style:preserve-3d`, `animation: coinspin 7s linear infinite` (0°→360° rotateY), disabled under `prefers-reduced-motion`. Three absolutely-positioned children filling `inset:0`: front face `background:url(data/gdi.png) center/contain no-repeat`, `border-radius:50%`, `box-shadow: 0 0 0 1px rgba(224,176,112,.55) inset, 0 0 10px -2px rgba(224,176,112,.5)`; back face `.b` = same, `rotateY(180deg)`; edge `.e` = `rotateY(90deg)`, **width 5px**, `left:calc(50% - 2.5px)`, `background:linear-gradient(180deg,#3a2b17,#e8c98f 18%,#8a6a3c 50%,#e8c98f 82%,#3a2b17)`, `border-radius:1px`.
   - Text `CNC3D` then `<span class="sub">MISSION EDITOR</span>` -- sub is `color:var(--dimmer)`, `letter-spacing:.14em`, `font-weight:400`, inherits 11px.
2. **`.vr`** -- vertical rule, **1px × 26px**, `background:var(--line)`, `flex:0 0 1px`.
3. **`#mapchip` (`.mapchip .bev`)** -- clickable; opens the Missions tab (`editor.js:2300`). `height:30px`, `padding:0 8px 0 10px`, gap 10px, `background:var(--panel2)`, `border:1px solid var(--line2)`, `border-radius:4px`, `cursor:pointer`. Hover: `border-color:#4a5c72; background:var(--panel3)`. Contents left→right:
   - `#mcdot` (`.dotside`) -- 7×7px circle, `background:var(--gold)`, `box-shadow:0 0 0 2px rgba(224,176,112,.18)`. Runtime: set to `#ff6a5a` when `side === 'Nod'`, else `#e0b070` (app.js:1190).
   - `#mcid` (`.id`) -- `color:var(--gold)`, **13px**, `letter-spacing:.06em`. Text = scenario id, e.g. `SCG01EA`.
   - `#mcname` (`.nm`) -- `var(--ui)`, **12px**, italic, `color:var(--ink)`.
   - `#mcmeta` (`.mt`) -- `color:var(--dim)`, **11px**. Runtime string (app.js:1185): `` `${theater} · 64×64 · relief ${(relief/64).toFixed(1)}× | flat · ${objectCount} objects` `` plus optional `` ` · ${n} cells have no cartridge tile` ``.
   - caret icon `.ic.s16.car` -- 16×16, `stroke-width:2.25`, `color:var(--dim)`, symbol `#i-caret`.
4. **`.right`** -- `margin-left:auto`, flex, gap 12px, `color:var(--dim)`, `font-size:11px`.
   - `#savestate` (`.save-state`) -- flex, gap 6px, `color:var(--ok)`. Icon `.ic.s14` (14×14, stroke 2.55) `#i-check` + a text span. Text states (editor.js:536-545, 1671-1678): `"No edits yet"` / `"N edits unsaved"` / flash message. `.save-state.bad{color:var(--danger)}` and the icon swaps to `#i-warn`; flash reverts after **2600 ms**.
   - `.pill` -- `border:1px solid var(--line)`, `border-radius:3px`, `padding:3px 7px`, `color:var(--dim)`, **10px**, `letter-spacing:.08em`. Static text: `RIGHT-DRAG ORBIT · WHEEL ZOOM · WASD PAN`.

---

## 5. LEFT TOOL RAIL -- `#rail`, width **56px**

```
width:56px; flex:0 0 56px;
background: linear-gradient(90deg, #141a23, #101620);
border-right: 1px solid #05080c;
display:flex; flex-direction:column; align-items:center;
padding: 8px 0; gap: 2px;
```

`.tool` = **42×42px**, `border-radius:5px`, `display:grid;place-items:center`, `color:var(--dim)`, `background:transparent`, `border:1px solid transparent`, `position:relative`.
- hover: `background:var(--panel2); color:var(--ink); border-color:var(--line)`
- `.on`: `background:var(--panel3); color:var(--mode); border-color:var(--mode); box-shadow:0 0 0 1px rgba(0,0,0,.6), 0 0 16px -6px var(--mode)`, **plus** a `::before` tab: `position:absolute; left:-8px; top:9px; bottom:9px; width:3px; border-radius:0 2px 2px 0; background:var(--mode)`.
- `.kk` keycap: `position:absolute; right:4px; bottom:2px; font-size:9px; color:var(--faint)`; when `.on`, `color:var(--mode); opacity:.8`.
- icon default `.ic` = **20×20**, `stroke-width:1.8`, `fill:none`, round caps/joins.

Order top → bottom (index.html:747-755):

| # | `data-t` | Icon symbol | Key | Tooltip |
|---|---|---|---|---|
| 1 | `place` (**default `.on`**) | `#i-select` (filled arrow cursor) | **V** | "Select and place" |
| 2 | `move` | `#i-move` (4-way arrows) | **M** | "Move a placed object" |
| 3 | `pick` | `#i-drop` (eyedropper) | **I** | "Pick up what is under the cursor and arm it" |
| 4 | `erase` | `#i-erase` (eraser) | **X** | "Erase what is under the cursor" |
| -- | `.railspacer` | `flex:1` -- pushes the rest to the bottom | | |
| 5 | `camera` | `#i-camera` | **0** | "Frame the playable rectangle" |

`camera` is an *action*, not a tool: it calls `A.frameMap()` and does not take the `.on` state (editor.js:2306). The other four route through `setTool()` which clears `armed`/`brush` for anything but `place`.

`.railsep` (26×1px, `background:var(--line)`, `margin:7px 0`) is defined but **not used in the markup** -- dead CSS.

---

## 6. STAGE / VIEWPORT -- `#view`

```
flex:1; position:relative; overflow:hidden;
background:#0a0d11                     /* line 464 override wins over the
                                          radial-gradient at line 88 */
border-top: 2px solid var(--mode);     /* the mode colour rail */
border-left: 1px solid #05080c;
#view canvas { display:block; width:100%; height:100% }
body.editing #view canvas { cursor:crosshair }
```

WebGL canvas fills it; sized to `#view.clientWidth/clientHeight`, `setPixelRatio(min(devicePixelRatio,2))`, `ACESFilmicToneMapping`, exposure `1.05`.

### Absolute overlays inside `#view`

**`.vp-badge` (`#vpbadge`)** -- top-left, `left:14px; top:12px`; flex, gap 9px; `background:rgba(10,14,20,.82)`; `border:1px solid var(--mode)` with `border-left-width:3px`; `border-radius:4px`; `padding:6px 11px 6px 9px`; `backdrop-filter:blur(3px)`.
- icon `.ic.s16` coloured `var(--mode)`; symbol swaps by mode: `#i-house` (objects) / `#i-water` (terrain) / `#i-cliff` (elevation).
- `.m` -- mode name uppercased: `color:var(--mode)`, `letter-spacing:.18em`, **11px**, weight 600.
- `.o` (`#vpowner`) -- **11px** `var(--dim)`; in Objects mode only: `placing for <b>{house label}</b>` where `b` is `var(--ink)` weight 400.

**`.vp-note` (`#vpnote`)** -- top-right, `right:14px; top:13px`. Final cascade (line 508 overrides line 108): `color:var(--dimmer)`, **font-size:11px**, `letter-spacing:.02em`, `text-transform:none`, `text-align:right`, `line-height:1.5`, `max-width:52%`. `b` → `var(--ink)` weight 400; `.out` → `var(--danger)`; `.sub` → `var(--faint)`. Runtime content = the terrain probe (app.js:1284-1298): `cell X,Y · <b>tilename</b> <span class=sub>family</span> · corners a b c d · drop N · here: TYPE, TYPE`; with no ray hit it falls back to `` `${theater} · 64 height units per cell` ``.

**`#status`** -- bottom-centre card. `left:50%; bottom:18px; transform:translateX(-50%)`; **`width:min(560px, calc(100% - 32px))`**; `background:rgba(9,13,18,.94)`; `border:1px solid var(--mode)`; `border-radius:5px`; `padding:10px 12px`; `box-shadow:0 18px 42px -14px #000`; `backdrop-filter:blur(4px)`; `pointer-events:none`; `#status:empty{display:none}`.
Three fixed lines, built by `card()` (editor.js:1691-1699):
- `.l1` -- flex gap 8px, **13px**: `.ic.s16` in `var(--mode)`, then `.nm` (`var(--gold)`, weight 600), then `.sz` (`var(--dim)`).
- `.l2` -- `margin-top:5px`, `var(--dim)`, **12px**, gap 7px, `line-height:1.45`, `align-items:flex-start`; icon `.ic.s14` = `#i-check` coloured `var(--ok)`, or `#i-x` when `.l2.bad`, in which case the whole line is `var(--danger)`.
- `.l3` -- `margin-top:7px; padding-top:6px; border-top:1px solid var(--line)`; `var(--dimmer)`, **10.5px**, gap 14px, `white-space:nowrap`. Holds `<kbd>` chips: `font:inherit; color:var(--dim); background:var(--panel2); border:1px solid var(--line); border-radius:3px; padding:0 4px`. A right-aligned span (`margin-left:auto`) carries `cell X,Y` / `block X,Y`.
`#status .l1 .bib` (11px `var(--dimmer)`) is styled but never emitted -- dead CSS.

**`.vp-nav`** -- bottom-right, `right:14px; bottom:14px`, flex gap 6px. Two `.navbtn.bev`: **32×32px**, `border-radius:4px`, `background:rgba(16,22,30,.85)`, `border:1px solid var(--line2)`, `color:var(--dim)`, `display:grid;place-items:center`. `#prevmap` = `#i-caret` rotated **90deg**; `#nextmap` = `#i-caret` rotated **−90deg**.

**`#legend`** -- `left:12px; bottom:12px; z-index:12; display:none` (shown only for `fam` and `tier` surface modes). `background:rgba(12,17,23,.88)`; `border:1px solid var(--line)`; `border-radius:6px`; `padding:7px 9px`; **font-size:10px**, `line-height:1.7`; `color:var(--dim)`; `backdrop-filter:blur(6px)`. Rows `.li` flex gap 6px nowrap; `.sw` swatch **9×9px**, `border-radius:2px`, `flex:0 0 9px`.

**`#err`** -- `position:absolute; inset:0; display:none` (`place-items:center` when shown); `text-align:center`; `padding:40px`; `color:#ffb3a8`; `background:rgba(10,13,17,.92)`; **13px**; `z-index:40`.

**Dead mockup overlays** (CSS present, elements absent from the DOM and unreferenced by JS): `#iso`, `#horizon`, `#terrainblob`, `#riverblob`, `#ghost-lo`, `#ghost-hi`, `#ghostbody`, `#leader`, `.gcell`, `.gc-ok`, `.gc-bad`.

---

## 7. VIEW BAR (bottom of the stage) -- `#viewbar`

```
min-height:44px; flex:0 0 auto;
display:flex; align-items:center; gap:8px; flex-wrap:wrap; row-gap:4px;
padding:5px 10px;
background: linear-gradient(#161d26, #10161e);
border-top: 1px solid #05080c;
box-shadow: inset 0 1px 0 rgba(255,255,255,.05);
```
It **wraps** rather than clips (explicit comment at index.html:147-150). Height is `44px` minimum, growing by one row-height + 4px per wrap.

`.vlab` = section caption: `color:var(--dimmer)`, **9.5px**, `letter-spacing:.16em`, `text-transform:uppercase`, `flex:0 0 auto`.

Left → right:

1. `.vlab` **"Surface"**
2. **`.seg4` `#modesSurface`** -- segmented control: `background:#0b1016`, `border:1px solid var(--line)`, `border-radius:4px`, `padding:2px`, `gap:2px`. Four `<button>`: `padding:4px 8px`, `border-radius:3px`, `font:11px var(--mono)`, `color:var(--dim)`, gap 5px, `white-space:nowrap`, each with a `.ic.s14` icon:
   - `tex` "Texture" `#i-image` (**default `.on`**)
   - `white` "White" `#i-flat`
   - `fam` "Tile type" `#i-tiles`
   - `tier` "Tiers" `#i-stairs`
   `.on` → `background:var(--panel3); color:var(--cyan); box-shadow:inset 0 1px 0 rgba(255,255,255,.08), 0 1px 2px #0008`.
3. `.vr` -- 1×26px `var(--line)`.
4. `.vlab` **"Show"**
5. **`.haspop`** wrapping `#tAssets` (`.vtog.on`): `height:28px`, `padding:0 8px`, `border-radius:4px`, `background:var(--panel)`, `border:1px solid var(--line)`, `color:var(--dim)`, **11px**, gap 5px. Contents: `.ic.s16` `#i-eye`, label "3D assets", `.cnt` `#assetCount` (`var(--dimmer)`, **10.5px**, `font-variant-numeric:tabular-nums`, runtime `` `${assetOn.size}/7` ``, initial "7/7"), `.ic.s14` `#i-caretup`. Hotkey **O**.
   - **`.pop`** popup: `display:none` until `.haspop:hover` or `.haspop.open`; `position:absolute; left:-4px; bottom:34px; z-index:40`; `width:250px`; `background:#101822`; `border:1px solid var(--line2)`; `border-radius:5px`; `padding:7px`; `box-shadow:0 -14px 40px -12px #000`.
     - `.ph` header "Draw which classes": `color:var(--faint)`, **9px**, `letter-spacing:.16em`, uppercase, `padding:0 2px 6px`.
     - `.pg` grid **2 columns**, `gap:4px`. Each `.pc`: `height:24px`, `padding:0 7px`, `border-radius:3px`, `background:var(--panel2)`, `border:1px solid var(--line)`, `color:var(--dim)`, **10.5px**, gap 6px. First child SVG is a `.ic.s12` `#i-check` at `opacity:0`, going to `opacity:1` when `.on`; second is a `.ic.s14.gi` class icon at `opacity:.75`. `.pc.on` → `background:#17262f; border-color:#3c5f6e; color:var(--cyan)`.
     - Seven entries in order (app.js:36-44), icons from `ICON` map (app.js:1358-1360): `buildings`→"Buildings" `#i-house`; `terrain`→"Trees, rocks" `#i-trees`; `walls`→"Walls" `#i-wall`; `tiberium`→"Tiberium" `#i-crystal`; `units`→"Vehicles" `#i-tank`; `infantry`→"Infantry" `#i-person`; `decals`→"Craters, decals" `#i-dots`.
6. **`.iogroup`** -- icon-only toggle cluster: `background:#0b1016`, `border:1px solid var(--line)`, `border-radius:4px`, `padding:2px`, `gap:3px`. Children are `.vtog.io` = **32×24px** (`width:32px`, `padding:0`, `height:24px` from `.iogroup .vtog`), transparent border/background; `.on` → `background:var(--panel3); border-color:transparent; color:var(--cyan)`. Each carries an absolute keycap `.k` at `right:2px; bottom:0; font-size:8.5px; line-height:1.4; color:var(--faint)` → `#6f9fb2` when on.
   - `#tPads` `#i-pad` key **L** (on by default)
   - `#tWire` `#i-mesh` key **R** (off)
   - `#tBorder` `#i-frame` key **B** (on)
   - `#tWater` `#i-water` key **P** (on)
7. **`#edNogo` (`.vtog.warn`)** -- 28px tall labelled toggle: `.ic.s16` `#i-hatch`, text "No-go", `.cnt` `#edNogoCount` (runtime `nogoCount.toLocaleString()`), `.k` "N" (`var(--dimmer)`, 9.5px, `margin-left:1px`). `.warn.on` → `background:#2a1c1c; border-color:#6b3c34; color:#ff9a8c`.
8. **`.scale`** -- `margin-left:auto`, flex gap 7px, `flex:0 0 auto`:
   - `.vlab` "Scale"
   - `.v` "0.2×" -- inline `width:auto; color:var(--faint)`
   - `<input type="range" min="0" max="60" value="10" id="exag">` -- `width:98px`, `accent-color:var(--cyan)`
   - `.v` "5.0×" -- inline `width:auto; color:var(--faint)`
   - `.v#exagV` -- `color:var(--cyan)`, `font-variant-numeric:tabular-nums`, `width:32px`, `text-align:right`. **Runtime formula (app.js:1384-1386):** `exag = Math.round((0.2 + v*0.08)*10)/10` for slider `v ∈ [0,60]`, printed as `exag.toFixed(1)+'×'`. Endpoints therefore 0.2× and 5.0×; default `v=10` → **1.0×**.
   - `#trueScale` (`.vtog`, 28px) -- text "Snap true"; `onclick` sets slider to **10** (=1.0×), i.e. 64 height bytes = one cell width.

---

## 8. RIGHT SIDEBAR -- `#side`

```
width: clamp(340px, 24vw, 460px);   flex:0 0 auto; overflow:hidden
display:flex; flex-direction:column; gap:7px; padding:7px;
background: linear-gradient(#131922, #0e141c);
border-left: 1px solid #05080c;
box-shadow: inset 1px 0 0 rgba(255,255,255,.05);
class="hatch"                        /* 45° 2px/6px 1.6% white hatch */
```
Inner content width = `W_side − 14px`.

Four stacked children, top → bottom, with 7px gaps:

| Order | Element | Height |
|---|---|---|
| 1 | `#topbox` (`.box .bev`) | `flex:0 1 auto; min-height:0;` **`max-height:40%`** of `#side`'s height. When `#topbox.grow` (Missions tab open): `flex:1 1 auto; max-height:none` |
| 2 | `#modes` | `flex:0 0 auto` → **54px** (one row of `.mtab`) |
| 3 | `.pane.on` | `flex:1; min-height:0` -- takes all remaining space |
| 4 | `#foot` | `flex:0 0 auto` -- content height |

`body.browsing` (set when the Missions tab is active, editor.js:2296) applies `#modes, .pane { display:none !important }` -- the mission browser owns the whole sidebar.

`.box` = `background:var(--panel); border:1px solid var(--line); border-radius:5px`.

### 8.1 `#topbox` tab strip -- `.boxtabs`

```
display:flex; border-bottom:1px solid var(--line);
background:#0e141c; border-radius:4px 4px 0 0; overflow:hidden
```
`.boxtab` = `flex:1` (three equal thirds), **height 27px**, centred flex gap 6px, `color:var(--dimmer)`, **10px**, `letter-spacing:.12em`, uppercase, `border-right:1px solid var(--line)` (none on last).
- hover → `color:var(--dim); background:#121a24`
- `.on` → `color:var(--gold); background:var(--panel); box-shadow: inset 0 -2px 0 var(--gold)`
- `.boxtab .n` → `color:var(--dimmer)`, **9px**

Three tabs, left → right:
1. `radar` -- `.ic.s14` `#i-radar`, "Radar" (**default `.on`**)
2. `miss` -- `#i-list`, "Missions", `.n#missCount` = `META.maps.length` (markup default "100")
3. `info` -- `#i-info`, "Map info"

`.face{display:none;min-height:0}`, `.face.on{display:flex;flex-direction:column;flex:1 1 auto;min-height:0}`.

### 8.2 Face A -- RADAR

`.radarrow{display:flex; gap:8px; padding:7px}`

**`#radar`** -- **172×172px**, `flex:0 0 172px`, `position:relative`, `background:#070a0e`, `border:1px solid #000`, `border-radius:3px`, `overflow:hidden`, `box-shadow: inset 0 0 0 1px rgba(143,228,255,.10), inset 0 0 34px rgba(0,0,0,.9)`.
- `#radarcv` -- `<canvas width="64" height="64">` stretched to `width:100%;height:100%`, `image-rendering:pixelated` → **1 cell = 2.6875 screen px**.
- `#rframe` -- camera frustum box: `border:1px solid #fff`, `box-shadow:0 0 0 1px rgba(0,0,0,.7), 0 0 12px rgba(255,255,255,.25)`, `cursor:move`, plus an `::after` at `inset:-1px` with `1px solid rgba(255,255,255,.25)`. CSS start values `left:72px; top:78px; width:54px; height:43px`, **overwritten every frame** (editor.js:2094-2097) by `left/top/width/height = clamp(cellCoord,0,64)/64*100 + '%'`, computed by ray-casting the four NDC corners onto the y=0 plane; hidden entirely if fewer than 4 corners hit.
- `.radarfoot` -- absolute strip at the bottom, `padding:3px 5px`, `background:linear-gradient(rgba(0,0,0,0),rgba(0,0,0,.8))`, `color:var(--dimmer)`, **9px**, `letter-spacing:.06em`, `justify-content:space-between`. Left: `#radarPlay` = `` `PLAYABLE ${PX},${PY} · ${PW}×${PH}` ``. Right: static `64×64`.
- Pixel colours drawn into the canvas (editor.js:2053-2058): tiberium `[126,224,160]`, GDI houses 0/4 `[224,176,112]`, Nod houses 1/5 `[255,106,90]`, other `[154,167,180]`; terrain shaded by `k = 0.72 + 0.55*(h/255)` and dimmed to `d = 0.42` outside the playable rectangle.

**`.surfcol`** -- `flex:1; min-width:0`, column, `gap:4px`:
- `.cap` "Jump to": `color:var(--dimmer)`, **9px**, `letter-spacing:.16em`, uppercase, `padding:1px 2px 2px`
- Three `.sbtn`: **height 29px**, `padding:0 8px`, `border-radius:4px`, `background:var(--panel2)`, `border:1px solid var(--line)`, `color:var(--dim)`, **11px**, gap 7px. Hover → `border-color:var(--line2); color:var(--ink)`. `.on` → `background:#17262f; border-color:#3c5f6e; color:var(--cyan)`.
  1. `#jumpGdi` `.ic.s16` `#i-house` (inherits colour) "Your base"
  2. `#jumpNod` `#i-side` inline `color:#ff6a5a` "Enemy base"
  3. `#jumpTib` `#i-crystal` inline `color:#7ee0a0` "Tiberium"
- `.mini#radarMini` -- `margin-top:auto`, `color:var(--dimmer)`, **10px**, `line-height:1.5`; `b` → `var(--dim)` weight 400. Three runtime lines (editor.js:2071-2074): `Heights <b>lo-hi</b>`, `N structures · N units`, `N infantry · N scenery`.

### 8.3 Face B -- MISSIONS (browser)

Top → bottom:

1. **`.srch`** -- `margin:7px 7px 0`, **height 30px**, `padding:0 9px`, gap 7px, `background:#0b1016`, `border:1px solid var(--line2)`, `border-radius:4px`, `color:var(--dim)`. Contains `.ic.s16` `#i-search`; `<input id="filter">` (`flex:1`, transparent, no border/outline, `color:var(--ink)`, `font:12px var(--mono)`, placeholder `color:var(--faint)`, placeholder text "Filter 100 maps by id or name"); `.hits#hits` (`color:var(--gold)`, tabular, **11px**).
2. **`.filters#chips`** -- `padding:7px 7px 0`, column, `gap:5px`. One `.fgrp` per filter family: flex, gap 6px, `align-items:flex-start`.
   - `.fl` label -- `flex:0 0 46px`, `color:var(--faint)`, **9px**, `letter-spacing:.14em`, uppercase, `padding-top:6px`.
   - `.fw` -- flex wrap, `gap:4px`.
   - `.chip` (rendered as `<button>`) -- **height 22px**, `padding:0 7px`, `border-radius:3px`, `background:var(--panel2)`, `border:1px solid var(--line)`, `color:var(--dim)`, **10.5px**, gap 5px, `white-space:nowrap`. `.n` count = `var(--faint)`, **9.5px**, tabular. Optional `.sd` dot = **6×6px**, `border-radius:50%`, `background:var(--c, #8b949e)` -- set only for `gdi` (`#e0b070`) and `nod` (`#ff6a5a`). `.chip.on` → `background:color-mix(in srgb, var(--gold) 18%, #131a24); border-color:var(--gold); color:var(--gold)`, `.n` → `color-mix(in srgb, var(--gold) 62%, #000)`. Chips with zero matches get the `disabled` attribute (no custom style).
   - Filter families in order (app.js:1430-1471): **Set** [Campaign, Spec Ops, Covert Ops, Dinosaurs, Bonus, Unused] · **Side** [GDI, Nod] · **Media** [Cartridge, DOS CD] · **Elevation** [Heightmap, No heightmap].
   - `#chipclear` -- appended after the groups: `padding:3px 2px 0`, `color:var(--cyan)`, **10px**, `cursor:pointer`, `letter-spacing:.04em`, underline on hover; `display` toggled between `block` and `none` by `active.size`.
3. **`.mlist#list`** -- `flex:1 1 auto; min-height:0; overflow-y:auto; margin:7px 7px 0`; `background:#0c1119`; `border:1px solid var(--line)`; `border-radius:4px`. Scrollbar: **9px** wide, thumb `#2c384a`, `border-radius:5px`, `border:2px solid #0c1119` (so the visible thumb is 5px).
   - Group headers `.grp` -- `position:sticky; top:0`; `padding:8px 8px 3px`; `color:var(--dimmer)`; **10px**; `letter-spacing:.08em`; uppercase; `background:var(--panel)`; `z-index:1`. Trailing count span at `opacity:.5`. Nine groups in fixed order (app.js:1509-1519): Spec Ops N64 exclusive · GDI campaign · Nod campaign · Covert Ops · Bonus missions · Debug rows · Named by nothing · Dinosaur missions · Multiplayer.
   - Rows `.mlist .row` -- flex, `align-items:center`, gap 8px, `padding:4px 8px`, `cursor:pointer`, `border-left:2px solid transparent`. Hover `background:var(--panel2)`. `.on` → `background:var(--panel2); border-left-color:var(--gold)`. Children: `.dot` **6×6px** circle (`flex:0 0 6px`, theater colour); `.id` `var(--ink)` **11px**; `.nm` `flex:1`, `var(--dim)`, **11px**, italic, ellipsis (variant `.nm.lbl` → `opacity:.6; font-style:normal`); `.rl` `var(--dimmer)` **10px** showing `(relief/64).toFixed(1)+'×'` or `flat`. `.row.flat .dot{opacity:.35}`.
   - `#empty` -- `padding:18px 10px`, centred, `var(--faint)`, **11px**, "Nothing matches those filters."
   - The `.mhd`/`.mrow` styles (sticky 25px-row list) are the **mockup's** list and are unused -- dead CSS.
4. **`.brief#briefSec`** -- `flex:0 0 auto`; `margin:7px`; `background:#0c1119`; `border:1px solid var(--line)`; `border-radius:4px`; `padding:8px 9px`; **`max-height:132px`**; `overflow-y:auto` (same 9px scrollbar). `.bh` header: gold, **10px**, `letter-spacing:.14em`, uppercase, gap 7px, `margin-bottom:5px`, `.ic.s14` `#i-brief`; `.bh .sub#briefSub` → `margin-left:auto`, `var(--faint)`, **9.5px**, `letter-spacing:.06em`, not uppercase, runtime `` `${scenario} · ${briefingTag || 'ENG/MISSION.ENG'}` ``. Body `p` = `var(--ui)` **11px/1.5** `var(--dim)`, `margin:0 0 6px` (last child 0). The whole section is `display:none` when the map has no briefing.

### 8.4 Face C -- MAP INFO

`.readout#facts` -- `flex:1 1 auto; min-height:0; overflow-y:auto; margin:7px; padding-right:3px`. Scrollbar thumb `#2c384a` with `2px solid var(--panel)` border.

Per section `.rsec` (`margin-bottom:9px`):
- `.rh` heading -- flex gap 7px, `var(--faint)`, **9px**, `letter-spacing:.16em`, uppercase, `margin-bottom:4px`, with an `::after` rule `flex:1; height:1px; background:var(--line)` filling the remaining width.
- `.kv` definition grid -- `grid-template-columns:auto 1fr`, `gap:2px 10px`, **11px**, `margin-bottom:5px`. `dt` = `var(--faint)`; `dd` = `var(--ink)`, `margin:0`, tabular numerals.
- `.rsec p` -- `var(--ui)` **11px/1.5** `var(--dim)`, `margin:0 0 5px`; `b` → `var(--ink)` weight 600.
- `#facts .bars` -- histogram: flex, `align-items:flex-end`, `gap:3px`, **height 40px**, `margin:8px 0 2px`; each `i` is `flex:1` with `background:linear-gradient(var(--gold), #8a6a3f)`, `border-radius:1px 1px 0 0`. **Bar height formula (app.js:1214-1215):** `max(3, round(38 * (count/total) / maxFraction))` px, where bars are the height bytes covering ≥2% of the playable rectangle. `.barlab` = `var(--faint)`, **9px**, `letter-spacing:.04em`, the byte values joined by `' · '`.
  (The generic `.bars` rule -- height 34px, margin `4px 0 6px`, gradient `#3f5064→#22303f` -- is overridden here.)
- Three sections in order: **Heightmap** (Source, Range, Relief, Corners `65 × 65 = 4,225`, Playable) + histogram + prose; **Objects on this map** (All + per-kind counts); **How things are drawn** (prose paragraphs about meshes, infantry, sea, missing meshes).

### 8.5 MODE TABS -- `#modes`

```
display:grid; grid-template-columns:1fr 1fr 1fr; gap:6px; flex:0 0 auto
```
`.mtab` -- **height 54px**, `border-radius:5px`, `background:linear-gradient(#1b232e, #151b24)`, `border:1px solid var(--line)`, column flex centred, `gap:4px`, `color:var(--dim)`, `position:relative; overflow:hidden`.
- `.ic.s24` icon (24×24, stroke 1.5)
- `.t` title -- **11px**, `letter-spacing:.16em`, uppercase, weight 600
- `.s` subtitle -- **9.5px**, `var(--dimmer)`
- `.kk` keycap -- `position:absolute; top:4px; right:6px; font-size:9px; color:var(--dimmer)`
- `.on` → `color:#0e1218`, `border-color:var(--mode)`, `background:linear-gradient(var(--mode), color-mix(in srgb, var(--mode) 78%, #000))`, `box-shadow:0 0 0 1px rgba(0,0,0,.7), 0 6px 18px -8px var(--mode)`; `.t` weight 700; `.s`/`.kk` → `rgba(12,16,22,.80)`

| Order | `data-m` | `data-c` | Icon | Title | Subtitle (static markup) | Key |
|---|---|---|---|---|---|---|
| 1 | `objects` (default on) | `#e0b070` | `#i-house` | Objects | `5 palettes` | 1 |
| 2 | `terrain` | `#7ee0a0` | `#i-water` | Terrain | `7 drawers` | 2 |
| 3 | `elevation` | `#8fe4ff` | `#i-cliff` | Elevation | `5 tiers · 3 tools` | 3 |

The three subtitle strings are literal HTML and never updated at runtime.

### 8.6 PANE A -- OBJECTS (`section.pane[data-p="objects"]`)

`.pane{flex:1; min-height:0; display:none; flex-direction:column; gap:7px}`, `.pane.on{display:flex}`.

1. **`.owners#edOwners`** -- `display:grid; grid-template-columns:repeat(4,1fr); gap:5px`. `.ow`: **height 38px**, `border-radius:4px`, `background:var(--panel2)`, `border:1px solid var(--line)`, **`border-top:3px solid var(--c)`**, column-centred, `gap:1px`, **10.5px**, `color:var(--dim)`. `.h` = `var(--dimmer)`, **9px**, `letter-spacing:.08em`. `.on` → `background:color-mix(in srgb, var(--c) 22%, #10161e); border-color:var(--c); color:#fff`, `.h` → `var(--c)`.
   Four entries (editor.js:1481-1483) with `--c` from `HOUSES`: `GOODGUY`/"Yours" `#e0b070`, `BADGUY`/"Enemy" `#ff6a5a`, `NEUTRAL`/"Neutral" `#9aa7b4`, `SPECIAL`/"Special" `#b98fe4`.
2. **`.cats#cats`** -- flex, `gap:3px`, `background:#0b1016`, `border:1px solid var(--line)`, `border-radius:4px`, `padding:3px`. `.cat`: `flex:1`, column centred, `gap:1px`, `padding:4px 0 3px`, `border-radius:3px`, **9.5px**, `color:var(--dim)`; `.n` = `var(--dimmer)`, **9px**, tabular. `.on` → `background:var(--panel3); color:var(--mode); box-shadow:inset 0 1px 0 rgba(255,255,255,.07)`, `.n` → `color-mix(in srgb, var(--mode) 70%, #8b949e)`. Icon `.ic.s16`.
   Five categories in fixed order with fixed icons (`CATICON`, editor.js:1490): Buildings `#i-house` (22 types) · Units `#i-tank` (16) · Infantry `#i-person` (13) · Scenery `#i-trees` (19) · Overlay `#i-crystal` (10).
3. **`.gridwrap`** → **`.grid5#palgrid`** (see §9).

### 8.7 PANE B -- TERRAIN

1. **`.drawers#drawers`** -- flex wrap, `gap:4px`. `.dw`: **height 28px**, `padding:0 9px`, `border-radius:4px`, `background:var(--panel2)`, `border:1px solid var(--line)`, `color:var(--dim)`, **11px**, gap 6px; `.n` = **9.5px** `var(--dimmer)` tabular. `.on` → `background:color-mix(in srgb, var(--mode) 16%, #131a24); border-color:var(--mode); color:var(--mode)`, `.n` → `color-mix(in srgb, var(--mode) 70%, #000)`. Icon `.ic.s14`.
   Seven drawers with their icons (`TERRAIN_GROUPS` editor.js:622-630 + `DRAWICON` editor.js:1548-1550): **Ground** `#i-flat` · **Water** `#i-water` · **Shore** `#i-water` · **Rivers** `#i-water` · **Roads** `#i-tiles` · **Rock** `#i-trees` · **Bridges** `#i-tiles`.
2. **`.derived`** hint strip -- flex, `gap:7px`, `align-items:flex-start`, `color:var(--dimmer)`, **10px/1.45**, `var(--ui)`, `padding:1px 2px`; `b` → `var(--dim)` weight 600; leading `.ic` gets `flex:0 0 14px; margin-top:1px`.
3. **`.gridwrap`** → **`.blocks#blockgrid`** (see §10).

### 8.8 PANE C -- ELEVATION (`#elevBody`)

`#elevBody{flex:1 1 auto; min-height:0; overflow-y:auto; display:flex; flex-direction:column; gap:7px}`

**Ungated state** (map not yet converted) -- a single `.gate`: `border:1px solid #6b5326`, `background:linear-gradient(#221c11, #1a160e)`, `border-radius:5px`, `padding:8px 9px`, flex `gap:8px` centred. `.ic` inline-sized to **22×22**, `stroke-width:1.64`, `color:var(--gold)`, symbol `#i-warn`. `.tt` = `#f0c070`, **10px**, `letter-spacing:.1em`, uppercase. `p` = `var(--dim)`, **10px/1.4**, `var(--ui)`, `margin:3px 0 0`. Button `#edConvert`: `flex:0 0 auto`, `align-self:stretch`, `background:#4a3a1a`, `border:1px solid #8a6a2c`, `color:#f5d9a0`, `border-radius:4px`, `padding:0 11px`, `font:10.5px var(--mono)`, `line-height:1.3`, **`max-width:96px`**, hover `#5c4820`.

**Converted state**, top → bottom:
1. `.secline` "Tier brush" -- flex, `gap:8px`, `var(--dimmer)`, **9.5px**, `letter-spacing:.16em`, uppercase, `margin-top:2px`, with an `::after` `flex:1; height:1px; background:var(--line)` rule.
2. `.tiers` -- flex, `gap:5px`, `align-items:flex-end`. Five `.tr`, `flex:1`, `border-radius:4px 4px 3px 3px`, `background:var(--panel2)`, `border:1px solid var(--line)`, column, `justify-content:flex-end`, `gap:4px`, `padding:5px 0 6px`, `color:var(--dim)`, **10.5px**. `.bar` = `width:60%`, `background:linear-gradient(#3d4c60,#26313f)`, `border:1px solid #4b5c72`, `border-radius:1px`, **`height = 8 + t*9` px** for t=0..4 → **8, 17, 26, 35, 44px**. Labels: t=1→"Ground", t=0→"Low", else `"+"+(t-1)`. `.on` → `border-color:var(--mode); background:color-mix(in srgb, var(--mode) 15%, #131a24); color:#fff`, bar → `linear-gradient(var(--mode), color-mix(in srgb, var(--mode) 55%, #000))` with `border-color:var(--mode)`.
3. `.secline` "Tool"
4. `.row3` -- `grid-template-columns:1fr 1fr 1fr; gap:5px`. Three `.tbtn`: **height 46px**, `border-radius:4px`, `background:var(--panel2)`, `border:1px solid var(--line)`, column-centred, `gap:4px`, **10.5px**, default `.ic` 20×20. Order: `tier` `#i-raise` "Raise / lower" · `pass` `#i-grade` "Graded pass" · `ramp` `#i-ramp` "Ramp (east)". `.on` → `border-color:var(--mode); color:var(--mode); background:color-mix(in srgb, var(--mode) 14%, #131a24)`.
5. `.secline` "Brush size"
6. `.row3` of three `.bsz`: **height 40px**, row flex centred, `gap:8px`, **11px**. `.sq` = `border:1px solid currentColor`, `opacity:.85`, **`size = 4 + b*8` px** for b=1..3 → **12, 20, 28px**; labels "2×2", "4×4", "6×6".
7. `.derived` explanatory strip (icon `.ic.s14` `#i-info`).
8. Optional `.derived.bad` -- final cascade (line 511): `border-left:2px solid var(--danger); padding-left:7px; color:#ffb3a8`, icon `#i-warn`.

### 8.9 FOOT -- `#foot`

`display:flex; flex-direction:column; gap:5px; flex:0 0 auto`. Children top → bottom:

1. **`.lint.bev#lintbox`** -- `background:var(--panel)`, `border:1px solid var(--line)`, `border-radius:5px`, `overflow:hidden`.
   - `.hd` -- flex, `gap:8px`, `padding:6px 9px`, `background:#0e141c`, `border-bottom:1px solid var(--line)`, `cursor:pointer`. Contains `.ic.s16` `#i-check` coloured `var(--dim)`; `.t` "Mission check" (`var(--dim)`, **10px**, `letter-spacing:.16em`, uppercase); then `margin-left:auto` `.tally.ok#tallyOk` and `.tally.bad#tallyBad` -- flex gap 4px, **11px**, tabular; `.ok` → `var(--ok)`, `.bad` → `var(--danger)`; each is icon (`.ic.s14` `#i-check` / `#i-x`) + count. `#tallyBad` is `display:none` when zero.
   - `#edLint` rows: `.lrow` -- flex `gap:8px`, `padding:4px 9px`, `border-top:1px solid #1a222c` (none on first), `align-items:flex-start`. `.k` = `width:60px; flex:0 0 60px`, `var(--ink)`, **10.5px** (`.bad .k`→danger, `.ok .k`→ok, `.note .k`→dim). `.d` = `flex:1`, `var(--dim)`, **10px/1.35**, `var(--ui)`.
   - `.lmore#lintmore` -- collapsed summary row: `padding:5px 9px`, `var(--dimmer)`, **10.5px**, flex gap 6px, `border-top:1px solid #1a222c`, `cursor:pointer`; `.lmore.open svg{transform:rotate(180deg)}` (caret `#i-caret`).
   - `.lrest#lintrest` -- `display:none`, `.open{display:block}`.
2. **`.acts`** -- `display:grid; grid-template-columns:1fr 1fr 1fr 1fr; gap:5px`. `.act.bev` = **height 32px**, `border-radius:4px`, `background:linear-gradient(#1c242f, #161d26)`, `border:1px solid var(--line)`, `color:var(--dim)`, centred flex `gap:5px`, **10.5px**; `.k` keycap **9px** `var(--dimmer)`; disabled `.act.dis` → final cascade `opacity:.35` + `pointer-events:none` + `cursor:default`. Icons `.ic.s16`.
   Order: `#edUndo` `#i-undo` "Undo" `^Z` · `#edRedo` `#i-redo` "Redo" `^Y` (**starts `.dis`**) · `#edReset` `#i-revert` "Revert" (no keycap) · `#edIniBtn` `#i-doc` ".INI" `▸`.
3. **`.inibox#inibox`** -- `display:none`, `.open{display:block}`; **`max-height:min(200px,22vh)`**, `overflow:auto`, `background:var(--bg)`, `border:1px solid var(--line)`, `border-radius:5px`, `margin-bottom:6px`. `pre` = `margin:0; padding:8px; font:10px/1.45 var(--mono); color:var(--dim); white-space:pre`.
4. **`.playrow`** -- `display:grid; grid-template-columns:auto 1fr; gap:5px`.
   - `#playbtn` -- auto width, **height 42px**, `padding:0 14px`, `border-radius:5px`, `background:linear-gradient(#8fe4ff, #4aa8cc)`, `border:1px solid #b6efff`, `color:#0d1117`, **12px**, weight 700, `letter-spacing:.1em`, gap 6px, `box-shadow:0 1px 0 rgba(255,255,255,.35) inset, 0 8px 20px -10px #4aa8cc`. Icon `.ic.s16` `#i-play`, label "PLAY", `.k` "^P" (**9px**, `opacity:.55`, weight 400). Hover `filter:brightness(1.08)`; `.busy` → `filter:grayscale(.6); cursor:progress`.
   - `#savebtn` -- fills the rest, **height 42px**, `border-radius:5px`, `border:1px solid #c79a5c`, `background:linear-gradient(#e8bd80, #c9944d)`, `color:#1a1206`, centred flex `gap:9px`, **13px**, weight 700, `letter-spacing:.1em`, `box-shadow: 0 1px 0 rgba(255,255,255,.35) inset, 0 -2px 0 rgba(0,0,0,.28) inset, 0 8px 22px -10px #e0b07088`. Hover `linear-gradient(#f2c98d, #d6a058)`. Contents: `.ic.s24` `#i-save`, text "CHECK AND SAVE", `.ext` ".INI · .BIN · .HGT" (**9.5px**, weight 400, `opacity:.65`, `letter-spacing:.04em`), `.k` "^S" (**10px**, weight 500, `opacity:.6`).
5. **`.joblog#joblog`** -- `display:none`, `.open{display:block}`; `margin-top:6px`, `background:var(--bg)`, `border:1px solid var(--line)`, `border-radius:5px`, `overflow:hidden`. `.jh` header: `padding:5px 9px`, `background:#0e141c`, `border-bottom:1px solid var(--line)`, **10px**, `var(--dim)`, `letter-spacing:.06em`, uppercase, gap 8px; `.x#jobclose` at `margin-left:auto`, `var(--faint)`, not uppercase, hover `var(--ink)`. `pre` = `padding:7px 9px`, **`max-height:170px`**, `overflow:auto`, `font:10px/1.5 var(--mono)`, `color:var(--dim)`, `white-space:pre-wrap; word-break:break-word`. `.joblog.bad .jh{color:var(--danger)}`.

---

## 9. THE CAMEO / PALETTE GRID

Container **`.gridwrap`**:
```
flex:1; min-height:120px; overflow-y:auto;
background:#0c1119; border:1px solid var(--line); border-radius:5px; padding:7px
scrollbar: width 9px, thumb #2c384a, radius 5px, border 2px solid #0c1119
```

Grid **`.grid5`**: `display:grid; grid-template-columns:repeat(5,1fr); gap:5px` → **5 columns, 5px gutters**.

**Column width formula** (all values from CSS):
```
W_side   = clamp(340px, 24vw, 460px)
inner    = W_side − 14 (#side padding)
well     = inner − 2 (gridwrap border) − 14 (gridwrap padding) = W_side − 30
colW     = (well − 4×5) / 5 = (W_side − 50) / 5
```
→ **58.0px at W_side=340**, **70.0px at 400**, **82.0px at 460**.

**`.tile`**: `border-radius:4px`, `background:var(--panel2)`, `border:1px solid var(--line)`, `padding:3px 3px 4px`, `display:flex; flex-direction:column; gap:3px`, `position:relative`, `cursor:pointer`.
- hover → `border-color:var(--line2); background:var(--panel3)`
- `.on` → `border-color:var(--mode); background:color-mix(in srgb, var(--mode) 14%, #131a24); box-shadow:0 0 0 1px var(--mode), 0 6px 18px -10px var(--mode)`

**`.cam`** (the cameo picture): `position:relative; width:100%;` **`aspect-ratio:32/24`** (4:3), `border-radius:2px`, `overflow:hidden`, `background:#0a0c10`, `image-rendering:pixelated`.
```
camW = colW − 2 (tile border) − 6 (tile padding) = colW − 8
camH = camW × 0.75
tileH = camH + 34      (1+1 border + 3 top pad + 3 gap + 22 name + 4 bottom pad)
```
→ at colW=70: cam **62×46.5px**, tile **80.5px** tall.

**Label placement -- three badges, all absolutely positioned inside `.tile`, plus one flow label under the picture:**
- `.tile .code` -- **top-left**, `left:2px; top:2px`; **8px**, `letter-spacing:.06em`, `color:#0d1117`, `border-radius:2px`, `padding:0 3px`, `line-height:12px`. Background `rgba(224,176,112,.88)` (GDI), `.code.nod` `rgba(208,138,106,.9)` when owner index 1, `.code.mesh` `rgba(154,167,180,.88)` when the tile is a rendered mesh rather than a cameo. Text = the 4-letter type code.
- `.tile .fp` -- **bottom-right**, `right:2px; bottom:2px`; **8px**, `color:#dfe8f2`, `background:rgba(0,0,0,.62)`, `border-radius:2px`, `padding:0 3px`, `line-height:12px`. Only for structures; text = `W×H` footprint.
- `.cam.mesh::after` -- **top-right of the picture**, `right:2px; top:2px`; content `"3D"`; **7.5px**, `line-height:11px`, `padding:0 3px`, `border-radius:2px`, `color:#bfe8fb`, `background:rgba(20,44,58,.9)`, `letter-spacing:.06em`.
- `.tile .nm` -- **below the picture, in flow**: `var(--ui)`, **9.5px/1.15**, `color:var(--dim)`, **fixed `height:22px`**, `overflow:hidden`, `text-align:center`. `.tile.on .nm` → `var(--ink)`. Suppressed (empty string) when the display name is not unique within the tab, so the `.code` badge carries the identity instead (editor.js:1500-1504).

**Cameo image source** (`tileArt`, editor.js:152-176) -- a sprite sheet at `data/{T.cameos.sheet}` addressed by background-position:
```
background-size:     (100 * sheetW / rectW)% (100 * sheetH / rectH)%
background-position: (100*rectX/(sheetW-rectW))% (100*rectY/(sheetH-rectH))%
```
Nod side variant chosen when `house === 1`. Where the game has no cameo, the cartridge mesh is rendered to a **96px** offscreen canvas (`const THUMB = 96`) and used with `background-size:contain; background-position:center; image-rendering:auto`.

---

## 10. THE TERRAIN BLOCK GRID

Container is the same `.gridwrap`; the inner element is **`.blocks#blockgrid`**: `display:flex; flex-wrap:wrap; gap:8px; align-items:flex-start` -- **not a fixed grid**; blocks are their real footprint and wrap.

`.blk`: `cursor:pointer`, column flex, `gap:3px`, `align-items:center`, **`width: var(--bw, 54px)`**.
**Runtime sizing (editor.js:1567-1580), `const CELL = 18`:**
```
--bw          = row.w * 18 + 2   px      (tile width + the .art border)
.art width    = row.w * 18       px
.art height   = row.h * 18       px
```
`.art` = `border:1px solid var(--line)`, `border-radius:2px`, `overflow:hidden`, `background:#0a0c10`, `image-rendering:pixelated`, `background-size:100% 100%` over a data-URL composed from the theater atlas (`TS=24, PITCH=26, GUTTER=1, COLS=32`, app.js:9).
- hover → `.art` `border-color:var(--line2)`
- `.on` → `.art` `border-color:var(--mode); box-shadow:0 0 0 1px var(--mode), 0 6px 16px -8px var(--mode)`

`.blk .lb` label **below** the art: `var(--mono)`, **9px**, `color:var(--dim)`, `text-align:center`, `line-height:1.25`, `letter-spacing:.02em`; contains the INI name, then a nested `i` on its own line (`display:block; font-style:normal; color:var(--faint); font-size:8.5px`) with `W×H`. `.blk.on .lb` → `var(--mode)`, its `i` → `var(--dim)`.

Empty/loading states render a `.derived` block instead of tiles.

---

## 11. BOTTOM / STATUS BAR

There is **no bottom bar spanning the window.** The three things that would normally live in one are distributed:
- **`#viewbar`** (§7) is the bottom bar *of the stage column only* -- `min-height:44px`, wraps.
- **`#status`** (§6) is a floating card in the viewport, 18px above its bottom edge, `min(560px, 100% − 32px)` wide.
- **`#savestate`** in the top bar carries the save/dirty state.

---

## 12. ICON SYSTEM

One inline `<svg width="0" height="0">` sprite sheet with **57 `<symbol>` elements**, all `viewBox="0 0 24 24"`, referenced via `<use href="#id">`. Base class `.ic`:
```
.ic      20×20, stroke:currentColor, stroke-width:1.8, fill:none, round caps/joins, flex:0 0 auto
.ic.f    fill:currentColor, stroke:none
.ic.s24  24×24, stroke-width:1.5
.ic.s16  16×16, stroke-width:2.25
.ic.s14  14×14, stroke-width:2.55
.ic.s12  12×12, stroke-width:3
```
Stroke width is scaled inversely with size so the optical weight stays constant (1.8×20 = 36; 1.5×24 = 36; 2.25×16 = 36; 2.55×14 = 35.7; 3×12 = 36).

Symbol ids: `i-select i-move i-brush i-drop i-erase i-ruler i-camera i-grid i-lock i-undo i-redo i-revert i-play i-save i-check i-x i-dash i-warn i-caret i-caretup i-radar i-list i-info i-house i-tank i-person i-tree i-crystal i-wall i-dots i-image i-flat i-tiles i-stairs i-cliff i-water i-eye i-frame i-mesh i-pad i-hatch i-layers i-raise i-grade i-ramp i-search i-doc i-brief i-scale i-del i-trees i-side`. Two are filled rather than stroked (`i-select`, `i-play`). Unreferenced by any markup or JS: `i-ruler`, `i-grid`, `i-lock`, `i-dash`, `i-tree`, `i-layers`, `i-scale`, `i-del`.

---

## 13. RUNTIME-COMPUTED VALUES (formulas, not constants)

| Value | Formula | Source |
|---|---|---|
| Sidebar width | `clamp(340px, 24vw, 460px)` | index.html:500 |
| `#topbox` height cap | `40%` of `#side` content height; `none` while browsing | index.html:507 |
| Palette column width | `(W_side − 50) / 5` | grid5 + paddings |
| Palette tile height | `0.75 × (colW − 8) + 34` px | tile box model |
| Terrain block width | `row.w × 18 + 2` px | editor.js:1575 |
| Terrain block art | `row.w × 18` by `row.h × 18` px | editor.js:1577 |
| Vertical scale readout | `round((0.2 + slider×0.08) × 10)/10` , slider ∈ [0,60] | app.js:1384 |
| Tier bar height | `8 + t × 9` px, t ∈ 0..4 | editor.js:1222 |
| Brush-size square | `4 + b × 8` px, b ∈ 1..3 | editor.js:1240 |
| Facts histogram bar | `max(3, round(38 × frac / maxFrac))` px | app.js:1214 |
| Radar frame rect | `clamp(cell,0,64)/64 × 100` % on all four edges | editor.js:2094-2097 |
| Radar canvas scale | 64px canvas → 172px box = **2.6875×**, nearest-neighbour | index.html:288 |
| Asset count chip | `` `${assetOn.size}/7` `` | app.js:1370 |
| No-go count | `nogoCount.toLocaleString()` | editor.js:1541 |
| INI preview cap | `min(200px, 22vh)` | index.html:504 |

---

## 14. DEAD CSS (present in the stylesheet, unreachable in the running app)

Rebuilding faithfully means **omitting** these -- they are the original static mockup's fake viewport and fake list:
`#iso`, `#horizon`, `#terrainblob`, `#riverblob`, `#ghost-lo`, `#ghost-hi`, `#ghostbody`, `#leader`, `.gcell`, `.gc-ok`, `.gc-bad`, `.mhd`, `.mrow` (and all `.mrow .*` descendants), `.railsep`, `#status .l1 .bib`, `.lrow.note`, `.tile .code.nod`/`.mesh` are live but `.blk`'s `54px` default `--bw` never applies, and the first `#view` background (`radial-gradient(120% 90% at 50% 8%, #1b2735 0%, #121a25 42%, #0a0e14 100%)`) is overridden by the flat `#0a0d11` at index.html:464.