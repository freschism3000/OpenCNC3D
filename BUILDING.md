# Building OpenCNC3D

This repository is source. It contains no game data, so a fresh clone will compile
but will not play until game data is available. Baking the packs yourself needs the
two things in the table. On Windows, macOS and Linux the launcher downloads the
published module packages instead, as section 5 describes.

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

**macOS**: Xcode command line tools, cmake and ninja, and Python 3 for the asset tools.
The Mac build is universal (Apple Silicon and Intel in one package, built on either), so
it links an SDL built for both rather than Homebrew's, which carries only the build
machine's CPU:

```
xcode-select --install
brew install cmake ninja
tools/mac/setup-sdl-mac.sh            # once: universal SDL3 + sdl2-compat, into ~/.opencnc3d-macsdk
```

The build scripts find that SDL by themselves. `OPENCNC3D_MAC_ARCHS=arm64` (or `x86_64`)
makes a quicker one-CPU development build; the play folder check then says it is not
universal, so it cannot be shipped by mistake.

**Windows**: the shipped binary is 32-bit, built with mingw-w64 i686. The 32-bit choice
is deliberate: the Remaster DLL casts a pointer through `unsigned int`, which is lossless
at 32 bits and truncates at 64. The scripts below drive that toolchain from a Unix shell,
which is also how CI builds it.

```
tools/win/setup-toolchain.sh          # once: fetches the two SDKs mingw-w64 lacks, into ~/.opencnc3d-winsdk
```

**Linux**: gcc, cmake, Python 3, and the SDL2, OpenGL, zlib and curl development
packages. The script checks for them and names the Debian/Ubuntu packages when one
is missing.

```
sudo apt install build-essential cmake git python3 \
    libsdl2-dev libgl1-mesa-dev zlib1g-dev libcurl4-openssl-dev
```

`git` is not part of `build-essential`, and the brain CMake stops without it.
`python3` is for the baking tools in section 4.

On Arch Linux the same build needs:

```
sudo pacman -S --needed base-devel cmake git sdl2-compat mesa zlib curl
```

`base-devel` supplies the compiler. `git` is separate, and the brain build needs it. `sdl2-compat` supplies `sdl2-config`. Arch packages include their headers, so there is no separate development package for Mesa, zlib or curl.

The Linux brain is 64-bit. The one place that stores a building type in a pointer
slot reads it back through `uintptr_t`, so the value is not cut down to 32 bits.
The site publishes no Linux program zip. The launcher installs the Windows module
packages for the game data and does not replace `opencnc3d` from the site.

**Both** need Python 3 with `pillow` and `numpy` for the extraction and baking tools.

## 3. The brain

The game logic is EA's 2020 GPL release of Tiberian Dawn, unforked, with four additive
portability patches. It builds as a shared library the renderer loads at runtime.

```
tools/mac/build-brain-mac.sh          # -> TiberianDawn.dylib
tools/win/build-win.sh                # builds TiberianDawn.dll as part of the Windows build
tools/linux/build-linux.sh            # -> TiberianDawn.so and RedAlert.so, beside the binaries
```

`tools/check-brain-checkout.sh` verifies the checkout and the patches match byte for byte
before anything compiles.

The host looks for the library relative to the working directory. Override it with
`CNC_BRAIN=/path/to/TiberianDawn.so`, `.dylib` or `.dll`.

## 4. Extracting and baking the data

With the cartridge in place, `tools/romdump/` decodes the archive and `tools/bakery/`
turns the result into the packs the renderer loads. **One step of that chain is missing
from this snapshot:** the PK4 pack baker is compiled bytecode whose source was lost and
is not published, so `tools/bakery/bake5.py` stops at its first import. See
`tools/bakery/README.md` for exactly what still runs and what does not. The `game/bake_*.py` scripts each own
one slice: sidebar, infantry, tiberium, campaign, effects, verdict screens.

These are the least documented part of the project and they were built to be run by
someone who already knew the pipeline. Start from `docs/re-findings.md` for the formats
and read the script you are about to run; each says at the top what it consumes and
produces.

Two environment variables are honoured throughout:

```
OPENCNC3D_ROOT      the repository root, when a tool is run from outside the tree
OPENCNC3D_ROM       the cartridge, when it is not at data/rom/cnc_eu.z64
```

