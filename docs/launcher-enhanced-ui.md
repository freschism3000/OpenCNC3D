# Launcher Enhanced interface

The launcher composes the same native Enhanced-menu artwork as the game from
`enhanced-menu.pack`: the preserved1016x636 Visuals housing, hard metal button
bevels, fixed reflections, orange selected plates, gunmetal wells and original
GRAD6FNT lettering with black shadow pixels. It retains the existing module,
update, media, editor, keyboard and launch controllers. Missing or invalid
Enhanced artwork keeps the classic standalone-installer presentation.

The independent CPU compositor is `launcher/lskin.c`. Source chrome is never
resampled into the DOS palette. Layout uses the legacy controller coordinates,
converted once with nativeX=28+3*x, nativeY=18+3*y; glyphs retain the game's2x
bitmap scale. Presentation uses an integer nearest scale, and pointer positions
reverse the same transform. Native font advances also own wrapping and fitting.
Tall module faces use source material patches inside preserved bevel/fitting seats.

Actual screenshots and the browser review are saved in the working review archive
`art/interface/launcher-enhanced-v01` and served by the Remaster Lab at
`/cnc-interface/launcher-enhanced-v01/`. This new layout is a reviewable runtime
change; test success does not record artistic acceptance. No image generation,
new mesh, importer or shipped texture change is involved.

Run `tools/launcher/verify-enhanced.py LAUNCHER PLAYABLE OUTPUT` for CPU-only
screen, visible control geometry, media-state and protected-file checks.
The launcher skips its macOS network-permission probe for shots and background
checks. Current main must build and refresh both TD and separate RA executables
before the fresh launcher is copied to the existing Dock install as
`playable/cnc3d-launcher`. Preserve the existing C&C3D.app wrapper and player data.


## EVA interaction and frameless shell

The native skin keeps the approved Enhanced chrome, emblems and wordmark. A
stronger 16-second green transmission loop crosses the complete window, with
running perimeter accents and side telemetry. Every visible control and module
gets a staggered idle packet, moving hover illumination, held feedback and a
420 ms release pulse. These colour-only effects preserve source geometry,
black shadows, alpha and bright bitmap lettering. --no-motion disables them.

The SDL shell is borderless and resizable, with an integrated Close control at
(940,30), 45x33 native pixels. Empty header space drags the window; thin outer
edges resize it. Close uses the existing event table and cancels a drag-off
release. The plain first-install menu-art bootstrap also remains cancellable.

The live green pointer reuses the game's exact CUR00 mesh, textures, vertex
colours, 55-degree camera and standard/additive blends from ecur/ecur.bin. Its
position uses native pixels independently of the three-pixel control grid,
with another sample immediately before drawing. The 33 ms cap is removed;
the normal loop has an 8 ms minimum paint budget with driver vsync retained.
Hidden timings measure processing throughput, not visible display latency.
CPU --shot cursor edges have documented rounding differences from GL. The
actual live GL cursor is checked exactly against the game's original renderer.

CPU snapshots support --shot-mouse X,Y and --shot-press. The latter paints a
held state without activating a control. --signal-time MS fixes the animation
phase. --hidden-ui-test REPORT_DIR creates its SDL window hidden from the first
call, emits input/window evidence and exits. It never shows, raises, focuses or
warps the system pointer.

Validation tools under tools/launcher/: verify-enhanced.py covers all eleven
screens and the custom Close seat; verify-signal.py covers all 960 global
animation ticks plus clipped idle/hover/pressed/pulse reactions; check-ui.py
checks actual hidden SDL events and compares the live pointer against the
unchanged game renderer using cursor-reference.cpp. Python pixel checks need
Pillow and numpy; native cursor proof needs SDL/OpenGL and a local C++ compiler.

The approved OpenCNC3D shiny ring and final native review/evidence are saved in
art/interface/launcher-enhanced-v05/. Both module emblems share the same ring
pixels; only their fitted inset identity artwork differs.

