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

**This program contains modified versions of EA's GPL source.** There are TWO
modified copies in this repository and both are covered here.

**1. `brain/vanilla/`, the engine the shipped game loads.** 33 files are modified:

    common/CMakeLists.txt             common/load.cpp
    tiberiandawn/CMakeLists.txt       tiberiandawn/aadata.cpp
    tiberiandawn/anim.h               tiberiandawn/bbdata.cpp
    tiberiandawn/bdata.cpp            tiberiandawn/building.cpp
    tiberiandawn/building.h           tiberiandawn/bullet.h
    tiberiandawn/const.cpp            tiberiandawn/defines.h
    tiberiandawn/dialog.cpp           tiberiandawn/display.cpp
    tiberiandawn/display.h            tiberiandawn/dllinterface.cpp
    tiberiandawn/dllinterface.h       tiberiandawn/drive.cpp
    tiberiandawn/externs.h            tiberiandawn/globals.cpp
    tiberiandawn/hdata.cpp            tiberiandawn/house.cpp
    tiberiandawn/house.h              tiberiandawn/idata.cpp
    tiberiandawn/ipxmgr.h             tiberiandawn/object.cpp
    tiberiandawn/object.h             tiberiandawn/saveload.cpp
    tiberiandawn/session.h            tiberiandawn/startup.cpp
    tiberiandawn/techno.cpp           tiberiandawn/techno.h
    tiberiandawn/udata.cpp

**All 33 now carry a dated modification block in the file itself** (1 Sep 2026),
and each one says what THAT file's change is rather than repeating a summary.
Before that date only 9 did, and those 9 all carried the sentence "the change
is additive and does not alter the game simulation", which is false for some of
them and is exactly the claim this notice had already retracted. They have been
corrected individually.

Every change is additive in the sense that nothing upstream was deleted, but it
is NOT true that none of them touch the simulation and an earlier version of
this notice said so wrongly: the changes add cheat-granted super weapons, widen
the playable house set to eight, and export state the renderer reads.

Read against the diff file by file, **17 of the 33 change what the engine
computes** and 16 do not. The 17 that do:

    tiberiandawn/CMakeLists.txt
    tiberiandawn/aadata.cpp
    tiberiandawn/bdata.cpp
    tiberiandawn/building.cpp
    tiberiandawn/defines.h
    tiberiandawn/display.cpp
    tiberiandawn/dllinterface.cpp
    tiberiandawn/drive.cpp
    tiberiandawn/globals.cpp
    tiberiandawn/hdata.cpp
    tiberiandawn/house.cpp
    tiberiandawn/idata.cpp
    tiberiandawn/object.cpp
    tiberiandawn/object.h
    tiberiandawn/techno.cpp
    tiberiandawn/techno.h
    tiberiandawn/udata.cpp

Those are the ones a reader checking whether this is still EA's game should
start with. The remaining 16 add build plumbing, read-only accessors,
exported state the renderer consumes, or portability shims.

**2. `brain/xl/`, a maintained fork of the same source** used for the large-map
mode. Its `common/` is asserted byte-identical to the unforked copy by
`tools/check-xl-common.sh`; its `tiberiandawn/` diverges in 52 files. The fork
is a second modified version of EA's program in its own right, and
`brain/xl/DIVERGENCE.md` records every divergence and why it exists.

The complete diff of the unforked copy against upstream is committed as
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