## 5. The build

```
game/make-build.sh                    # macOS: renderer + app, into a playable folder
tools/win/make-build-win.sh           # Windows: a zip of the game, without the movies and music
tools/win/build-win.sh --no-brain     # skip the DLL, which changes rarely
tools/linux/build-linux.sh            # Linux: game, renderer, launcher, netcheck, both brains
tools/linux/build-linux.sh --no-brain # skip the .so files, which change rarely
```

On Linux the binaries land in `build/linux/`. Run `opencnc3d` or `opencnc3d-launcher`
from that directory. `TiberianDawn.so` and `RedAlert.so` sit beside `opencnc3d`.
Red Alert loads `RedAlert.so` from the working directory or from beside the binary.
On a hybrid with exactly one boot GPU and one other, the game moves rendering
to the discrete adapter when the boot GPU is Intel (Intel plus NVIDIA uses
NVIDIA PRIME; Intel plus AMD uses Mesa `DRI_PRIME=pci-…`), or when both GPUs
are AMD and the machine reports a battery. A desktop whose monitor is on the
graphics card is unchanged. Exported `DRI_PRIME` or NVIDIA PRIME variables win;
`DRI_PRIME=0` keeps the integrated GPU. Game data is the published Windows module. In this folder the launcher installs
it beside the binaries. The launcher does not download a program update: the site
has no Linux build of `opencnc3d`.

A Debian package of the same build is `dpkg-buildpackage -us -uc` from this tree,
then `apt install ../opencnc3d_*.deb`. The package file is written next to this
tree, not inside it. The programs land in `/usr/lib/opencnc3d`, with
links in `/usr/bin`, and a menu entry starts `opencnc3d-launcher`. The package does
not contain the game data. The launcher stores that in
`~/.local/share/Slipgate Ironworks/OpenCNC3D/`.

An Arch package of the same build is `cd archlinux && makepkg -si`. The package
file is written in `archlinux/`. The programs, the menu entry and the data folder
are the same as in the Debian package. The brain build needs `git`, which is
not part of `base-devel`.

`game/make-build.sh` takes the destination as its first argument and reads the baked data
from `OPENCNC3D_DATA`. The Windows zip expands to `OpenCNC3D-windows-<commit>/`, whose one
file to run is `OpenCNC3D.exe`, the launcher, and needs nothing installed on the target
machine.

The play folder `game/make-build.sh` makes keeps the 1995 movies and music, because the
gates that play a movie or measure the score run in it. The packages do not carry them:
`tools/mac/make-package-mac.sh` and the Windows packager leave out every path in
`tools/media/media-paths.txt`. A player gets them through the Windows installer or the
launcher's Setup screen, downloaded from disc images on archive.org: the movies from the
1995 Gold discs (EA's 2007 freeware release), the music from the Covert Operations disc
image. The game plays without them.

The SDL libraries are copied into the macOS build by `tools/bundle-sdl.sh`, which is
called with no file list on purpose so it scans the whole output folder. Skipping that
step produces a build that runs only on the machine that made it.

## 6. Two render tiers

Tier 2 is the modern desktop build, Windows, macOS and Linux, and may use shaders. Tier 1 is
the constrained one kept reachable as a stretch goal: Windows 98 on a 3dfx Voodoo 2
through Glide, fixed function, no shaders, no render targets, 640x480, 16-bit colour.

`docs/tier1-gap.md` is the contract between them. Any feature that cannot run on Tier 1
is listed there with its fallback. A Tier 2 feature that is not in that file is a bug,
because it turns a known list of compromises into a surprise.

The desktop presentation chain (bloom, ambient occlusion, sun shadows, colour grade, CRT)
is off unless asked for, so the default build draws the same picture on both tiers.

## 7. If it does not run

- **"Could not find the game"**: the executable resolves its own data directory relative
  to itself. It is looking for the baked packs, not the source tree.
- **Missing movies or music**: no package carries them. Tick them in Setup, or in the
  launcher's Settings (the gear button), and the launcher downloads them. In a development
  tree, the campaign names 72 `.VQA` files and they are split across both 1995 discs; see
  `data/dosdata/README.md`.
- **A tool cannot find the ROM**: set `OPENCNC3D_ROM`. Nothing hardcodes a path any more, and
  anything that appears to is a bug worth reporting.
