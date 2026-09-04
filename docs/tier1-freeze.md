# The Tier 1 freeze: where the Windows 98 build stops following the main line

Written 1 Sep 2026 on branch `multiplayer`, at the director's instruction, on the day he
decided that the Enhanced brain (`brain/xl`) becomes the shipping brain by default
(`docs/design-multiplayer.md` section 11, decision 9).

This file exists to be a **dated marker in the source**, so that a year from now the question
"when did Win98 stop keeping up, and what did it have at the time" has an answer that was
written while the facts were still checkable, rather than reconstructed from memory.

Everything below was re-derived from the tree on the day it was written, and then checked a
second time by a reader whose job was to refute it. Where the second pass overturned the
first, the corrected version is what is recorded, and the correction is named.

---

## 0. The instruction, and the one correction it needs

The instruction was to record "the point in time of the source code where the Win98 version
cannot progress further". That is very nearly right, and it needs two corrections to be true.

**First, Win98 stopped following the main line five days before this decision.** On
**26 Aug 2026**, commit `56d7794` ("The editor makes 128x128 maps, and the engine's own .BIN
format taught us how") moved the desktop terrain pack format from `CNC3DPKE` version 14 to
`CNC3DPKF` version 15, which carries `u32 mapW, mapH` after the version for any grid that is
not 64x64 (`game/bake5.py:2459-2462`). Tier 1's own pack converter never followed:
`tools/win98/mkterrain.py:55` still reads

```
assert blob[7:8] == b"E", "only CNC3DPKE (v14) is understood, got %r" % blob[7:8]
```

so a modern pack dies at the magic byte. **That is the real, dateable divergence**, and it is
a better freeze point than the one this file was originally going to claim, because it is a
thing that happened rather than a thing that was decided.

**Second, "cannot progress" is stronger than the evidence supports.** See section 4. The
honest formulation of what changed today is narrower and more structural:

> **Today is the day Tier 1 stops being a LAGGING build and becomes a FROZEN one.**

`brain/xl/DIVERGENCE.md` records a deliberate rule that every structure the outer program
reads is kept byte identical between the two brains, so one build of the game can drive
either. That rule is what let Tier 1 hand mirror those structures (`tier1/w98_brain.c:114`,
`:115`, `:145`) and follow the main line without being ported to. The decision text at
`docs/design-multiplayer.md:668-672` withdraws that guarantee **today**, before any field has
actually widened. Tier 1 could previously catch up whenever someone had time. From here it
can only be carried forward deliberately.

---

## 1. The markers

| | |
|---|---|
| Version at the freeze | **0.6.4**, shipped 31 Aug 2026 at build commit `cd5cb00` |
| Branch this was written on | `multiplayer`, off `main` at `5f65d1a` |
| Last Tier 1 source change | `d7cb4d4`, **28 Aug 2026**, "The bridges stop leaking sea, and the globe loses a door to nowhere" |
| Real divergence began | `56d7794`, **26 Aug 2026**, the pack format move |
| `tier1/` size | **44 files, 12,550 lines** (24 `.c` at 10,420, 20 `.h` at 2,130) |
| Branch state | `win98-glide` is fully merged into `main`: 0 commits ahead, 515 behind |

**Nothing is stranded.** The freeze lands at a finished point, not in the middle of a piece
of work.

---

## 2. What Tier 1 has, at this point in time

Tier 1 is a separate program. `tools/win98/build.sh` builds it from `tier1/*.c` and **never
compiles `game/cnc_eyes.cpp`** (`grep -c cnc_eyes tools/win98/build.sh` returns 0). It targets
`i686-w64-mingw32` at `-march=pentium2 -mtune=pentium3`, with `-DWIN98
-D_WIN32_WINNT=0x0400 -DWINVER=0x0400`, and pins the PE subsystem to 4.0 because Windows 98
refuses to load a PE that claims a newer OS than itself.

It does not compile a brain. `tier1/w98_brain.c:239` hardcodes one filename,
`TiberianDawn.dll`, and `LoadLibraryA`s it at `:243`; the DLL is built separately by
`tools/win98/build-brain.sh:42`, which points cmake at **`brain/vanilla`** with `-DWIN9X=ON`.
There is no candidate list and no override.

**The play loop works.** The port's own status line, quoted rather than paraphrased:

> "The whole play loop works and looks right, and it now has most of the presentation depth
> too. Select, order, build, place, deploy, fight, win or lose, with sound, a title screen, a
> briefing movie, animated models, section-by-section construction, the cartridge's own
> fourteen 3D cursors, shadows and a real particle simulation."

Measured on the 534 MHz Pentium III with a Voodoo 2: 65 FPS at 640x480 with everything on.
The software rasteriser fallback measured 12.0 FPS at 640x400, which is why the Voodoo path
is the real one.

---

## 3. What Tier 1 does not have, sorted honestly

The temptation in a freeze note is to write a long list of losses. Most of it would be wrong.
**Almost nothing on the list below was ever taken away.** These are three different
categories and conflating them is how a record like this ages badly.

### 3a. Already declared gone, before today

**Network multiplayer.** Declared Tier 2 only in `docs/design-multiplayer.md` section 8 and
`docs/tier1-gap.md:20`, and the menu already refuses the click (`menu/dosmenu.c:62`,
`tier1/t1_menu.c:103`). The declaration was made on 30 Aug 2026 at 12:17 UTC and the first
`net/` file landed at 21:10 the same day, so it preceded the netcode **by nine hours**. It
did precede it. It is not a long standing precedent, and the record should not imply one.

The reasoning stands on its own and is not about Winsock: Win98 SE has Winsock 2.2 and plain
UDP is enough. It is that Tier 1 is a separate program, so parity means writing the lobby,
the turn scheduler and the transport a second time, and that lockstep runs at the slowest
peer.

### 3b. Never built, and never a loss

Four menu items are enabled in the shared menu module and have no Tier 1 handler at all.
Tier 1 dispatches exactly `DM_EXIT` (`tier1/w98_glidegame.c:1771`), `DM_TESTMAP`/`DM_START`
(`:1772`) and `DM_INTRO` (`:1799`). Live on Tier 2 and dead on Tier 1:
**`DM_SPECIAL`, `DM_USERMAPS`, `DM_SKIRMISH`, `DM_VISUALS`** (Tier 2 handles all four at
`app/cnc3d.cpp:1647`, `:1687`, `:1720`, `:1797`).

Also absent: the options dialog, the campaign shell, the score tally, and the eighteen
separate vehicle shadow quads.

**A defect found while writing this**, worth its own line because no compiler will ever point
at it: the comment at `tier1/w98_glidegame.c:1766-1770` claims "Load, Multiplayer and Intro
are drawn DISABLED by the module itself and cannot be picked". Intro is enabled at
`menu/dosmenu.c:63` and handled at `:1799`. The comment is wrong about its own invariant.

### 3c. What genuinely stops here

**One thing, and it is structural rather than a feature:** the byte identical ABI guarantee
described in section 0. Everything else in this section follows from it rather than standing
beside it.

When the roster does grow, Tier 1's hand written mirrors will not silently rot. Two
deliberate 64 to 32 bit narrowings in the Enhanced brain each carry
`static_assert(HOUSE_COUNT <= 32)` (`brain/xl/DIVERGENCE.md:191-198`), so growing the roster
past 32 stops the build rather than dropping the alliances and selection of every house above
the 32nd. That is a better failure mode than the one to fear, and it is worth knowing that it
is there.

### 3d. Big maps: does not, rather than cannot

Tier 1 draws a 64x64 grid, hardwired in its own literals: `tier1/w98_glidegame.c:261`
(`static unsigned char g_vis[64 * 64]`), `tier1/t1_terrain.h:29`, and a hardcoded stride in
the draw loop at `tier1/t1_terrain.c:197` (`rec = t->cells + (long)(cz * 64 + cx) * 4`). The
desktop side has made 128x128 maps since 26 Aug.

**This is a compile time literal, not a property of the box or the card.** At a 128 stride the
added storage is roughly 66 KB of BSS against about 388 MB free on the test box, the terrain
atlas does not grow with cell count because it is a bank of distinct tiles rather than of
cells, and the view cull already means only visible cells are submitted. `t1_terrain.h:29`
`ncells` is already a runtime field read from the pack.

So: **Tier 1 does not today draw past 64x64. It is not true that it cannot.**

---

## 4. Decision, not physics: the part most likely to be misquoted

The design has said in several places that the Enhanced brain never ships to Windows 98. On
the day that became load bearing, it was checked properly for the first time, and **the
technical case is markedly softer than the design claims.**

| Checked | Result |
|---|---|
| `brain/xl/common` vs `brain/vanilla/common` | **byte identical**, `diff -rq` clean |
| The Win98 switch in the fork | intact, `brain/xl/common/CMakeLists.txt:194-195`, `if(WIN9X)` / `_WIN32_WINNT=0x400` |
| C++ standard | same in both, `CMAKE_CXX_STANDARD 11` |
| Modern runtime headers in XL | none: no `std::thread`, `std::chrono`, `<atomic>`, `<mutex>` |
| `MAX_HOUSES` in the XL ABI | still **32** (`brain/xl/tiberiandawn/dllinterface.h:170`) |
| `MAX_EXPORT_CELLS` | still **128\*128** (`:31`) |
| XL peak RSS, measured | about **101 MB** against about **388 MB** free on the box |
| Files diverged in `brain/xl/tiberiandawn` | **52** |

**Nobody has ever tried.** `tools/mac/build-brain-xl-mac.sh` is the only XL build script in
the tree, CI has never built `brain/xl` at all, and the XL top level does not declare
`option(WIN9X ...)` the way vanilla does at `brain/vanilla/CMakeLists.txt:29`, so the switch
would have to arrive as a cache entry. One command would convert this whole section from
inference into fact:

```
cmake -S brain/xl -DCMAKE_TOOLCHAIN_FILE=tools/win98/toolchain-win98.cmake -DWIN9X=ON
```

Until somebody runs it, **the correct thing to say is that Enhanced on Win98 is unbuilt and
undecided, not impossible.** The freeze is a consequence of a product decision about where
effort goes. It is not a wall the hardware put there.

**And the asymmetry is not what it looks like.** On the day Enhanced is promoted, **neither
tier can load it**. `game/cnc_eyes.cpp:16576-16603` is the whole of Tier 2's `find_brain`, and
its candidate table holds three paths, all of them vanilla, one naming
`brain/vanilla/build-native` explicitly. There is no `game/brain_xl.cpp`, no `BrainVTable`,
and no INI driven brain pick anywhere in `game/`. Tier 2's brain selection seam is unbuilt
too. Tier 1 is not behind here; both tiers are.

---

## 5. What is owed, as of today

**A `docs/tier1-gap.md` row for the Enhanced brain, and it does not exist.** That file's own
rule at `:7-9` requires every Tier 2 only feature to be declared in it before it ships.
Searching it for the Enhanced brain returns exactly one line, `:20`, where "Enhanced never
ships to Windows 98" survives only as a subordinate clause inside the network multiplayer
row. By the file's own rule, **promoting Enhanced to the shipping brain is an undeclared
Tier 2 only feature.**

That row is not written here because this branch is not where that file is being edited, and
writing it from here would land as a merge conflict rather than as a declaration. **It is
owed by the next change to that file**, and it should say what section 4 says: unbuilt and
undecided,
with the one command that would settle it.

Two smaller things also owed, both found while writing this and neither belonging to this
branch:

- `tools/win98/mkterrain.py` does not understand `CNC3DPKF` v15, so Tier 1 cannot consume any
  pack baked since 26 Aug 2026.
- The wrong invariant comment at `tier1/w98_glidegame.c:1766-1770`.

---

## 6. What would unfreeze it

Recorded so that "frozen" is understood as a state with an exit, rather than a verdict:

1. **Run the cmake line in section 4.** Half an hour, and it either produces a 32 bit
   Enhanced DLL or a specific error. Everything else is downstream of that answer.
2. **Teach `mkterrain.py` the v15 pack header.** Tier 1 then sees modern content again.
3. **Lift the 64 stride** in the four Tier 1 literals in section 3d. About 66 KB of BSS.
4. **Give Tier 2 a brain selection seam**, which it also lacks, and let Tier 1 use the same
   one rather than inventing a second.

None of that is planned, and none of it is on the multiplayer critical path. It is written
down so that the decision to leave it undone stays a decision.

---

## 7. Corrections this file makes to earlier records

Kept because each was believed by somebody competent and will be believed again:

- `docs/win98-port.md:33` sizes the parked Tier 2 renderer seam at "12,646 lines". That was
  true on 19 Aug 2026. `game/cnc_eyes.cpp` is **26,216 lines** today, so the parked work is
  roughly twice as expensive as the record says.
- `docs/design-xl-brain.md:74` describes a length prefixed pack format as future work. It
  shipped on 26 Aug 2026 as `CNC3DPKF` v15.
- `docs/tier1-gap.md:40` still lists VI output gamma as Tier 1's one real loss with its Glide
  answer "not written". Nothing in `tier1/` implements it: there is no `grGammaCorrectionValue`
  anywhere and no occurrence of "gamma" in any `tier1/*.c`. It is unbuilt on both sides.
- `docs/tier1-gap.md:46` treats bilinear filtering as an addition Tier 1 could make. It is
  already built: `tier1/t1_glide.c:56` sets it at open and `:135-137` is a runtime toggle.
- The Enhanced contract expects the stock edge follower pathfinder to fail on long treks.
  The Phase 2 measurement contradicts it: two players 800 cells apart on a 1024 map, 20,000
  ticks in 133 seconds, and the pathfinder walked the 800 cells. Record it as an unreproduced
  expectation, not a measured failure.
- Tier 1 gets `MAX_PLAYERS 8` by CMake default rather than by declaration:
  `brain/vanilla/tiberiandawn/defines.h:2591-2595` is `#ifdef EIGHTPLAYERS / 8 / #else / 6`,
  the option defaults `ON` at `brain/vanilla/tiberiandawn/CMakeLists.txt:183`, and
  `tools/win98/build-brain.sh:42-49` passes no such flag.
