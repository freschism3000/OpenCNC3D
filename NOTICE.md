# Notices

C&C 3D is a faithful PC recreation of the Nintendo 64 presentation of
Command & Conquer: Tiberian Dawn. It is assembled from parts with different
origins, and this file says which is which.

## Licence

Everything in this repository is distributed under the **GNU General Public
License, version 3** (`LICENSE`). That includes the renderer, the audio and
video layers, the menus, the sidebar, the reverse-engineering tools and the
build scripts, all of which are original work in this repository.

The renderer runs the game logic in the same process through the flat `CNC_*`
C ABI, so the running program is a single work built on GPL code. It is
licensed as a whole under the GPL accordingly.

## Third-party code: the game logic

`brain/vanilla/` is a checkout of the Tiberian Dawn game logic released by
Electronic Arts Inc. in 2020 under GPL version 3, with additional terms under
GPL Section 7. Those terms are not reproduced here in summary because a
summary is not the licence: read them in full in

    brain/vanilla/License.txt

which is EA's own file, carried unchanged. The per-file copyright headers
throughout `brain/vanilla/` are likewise unchanged.

Upstream is the Vanilla Conquer project. The exact commit this checkout
corresponds to is pinned in `brain/patches/UPSTREAM.txt`.

### Modifications to the GPL source

**This program contains modified versions of EA's GPL source.** The
modifications were made in August 2026, and they are these nine files:

    common/CMakeLists.txt          tiberiandawn/anim.h
    common/load.cpp                tiberiandawn/bbdata.cpp
    tiberiandawn/CMakeLists.txt    tiberiandawn/bullet.h
    tiberiandawn/dialog.cpp        tiberiandawn/startup.cpp
    tiberiandawn/dllinterface.cpp

Every change is additive: headless input, compiler flags, a compatibility
header for non-Windows builds, and read-only instrumentation the renderer
needs. None of them alter the game's simulation.

The complete diff against upstream is committed as
`brain/patches/vanilla-cnc3d.patch`, and `tools/check-brain-checkout.sh`
verifies that upstream plus that patch reproduces `brain/vanilla/` byte for
byte, so the record cannot silently drift from the code.

This is a modified version of EA's program and is not the original program.

## What is not in this repository

No game data is redistributed here: not the Nintendo 64 cartridge, not the
1995 MS-DOS disc content, and nothing extracted from either. Those are not
ours to distribute, and they are not covered by the GPL. Supply your own; see
`BUILDING.md` and the README in each empty data directory.

Command & Conquer, Tiberian Dawn, Westwood Studios and related marks belong to
their respective owners. This project is not affiliated with or endorsed by
Electronic Arts Inc.
