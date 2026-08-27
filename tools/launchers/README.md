# The double-click launchers

`playable/` is a BUILD OUTPUT folder and is gitignored in full (see `.gitignore`,
for why: it changes every build and is published as a GitHub
Release instead). Anything edited only there is lost on the next reassembly.

These are the tracked masters. Copy them into `playable/` when assembling a
build:

```
cp tools/launchers/*.command playable/ && chmod +x playable/*.command
cp -R "tools/launchers/C&C3D.app" playable/
```

**`READ-ME.txt` lives here too, and the packaging reads it from here.** It is
the paper that ships in the macOS zip beside these launchers, so it belongs with
them rather than in `docs/`. `tools/release.sh` copies THIS file into the
package; it does not take whatever `READ-ME.txt` happens to be sitting in
`playable/`. Edit it here.

That arrangement exists because the old one failed in silence. Earlier
the macOS package was made with `cp -RL playable`, so it shipped an untracked
`READ-ME.txt` that nothing regenerated and nobody reviewed: it still opened
"C&C 3D v0.3.1 (macOS build, )", still listed v0.3.1 as the news, and
had ridden along in every release since. A file that exists only in a gitignored
output folder ages out of sight.

**`C&C3D.app` is the default.** It is a real macOS app bundle rather than a
`.command` file, because only a bundle can carry an icon and a name -- that was asked
for one thing he can always double-click. It boots the DOS main menu on the NEW
640x480 sidebar HUD and takes no options. Its icon is the cartridge's own GDI
eagle medallion (`tools/sidebar_redesign/gdi_eagle/02_isolated_80x69.png`, masked
to its circle to drop the isolation fringe, upscaled 7x with NEAREST so the pixel
art stays crisp) on the new HUD's own panel tone. Regenerate it with
`tools/launchers/make_icon.py`.

The Desktop shortcut is a symlink to the copy in `playable/`, so rebuilding the
app in place keeps the shortcut working:
```
ln -s "$(pwd)"/playable/"C&C3D.app" ~/Desktop/"C&C3D"
```

This directory exists because three separate fixes were nearly lost to that
trap in one run: the `--dosinf` flag missing from `PLAY-GDI.command` and
`PLAY-NOD.command` (which is why the infantry looked low-res on those two
routes while `PLAY.command` was fine), and `TUNE-SHADOWS.command` itself.

## What each one is for

| file | what it does |
|---|---|
| `PLAY.command` | the real thing: 1995 DOS main menu, campaign, everything |
| `PLAY-GDI.command` | straight into GDI mission 1, skipping the menu |
| `PLAY-NOD.command` | straight into Nod mission 1 |
| `TUNE-SHADOWS.command` | Nod 1 with the shadow and explosion dials on live keys |

`TUNE-SHADOWS.command` is the one to reach for when something needs judging by
eye rather than measuring: it puts the art dials on the keyboard so a number can
be chosen by looking, then read off and made the default.


## Two binaries, and the trap that follows from it

`PLAY.command`, `PLAY-HUD-MENU.command` and `TUNE-PRESENTATION.command` launch
**`cnc3d`** (the app: menu, campaign, movies), and so does `C&C3D.app`. Every other
launcher here runs **`cnc_eyes`** (the renderer) directly. The split matters to a
player rather than just to a builder, because the Tier 2 presentation chain is on the
`cnc3d` side: `cnc_eyes` boots with `fx_defaults()`, whose master switch is off
(game/fx_state.h, `s->enabled = 0`), and never reads a saved preset without `--gfx`. They share almost all
of their source, and they are built by two different scripts:

```
cd game && ./build.sh && cp cnc_eyes ../playable/
cd app  && ./build.sh && cp cnc3d    ../playable/
```

**Rebuild BOTH after any change to a shared source file.** Every scripted gate
drives `cnc_eyes`, so a stale `cnc3d` is invisible to all of them: the change
verifies green, gets committed, and is simply not present in the build that gets
played. That happened twice -- once leaving `cnc3d` rejecting
every v13 pack, once shipping a cursor shadow and a cursor depth fix that were
absent from `PLAY.command`. Both were reported as regressions. Neither was one.

Gate **G30** (`game/gate_fresh.sh`) now fails if either deployed binary is older
than any source it is built from, and names the rebuild command for the one that
is stale.

## `TUNE-PRESENTATION.command` / `.bat`

The Tier 2 presentation chain with its F5 panel already open, on both platforms.
Every dial is live while the game plays; the SAVE button writes `cnc3d-fx.cfg`
into the build folder and that file is the deliverable, because its numbers
become `fx_defaults` in `game/fx_state.h`. Load one back with `--gfx <file>`.

The header comment inside the `.command` is the user manual for the panel, so
edit it there rather than describing the controls a second time here.
