# Building C&C 3D

This repository is source. It contains no game data, so a fresh clone will compile but
will not run until you supply two things you already own.

## 1. What you have to bring

| | Where it goes | Why |
|---|---|---|
| A dump of your **European** C&C Nintendo 64 cartridge | `data/rom/cnc_eu.z64` | All 3D: models, textures, terrain, the camera |
| Your 1995 **MS-DOS** Command & Conquer discs | `data/dosdata/` | All 2D and all audio: sidebar, sprites, music, speech, movies |

Each of those directories has a README saying exactly which files are expected and,
for the movies, how to reconstruct the set. Read those first.

The EU cartridge specifically: it carries the Covert Operations missions, and every
offset in `docs/re-findings.md` is quoted against it. A US dump will not line up.

## 2. Prerequisites

**macOS** — Xcode command line tools, SDL2, and Python 3 for the asset tools:

```
xcode-select --install
brew install sdl2
```

**Windows** — the shipped binary is 32-bit, built with mingw-w64 i686. The 32-bit choice
is deliberate: the Remaster DLL casts a pointer through `unsigned int`, which is lossless
at 32 bits and truncates at 64. The scripts below drive that toolchain from a Unix shell,
which is also how CI builds it.

```
tools/win/setup-toolchain.sh          # once: fetches the two SDKs mingw-w64 lacks, into ~/.cnc3d-winsdk
```

**Both** need Python 3 with `pillow` and `numpy` for the extraction and baking tools.

## 3. The brain

The game logic is EA's 2020 GPL release of Tiberian Dawn, unforked, with four additive
portability patches. It builds as a shared library the renderer loads at runtime.

```
tools/mac/build-brain-mac.sh          # -> TiberianDawn.dylib
tools/win/build-win.sh                # builds TiberianDawn.dll as part of the Windows build
```

`tools/check-brain-checkout.sh` verifies the checkout and the patches match byte for byte
before anything compiles.

The host looks for the library relative to the working directory. Override it with
`CNC_BRAIN=/path/to/TiberianDawn.dylib`.

## 4. Extracting and baking the data

With the cartridge in place, `tools/romdump/` decodes the archive and `tools/bakery/`
turns the result into the packs the renderer loads. **One step of that chain is missing
from this snapshot:** the PK4 pack baker is compiled bytecode whose source was lost and
is not published, so `tools/bakery/bake5.py` stops at its first import. See
`tools/bakery/README.md` for exactly what still runs and what does not. The `game/bake_*.py` scripts each own
one slice — sidebar, infantry, tiberium, campaign, effects, verdict screens.

These are the least documented part of the project and they were built to be run by
someone who already knew the pipeline. Start from `docs/re-findings.md` for the formats
and read the script you are about to run; each says at the top what it consumes and
produces.

Two environment variables are honoured throughout:

```
CNC3D_ROOT      the repository root, when a tool is run from outside the tree
CNC3D_ROM       the cartridge, when it is not at data/rom/cnc_eu.z64
```

## 5. The build

```
game/make-build.sh                    # macOS: renderer + app, into a playable folder
tools/win/make-build-win.sh           # Windows: a zip containing everything
tools/win/build-win.sh --no-brain     # skip the DLL, which changes rarely
```

`game/make-build.sh` takes the destination as its first argument and reads the baked data
from `CNC3D_DATA`. The Windows zip expands to `CNC3D-windows-<commit>/`, contains
`PLAY.bat`, and needs nothing installed on the target machine.

The SDL libraries are copied into the macOS build by `tools/bundle-sdl.sh`, which is
called with no file list on purpose so it scans the whole output folder. Skipping that
step produces a build that runs only on the machine that made it.

## 6. Two render tiers

Tier 2 is the modern desktop build — Windows and macOS — and may use shaders. Tier 1 is
the constrained one kept reachable as a stretch goal: Windows 98 on a 3dfx Voodoo 2
through Glide, fixed function, no shaders, no render targets, 640x480, 16-bit colour.

`docs/tier1-gap.md` is the contract between them. Any feature that cannot run on Tier 1
is listed there with its fallback. A Tier 2 feature that is not in that file is a bug,
because it turns a known list of compromises into a surprise.

The desktop presentation chain (bloom, ambient occlusion, sun shadows, colour grade, CRT)
is off unless asked for, so the default build draws the same picture on both tiers.

## 7. If it does not run

- **"Could not find the game"** — the executable resolves its own data directory relative
  to itself. It is looking for the baked packs, not the source tree.
- **Missing movies** — the campaign names 72 `.VQA` files and they are split across both
  1995 discs. See `data/dosdata/README.md`.
- **A tool cannot find the ROM** — set `CNC3D_ROM`. Nothing hardcodes a path any more, and
  anything that appears to is a bug worth reporting.
