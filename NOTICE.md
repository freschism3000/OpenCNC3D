# Notices

OpenCNC3D is a faithful PC recreation of the Nintendo 64 presentation of
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

`brain/vanilla/` is a checkout of the Tiberian Dawn and Red Alert game logic
released by Electronic Arts Inc. in 2020 under GPL version 3, with additional
terms under GPL Section 7. Those terms are not reproduced here in summary
because a summary is not the licence: read them in full in

    brain/vanilla/License.txt

which is EA's own file, carried unchanged. The per-file copyright headers
throughout `brain/vanilla/` are likewise unchanged.

Upstream is the Vanilla Conquer project. The exact commit this checkout
corresponds to is pinned in `brain/patches/UPSTREAM.txt`.

### Modifications to the GPL source

**This program contains modified versions of EA's GPL source.** There are TWO
modified copies in this repository and both are covered here.

**1. `brain/vanilla/`, the source used by the standard brain builds.** The following 80 upstream files are modified:

    common/CMakeLists.txt
    common/load.cpp
    common/mixfile.h
    redalert/CMakeLists.txt
    redalert/aircraft.cpp
    redalert/anim.cpp
    redalert/building.cpp
    redalert/building.h
    redalert/bullet.cpp
    redalert/bullet.h
    redalert/display.cpp
    redalert/display.h
    redalert/dllinterface.cpp
    redalert/dllinterface.h
    redalert/event.cpp
    redalert/event.h
    redalert/externs.h
    redalert/factory.cpp
    redalert/factory.h
    redalert/foot.cpp
    redalert/globals.cpp
    redalert/house.cpp
    redalert/house.h
    redalert/idata.cpp
    redalert/infantry.cpp
    redalert/init.cpp
    redalert/logic.cpp
    redalert/map.cpp
    redalert/object.cpp
    redalert/object.h
    redalert/radar.cpp
    redalert/rules.cpp
    redalert/saveload.cpp
    redalert/startup.cpp
    redalert/techno.cpp
    redalert/unit.cpp
    tiberiandawn/CMakeLists.txt
    tiberiandawn/aadata.cpp
    tiberiandawn/aircraft.cpp
    tiberiandawn/anim.cpp
    tiberiandawn/anim.h
    tiberiandawn/bbdata.cpp
    tiberiandawn/bdata.cpp
    tiberiandawn/building.cpp
    tiberiandawn/building.h
    tiberiandawn/bullet.cpp
    tiberiandawn/bullet.h
    tiberiandawn/const.cpp
    tiberiandawn/defines.h
    tiberiandawn/dialog.cpp
    tiberiandawn/display.cpp
    tiberiandawn/display.h
    tiberiandawn/dllinterface.cpp
    tiberiandawn/dllinterface.h
    tiberiandawn/drive.cpp
    tiberiandawn/event.cpp
    tiberiandawn/event.h
    tiberiandawn/externs.h
    tiberiandawn/factory.cpp
    tiberiandawn/foot.cpp
    tiberiandawn/globals.cpp
    tiberiandawn/hdata.cpp
    tiberiandawn/house.cpp
    tiberiandawn/house.h
    tiberiandawn/idata.cpp
    tiberiandawn/infantry.cpp
    tiberiandawn/infantry.h
    tiberiandawn/ipxmgr.h
    tiberiandawn/map.cpp
    tiberiandawn/object.cpp
    tiberiandawn/object.h
    tiberiandawn/saveload.cpp
    tiberiandawn/scenario.cpp
    tiberiandawn/session.h
    tiberiandawn/special.h
    tiberiandawn/startup.cpp
    tiberiandawn/techno.cpp
    tiberiandawn/techno.h
    tiberiandawn/udata.cpp
    tiberiandawn/unit.cpp

Each modified upstream file carries a dated modification block describing its own
changes. Earlier blocks and dates preserve the history of the renderer exports,
portability, multiplayer and simulation work. The 19 October 2026 fair-skirmish
blocks identify per-house skill selection, ordinary economic/production rules,
computer exploration and observed-target filtering, and save/network state.
These changes DO affect multiplayer simulation. Campaigns retain their existing
AI and difficulty paths.

Project-authored additions within this tree are `common/wwkeyboard_null.cpp`,
`tiberiandawn/opencnc3d_compat.h`, `tiberiandawn/fair_ai_td.h`,
`tiberiandawn/fair_ai_td.inc`, `redalert/fair_ai_ra.h` and `redalert/fair_ai_ra.cpp`.
They are excluded from the modified-upstream count. `brain/fair_ai.h`, the host
probes in `brain/host/`, the synthetic fixtures in `brain/tests/ra3d/` and the
project's gate scripts are also project-authored. `RA3DBOOT.mpr` contains no
original Red Alert game data.

The source changes include both simulation changes and supporting exports/build
plumbing. Do not interpret an older per-file statement about a read-only export as
a claim that the program's complete diff leaves the simulation unchanged. In
particular, the skirmish controller replaces the original computer planner and
removes its privileged handicaps while respecting faction-specific rules.

**2. `brain/xl/`, a maintained fork of the same source** used for the large-map
mode. Its `common/` is asserted byte-identical to the unforked copy by
`tools/check-xl-common.sh`; its `tiberiandawn/` diverges in 52 files. The fork
is a second modified version of EA's program in its own right, and
`brain/xl/DIVERGENCE.md` records every divergence and why it exists.

The complete diff of the unforked copy against upstream is committed as
`brain/patches/vanilla-opencnc3d.patch`, and `tools/check-brain-checkout.sh`
verifies that upstream plus that patch reproduces `brain/vanilla/` byte for
byte, so the record cannot silently drift from the code.

This is a modified version of EA's program and is not the original program.

## Third-party art committed here

`data/mottle/mottle.pack` is built from a purchased DAZ Studio content set, Ground Decal
Materials, and holds fifteen ground patches the Enhanced renderer lays over plain terrain.
The art is not ours and is not the cartridge's; the licence for it is held.
`data/mottle/README.md` carries the provenance, the hashes and the terms.

`data/trees/tree3d.pack` is built from Poly Haven models, which are CC0. The baker that
builds it, `game/bake_tree3d.py`, records that and what it had to decode.

## What is not in this repository

No original or proprietary game data is redistributed here: not the Nintendo
64 cartridge, not the 1995 MS-DOS disc content, and nothing extracted from
either. The project-authored `RA3DBOOT.mpr` test fixture is synthetic. Original
game data is not ours to distribute and is not covered by the GPL. Supply your
own; see `BUILDING.md` and the README in each empty data directory.

Command & Conquer, Tiberian Dawn, Westwood Studios and related marks belong to
their respective owners. This project is not affiliated with or endorsed by
Electronic Arts Inc.
