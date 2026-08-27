# C&C 3D

A faithful PC recreation of the Nintendo 64 presentation of Command & Conquer:
the GPL Tiberian Dawn game logic (the brain), the N64 cartridge's own 3D assets
(the skin), and a new renderer written for OpenGL 1.1 (the eyes).

It runs on **Windows and macOS**, and is developed and built on both. The renderer
is deliberately written to fixed-function OpenGL 1.1, which keeps a native
**Windows 98 / 3dfx Voodoo 2** build reachable as a stretch goal rather than a
requirement.

- `game/` the renderer and playable build (see `game/make-build.sh`)
- `brain/` four additive portability patches over Vanilla Conquer, plus the host
- `tools/` ROM extraction: overlay resolver, codecs, scene graphs, textures
- `sidebar/`, `menu/`, `video/` the 1995 MS-DOS UI layers, rebuilt from the original CD
- `docs/` design charter, RE findings, the recovered N64 camera, changelog

No game data lives in this repository. You supply your own N64 cartridge dump and your
own 1995 MS-DOS discs: see **[BUILDING.md](BUILDING.md)**, and the README in each of
`data/rom/`, `data/dosdata/` and `data/assets/`.

## This repository is a snapshot, not a history

Each release replaces this repository wholesale: a single commit carrying the tree as it
stood at that version. There is no development history here, and a new release does not
build on the previous one, so re-clone or hard-reset rather than pulling.

## Licence

**GPL version 3** ([LICENSE](LICENSE)), for everything in this repository.

The game logic in `brain/vanilla/` is EA's 2020 GPL release of Tiberian Dawn,
carried with its own licence file and per-file headers intact, and modified in nine
files; the renderer around it is original work. [NOTICE.md](NOTICE.md) sets out which
part is which, what was changed, and what is deliberately not redistributed here.
