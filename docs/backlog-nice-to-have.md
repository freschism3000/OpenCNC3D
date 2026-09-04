# The Nice To Have backlog

**These are not refused, and they are not scheduled.** They are recorded
here so the board stops carrying them as live work and so the next person who has the idea
finds the reasoning already done rather than rediscovering it.

Nothing in this file is a defect. Everything in it either asks for something the cartridge
never contained, or asks the game to behave like a modern RTS rather than like the console.
Where a request had a buildable half, that half was built and shipped and only the remainder
is written down here.

**The rule these were measured against**: *presentation may deviate from
the console, the simulation may not.* Two requests that looked like this class were pulled
OUT of it on that rule and are being built instead: **ultrawide support** and the
**PC-style health bars**. Greater zoom range was already shipped.

Where a report is named, it has been archived off the board as `wont_fix` with a note
pointing here. Un-archiving is one API call if any of these is ever taken up.

---

## 1. Art the cartridge does not contain

The five of these are one decision, not five. Every mesh and texture the game draws comes
out of a baked container that `load_pack` reads wholesale. There is no override directory,
no user asset path, no material layer, and one UV pair per vertex. Nothing here is a wire
that was forgotten; each is a mechanism that does not exist.

| Report | What it asks | What it would take |
|---|---|---|
| `FR-20260814-6EC321` | The SSM Launcher and the Visceroid, which have no cartridge mesh | Authoring art the console never held. **Note the other two names in that report needed nothing**: Mobile HQ already draws, because the cartridge itself aliases MHQ to the APC's mesh, and the Oil Tanker is a deliberate registered absence, since ARCO is not in the cartridge's structure array at all. |
| `FR-20260814-DCB3C0` | Texture packs and HD replacement textures | An asset override path, which is entirely new mechanism. The zip export half of that request already exists: the model gallery's EXPORT ALL writes every model with its textures. |
| `FR-20260822-E4E51C` | Infantry as 3D models instead of sprites | Infantry are sprites because **the cartridge has no infantry meshes at all**. This is authoring a whole army from nothing. |
| `FR-20260822-37376A` | A second UV channel for unique unwraps and AO bakes | Three changes: the pack format, the baker, and a multitexture draw path. Only worth any of it once authored art is allowed at all. |
| `FR-20260825-857635` | Parallax occlusion mapping on terrain and decals | A per-pixel material technique, and this renderer draws the world in fixed-function immediate-mode GL. There is no material layer to put it in. |

**What unblocks all five:** a decision that art which is not the cartridge's may enter the
game. Until then the answer to each is the same, which is why they are one entry.

## 2. Modern-RTS conventions the console never had

| Report | What it asks | Where it stands |
|---|---|---|
| `FR-20260814-004655` | A radial command menu, and gamepad support | Neither exists. SDL is initialised for video and audio only; there is no gamepad code anywhere in `game/`, `app/`, `menu/`, `tier1/` or `launcher/`. **A trap for whoever picks this up:** upstream Vanilla Conquer's own gamepad code is in the tree under `brain/vanilla/common/` and is NOT COMPILED, because every brain build passes `-DSDL2=OFF` and builds headless against the `CNC_*` API. It is dead code, not a head start. A radial menu was also drawn once as one of five editor layout mockups, and a different one was picked. |
| `FR-20260823-C84DF9` | Remaster quality-of-life: sidebar category tabs, and a unit queue | **The queue shipped in v0.6.3.** The tabs did not. The console's own answer to the same problem is its five category rows, which belong to the parked N64 sidebar work, so building PC-style tabs would be choosing the Remaster's shape over the cartridge's. That is the decision, and it is not a missing wire. |

---

## What is deliberately NOT in this file

These were considered and left on the board or in `missing.md` instead, because each is a
live question rather than a deferred feature:

- **The N64 sidebar** (`docs/design-n64-sidebar.md`). Decoded in full and parked
  pending a design decision. Its own decision, and several of the entries above wait on it.
- **A Linux port and console ports** (`FR-20260822-0462FB`, `FR-20260824-EC4C85`). A third
  build target is a project question, not a feature request.
- **Dune 2000 PS1** (`FR-20260822-BF3113`). A different game.
- **ROM regions and a German dub** (`FR-20260827-EE5DC2`). Players supply no ROM today; the
  cartridge is a build-time bakery input only.
- **Macro terrain textures** (`FR-20260823-CEAF99`) and **VFX editing** (`FR-20260822-7F139C`).
  Both are decoded capability that exists only at build time, which is a different shape of
  problem from "art that does not exist".