## Platform/module browser brief and Launch caption — 19 October 2026

User defined OpenCNC3D as the platform, TD3D as the default Tiberian Dawn module,
and a future RA3D-specific package as the same platform with RA3D preselected.
The proposed browser flow is captured in the launcher notes:
+ opens the catalog; installed modules stay visible/greyed; available module opens
its description with Back/Install; Install reveals optional Music/Movies choices;
Download & Install completes a validated module and adds its tile. Screen design
still needs a versioned Lab review; catalog/download installation is not shipped.

The independent requested caption fix is implemented: ready action is Launch for
both current module selections, including after a completed update. Update/Updating
states retain their existing labels/controllers. Current-main launcher build passed
all11 CPU-only screen/geometry/protected-file checks. The launcher is deployed to
both existing installed aliases; Dock wrapper/player settings/saves stay untouched.
The existing TD/RA app binaries already match current main (no game/app/menu source
changes since their verified build), and their aliases are refreshed unchanged.
No accepted launcher v05 artwork, geometry, animation or source pixels were changed.


## Module Browser v01 and public installer rule — 19 October 2026

Current variation: `art/interface/launcher-module-browser-v01/`, built by
`tools/launcher/module-browser-lab/`, reviewed at
`/cnc-interface/module-browser-v01/`. Design-review visual acceptance is pending. This is
an interactive design with in-memory installs and simulated progress; no native
installer or Dock runtime delivery is claimed. Accepted launcher v05 sources stay
unchanged. The module name is **Dawn3D**, superseding TD3D in the earlier brief.
Exact current descriptions and full release checks live in
the launcher notes.

The design review added a mandatory future release rule: Windows setup installs only the
empty OpenCNC3D launcher, and the Module Browser installs the player's chosen game
plus optional Music/Movies. All public catalogs exclude RA3D entirely until the
user explicitly enables it. Private review scenarios may include RA3D; approval
of that preview does not permit public exposure. The native setup/packager still
needs conversion and verified first-run checks before the next installer release.
This supersedes the older bundled/default-Dawn3D installer plan for Windows.

Construction uses exact Enhanced frame/font/button pixels and unchanged v05
160×160 emblems /220×94 wordmark. Native1016×636 frame;900×166 catalog seats;
676×340 details and676×584 expanded media popup;40px buttons/12px corner guards;
original font advances at2×, max680px catalog /412px details. Stable hit controls,
modal-only focus, retained checkbox choices, disabled installed rows, hover/click
sounds and simulated cancel/retry/complete states are included. Public first-install
preview starts empty with Launch disabled and only Dawn3D in its catalog.

The initial tall-face reflection repeat and footer/button collision were fixed
before final screenshots; full user descriptions now wrap by measured advances.
Native and responsive actual browser screenshots, enlarged fitting crops,27 clean
screen geometry/text diagnostics and source hashes are preserved in the v01
README, `kit/manifest.json`, `review-manifest.json` and `evidence/`. Browser console
warnings/errors were empty; modal keyboard and install-state transitions were
exercised. Enhanced source pack SHA-256:
`8af4c72abd260c6f774ffba756b453b97bfc3711f0526b58151dfcbffb2b8a93`.
Technical review does not record acceptance or validate a real downloader.


### Conditional public build lineup — 19 October 2026

Only once public RA3D is explicitly enabled, release files use these seven
names: **Windows, Installer**; **Windows, Complete (Dawn3D)**;
**Windows, Complete (RA3D)**; **Windows, Binaries only**; **macOS, Installer**;
**macOS, Complete (Dawn3D)**; **macOS, Complete (RA3D)**. The final macOS label's
capitalization/closing parenthesis is normalized from the request. This records a
future naming/variant rule, not permission to enable RA3D or publish/rename builds
now. The module-browser guide has the canonical table and implementation boundary.
Current public catalogs/payloads continue to exclude RA3D. Windows Installer remains
the empty launcher flow; complete module packages are distinct choices.
