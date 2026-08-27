# The Windows 98 port (Tier 1)

Developed on branch `win98-glide`.

Read the repository conventions first. This file is the authority on the Win98
port specifically: what it owns, what it must not touch, what has actually been proven on
real hardware, and what is owed.

**The branch name says glide and the first work is software rendering. That is deliberate,
the decision: prove the whole path with a CPU rasteriser first, because that
removes the 3dfx driver from every early failure, it can be developed and gated on the Mac
before it ever reaches the box, and unlike a Voodoo it is visible over VNC.** Glide remains
the destination, not the starting point.

---

## 1. What this branch owns, and what it must not touch

This branch is developed alongside two others. `win11-build`
is the modern Windows port and `sidebar-hud-640` is the
HUD rebuild.

**Owns (new files, nobody else is in them):**

```
tier1/                  the Win98 platform layer and the software rasteriser
tools/win98/            the Win98 toolchain and deploy scripts
docs/win98-port.md      this file
```

**Must NOT touch, for now:**

- `game/cnc_eyes.cpp`. It is 12,646 lines, it holds about 523 of the project's GL call
  sites, both other branches are working in it, and it is already recorded
  it as the file that produces merge conflicts. The Win98 port's eventual job is to pull
  those GL calls behind a backend, which is the single most conflict-generating change
  possible in this repo. That refactor is scheduled work on `main`, agreed with the other
  branches, not something this branch does quietly. See section 6.
- The GL headers `cnc_sidebar.h`, `dosinf_mod.h`, `verdict_mod.h`, `effects_mod.h`,
  `cursors.h`, `shroud_mod.h`, `smudge_mod.h`, `dostib_mod.h`, `cursor3d_mod.h`,
  `dosmake_mod.h`, `dosopt_gl.h`, `fx_*.h`, and `video/movieplay.*`, for the same reason.
- `brain/vanilla`, which is someone else's GPL project. Changes live in `brain/patches/`.

**May READ and COMPILE freely.** `game/dosbar.c` is compiled into the Win98 prototype
unedited and that is the point: the prototype draws the real sidebar with the real code,
not a lookalike. Reuse is not a conflict; editing is.

---

## 2. THE TOOLCHAIN. This is the part that would have cost a week.

Windows 98 has no UCRT. Homebrew's mingw-w64 is built **UCRT-only**, and the trap is that
it does not look like it: `__MSVCRT__` is defined, `-mcrtdll=msvcrt` is accepted and
changes nothing, and `libmsvcrt.a` exists but is a **relabelled UCRT import library** whose
members are literally named `lib32_libucrt_*`. An ordinary link therefore produces a binary
that imports `api-ms-win-crt-*.dll`, and Windows 98 says so in a dialog box, measured on
the real machine on 19 Aug 2026:

> A required .DLL file, API-MS-WIN-CRT-CONVERT-L1-1-0.DLL, was not found.

**No new toolchain is needed.** mingw-w64 also ships `libmsvcrt-os.a`, which is the genuine
`msvcrt.dll` import library. Discard the default library set and name every library
yourself:

```
-nodefaultlibs
-Wl,--start-group -l:libstdc++.a -lmingw32 -lmingwex -lmsvcrt-os \
                  -lgcc -lgcc_eh -l:libwinpthread.a -Wl,--end-group
-lkernel32 -luser32 -lgdi32 -lwinmm
```

The **group is required**, not decoration: this GCC uses the posix threading model, so
`libgcc_eh` drags in winpthreads, which calls back into the CRT for `snprintf`, `_setjmp3`,
`longjmp` and `_endthreadex`. Without the group the link fails on a library cycle. Drop
`libstdc++.a` and `libwinpthread.a` for a pure C target.

`tools/win98/build.sh` encodes all of this and **refuses to finish if the binary imports a
DLL Windows 98 does not have**, so the mistake cannot come back silently.

### C++ is fine on Windows 98, and that was not obvious

A C++14 test program built this way was run on the box and every check passed:

```
static_ctor=5eed OK                     static initialisers run
stl vector=200 map=200 first=item000 OK std::vector, std::map, std::string, std::sort
bigalloc 8MB=OK                         an 8 MB new[] succeeds
exceptions=OK                           try / throw / catch, SJLJ unwinding
```

That matters because `cnc_eyes.cpp` is C++14 and the whole port would have been a different
project if the standard library had been off the table. It is not.

---

## 3. The box, measured rather than assumed

| | |
|---|---|
| OS | Windows 98 SE, 4.10.2222 (`GetVersionExA`: 4.10 build 2222, platform 1) |
| CPU | GenuineIntel family 6 model 8 stepping 3, one core. Pentium III Coppermine. **534 MHz measured** by counting RDTSC cycles across a QueryPerformanceCounter interval |
| RAM | **511 MB total, about 388 MB free.** Memory is not a constraint on this machine |
| Disk | about 52 GB free on C: |
| Desktop | 1280x1024 at 32 bit |
| `QueryPerformanceFrequency` | 1193180 Hz, the PIT. QPC works; **`GetTickCount` advances only about 18 times a second and must never be used to animate from** |
| `msvcrt.dll` | present, 6.0.8397.0 |
| `opengl32.dll` | **present, 4.0.1381.4.** This is Microsoft's software OpenGL 1.1. `LoadLibraryA` succeeds and `glBegin` resolves. See section 6, it is an option nobody had costed |
| `glide2x.dll` | present, 2.56.0.2105 |
| `glide3x.dll` | present, 3.3.0.670 |
| `ddraw.dll` / `d3dim.dll` | present (DirectX 8.1a era) |
| 3D hardware | 2x 3dfx Voodoo 2 in SLI |

---

## 4. Build, deploy, look at it

```
tools/win98/build.sh                    -> build/win98/w98proto.exe
```

Deploying is not a copy, it is a documented obstacle course, and every step below exists
because the obvious version failed:

1. **Binaries go over SMB, never over the agent daemon.** The daemon pushes text only:
   AutoIt strings cannot hold NUL bytes, so a pushed binary silently truncates.
2. **Drop into a SINGLE-level share folder**, `\\192.168.1.117\share\cnc98\`. A two-level
   UNC subfolder (`\share\CNC3D\win98\`) made COMMAND.COM's `copy` report success and copy
   nothing.
3. **The box copies the file to `C:` and runs the local copy.** Running an exe off the
   share locks it against the next rebuild.
4. **NEVER `RUNWAIT` a COMMAND.COM job.** Windows 98 leaves the DOS box open as
   "Finished - <name>", so `RunWait` never returns, and it **wedges the single-threaded
   daemon**, after which port 9855 stops answering entirely. Recovery needs VNC to close
   that window by hand. This was rediscovered the hard way on 19 Aug. Use `RUN` and poll
   `FILESIZE`/`EXISTS`, or `RUNWAIT` a real exe such as `AutoIt3.exe`, which is safe.
5. **Copying a file from `C:` back to the share fails under COMMAND.COM** ("0 file(s)
   copied", no error). AutoIt's `FileCopy` handles the same path fine, so results come back
   through a small pushed `.au3`.
6. **A program must find its data next to its own exe**, via `GetModuleFileNameA`, not via
   the current directory. Nothing that launches a program on this box sets a sensible
   working directory.

**Looking at the picture.** `w98proto.exe` writes `w98proto.bmp` (bottom-up 24 bit, because
a top-down BMP file crashes Win9x `LoadImage`) and `w98proto.txt` with its measurements, so
a frame and its numbers can be read on the Mac without trusting a screenshot. That matters
beyond convenience: **the VNC screenshot of a running frame came back BLACK while the BMP
of the same frame was correct**, so on this box VNC is not evidence about what the program
drew. Use the BMP.

---

## 5. What has actually been proven, as of 19 Aug 2026

`tier1/w98_proto.c` runs on the box and draws, at 640x400, entirely on the CPU:

- the real 1995 MS-DOS sidebar, drawn by `game/dosbar.c` **compiled unedited**, from the
  project's own `dossidebar.pack`: cameos, REPAIR/SELL/MAP buttons, the power bar, the
  radar plate, the ready overlay, the scroll arrows, the credits tab
- text in the pack's own 6POINT DOS font
- a perspective, z-buffered, perspective-correct textured 3D scene (a ground grid and a
  rotating cube) textured with real cameo art, lit through a 32-level palette shade table

**Measured: 17.86 FPS**, 134,071 drawn pixels per frame, 60.2 cycles per drawn pixel, at
640x400 on a 535 MHz Pentium III with the whole frame accounted for (55.82 ms of 55.99).

It did not start there. The first run reported 3.56 FPS and two things were wrong, both now
understood and written up as open questions:

- **VNC was costing 4.6x.** TightVNC hooks GDI and this program hands GDI a 640x400 32-bit
  `BitBlt` every frame. Never benchmark this box with VNC attached.
- **x87 float-to-int casts were costing about 145 cycles per pixel.** Six `fldcw`
  instructions in the per-pixel body, from three `(int)float` casts. The inner loop is now
  fixed point and contains no floating point at all: raster went 45.02 ms to 15.09 ms.

What is left is mostly not the rasteriser. Clearing costs 20.50 ms and is **memory bound**:
this box writes 119 MB/s (measured, and the DIB section is no slower than ordinary heap),
so 2 MB of clears is 16.8 ms and the only cure is writing fewer bytes. `BitBlt` costs
15.65 ms through GDI. Both have known answers (a 16-bit buffer, DirectDraw) and neither has
been taken yet.

---

## 5b. The brain runs on Windows 98, and it is the same brain (19 Aug 2026)

`tools/win98/build-brain.sh` builds EA's GPL Tiberian Dawn logic as a 32-bit Windows 98
DLL from the same committed source and the same cmake targets the Mac and modern Windows
builds use. `brain/host/cnc_host.c`, the dependency-free headless host, is compiled
unedited: its Win32 `LoadLibrary` branch was written in advance "so the same file builds
for the Win98 target", and it did.

On the box, loading `c:\CNC98\TiberianDawn.dll` and starting GDI mission 1 (`SCG01EA`,
extracted from the N64 cartridge):

```
  handle: 62d00000
  resolved 10/10 symbols (0 missing)
  CNC_Version(0) = 258
  templated cells: 700 of 700 scanned
  advanced 60 tick(s)
  [event] SOUND_EFFECT  sfx=68 var=1 name='TNKFIRE6' at px(1176,1320) prio=1 ctx=1
```

**The simulation is byte identical to the Mac.** Sixty ticks of `SCG01EA` on macOS and on
Windows 98 produce the same eight engine events, in the same order, with the same sound
ids and the same world coordinates:

```
mac events: 8   win98 events: 8
IDENTICAL: every event matches byte for byte across macOS and Windows 98
```

### The one thing that made it work, and it needed no patch

Vanilla Conquer already has first-class Windows 95/98/ME support (`CMakeLists.txt:29`,
`option(WIN9X ...)`) and nobody had switched it on. Without it,
`common/CMakeLists.txt:191` compiles the common library with `UNICODE _UNICODE
_WIN32_WINNT=0x501`, which makes `rawfile.cpp`'s `raw_fopen` expand to `_tfopen` and
therefore `_wfopen`. **Windows 98 stubs the wide entry points**, so the brain loaded,
initialised, resolved every symbol, reported its version, and then could not open a single
file:

```
[event] DEBUG_PRINT   Failed to load scenario file
```

`-DWIN9X=ON` gives `_WIN32_WINNT=0x400` and plain ANSI `fopen`. Because it is upstream's
own switch it costs no patch to `brain/vanilla`, which the repository conventions keeps our hands out of.

### `msvcrt.dll` does not understand `%zu`, and it fails quietly

The host's diagnostic line printed as `object=zu list_hdr=zu map=zu`, because `msvcrt.dll`
predates C99 and prints the letters instead of the number. It matters more than it looks:
that line is exactly the diagnostic you need when the engine then reports zero objects, and
it was unreadable at the moment it was needed. `-D__USE_MINGW_ANSI_STDIO=1` routes `printf`
through mingw's own implementation in `libmingwex` and fixes it. **Any project code with a
`%zu`, `%lld` or `%a` in a format string is silently wrong on Win98 without that flag**, so
it is now in `tools/win98/build.sh` for every Win98 target.

## 5c. The mission runs, live, on the Win98 screen (19 Aug 2026)

`tier1/w98_game.c` is the game rather than the prototype. It loads the brain, starts
`SCG01EA`, ticks it at the engine's native 15 Hz independently of the frame rate, reads the
live object list every frame, and draws it under the project's own DOS sidebar.

Measured on the box: **615 ticks, 68 objects, 18.0 FPS**, map 28x25 cells at 13.6 pixels
per cell. On screen are Nod infantry at their exact lepton positions with their real
facings, three Nod gun turrets carrying damage bars because the GDI gunboat is shelling
them, the MCV, and the tree line, with the 1995 sidebar composited over the top.

**What it does not draw is the N64 3D art.** The terrain tiles and the models live in the
scenario packs and reading those is the next piece of work. Until then the tactical view is
an honest top-down readout of live engine state, not a picture of the game. That
distinction is worth keeping: everything on that screen came out of the simulation on that
machine on that frame, and nothing is staged.

### The trap that cost the most here, and it is a good one to know

The console test found 60 objects and the windowed game found zero, from the same source
file. **A `-mwindows` program has no standard handles at all.** The brain reports its
object list by calling `printf`, the capture duplicates a file descriptor around the call,
and in the GUI build there was no descriptor to duplicate. Two things were needed:

1. `wb_ensure_stdout()` freopens `stdout` onto a file when `_fileno(stdout)` is negative.
2. The capture dups **`_fileno(stdout)`, not the literal `1`**. In a console build they are
   the same number; after a freopen in a GUI process they are not, and duping `1` fails
   silently.

The diagnostic that settled it was looking at the scratch files on the box rather than
reasoning about the code: `objdump.tmp` was full and `brainout.tmp` was empty, which
immediately said the full one was stale from the console run and the GUI build had never
written anything at all.

## 5d. The cartridge's own terrain, on Windows 98 (19 Aug 2026)

`tier1/t1_terrain.c` draws the real heightfield: the scenario's own 65x65 corner map, its
own 24x24 atlas tiles, its own per-corner lighting, under the N64 camera recovered from
the ROM. `tier1/t1_cam.h` is a plain C mirror of `cnc_eyes.cpp`'s CAM_N64 with every
constant cited back to its ROM offset.

The camera checks out against the ROM rather than against my arithmetic: the status line
reads `D3000 P48.1` and the cartridge's pitch at distance 3000 is 48.1285 degrees.

Measured on the box: **12.0 FPS, 254,541 pixels per frame** at 640x400, with 64 live
objects. It started at 9.4 and three measured changes got it here, none of them guesses:

| ms per frame | first run | now | what changed |
|---|---|---|---|
| terrain | 39.62 | 36.00 | edge slopes computed once per triangle instead of three divides per scanline, and the terrain emitted in the winding that is already front facing so `t1_tri` stops paying for a flipped retry on every triangle |
| object dump | 11.91 | 4.04 | the brain reports by `printf`, so each call formats about 20 KB, writes it and parses it back. Capped at 5 Hz, which is under the engine's own 15 Hz and imperceptible at infantry speed |
| clear | 14.01 | 6.80 | **the colour buffer is no longer cleared at all.** The terrain covers every pixel of the view, so the clear was a megabyte written and immediately overwritten, and this machine writes 119 MB/s |
| present | 15.79 | 15.35 | unchanged; this is GDI's `BitBlt` and wants DirectDraw |
| **total** | **9.39 FPS** | **11.99 FPS** | |

Not clearing colour is a real constraint, not a free win: the scroll is clamped to the
playable rect with a margin so the camera can never see past the drawn terrain patch.
Widen that clamp and the background becomes last frame's leftovers. If the map edge ever
has to be visible, the answer is a sky pass and not putting the clear back.

### The three traps, all of which look almost right

The recon pass flagged these before a line was written and all three were real:

1. **`sr_triangle` backface culls; `cnc_eyes.cpp` never enables culling anywhere.** The
   baker copies the ROM display list's vertex order through without normalising it, so
   the pack's winding is arbitrary. The terrain triangles come out `det = -1398` under
   this camera, so a direct call would have discarded **one hundred percent of the map**
   and drawn black. `t1_tri` flips and retries instead of rejecting.
2. **`SR_Vertex.w` is the positive eye DEPTH, not eye z.** World +Z runs south, toward
   the viewer. Writing `w = ze` gives a north/south mirrored world that looks plausible.
3. **The depth buffer takes w in CELLS, never leptons.** In cells 1/w as 12.20 spans
   about 47,000 to 127,000 counts over the visible range; in leptons it is 0.116 counts
   per lepton, which quantises to zero and collapses the buffer entirely.

### One palette per bank, not one per program

`tools/win98/mkterrain.py` converts a scenario pack into a `.t1terr` file: the atlas
palettised to 255 colours plus a reserved transparent index, the cell table, the
heightfield and the CM tint. Measured on the real file: 305 distinct opaque colours in
the used area and **0.670% of texels snapped to a neighbour**.

The reason a per-bank palette is right rather than one shared palette for everything is
measured too: quantising the terrain atlas alone gives a mean error of 0.179 on a 0..441
scale, while one palette shared across the whole texture bank gives 2.957, sixteen times
worse. It costs nothing, because the framebuffer is 32-bit and `sr_use_shade` is a single
pointer store: a palette is a property of a texture BANK, not of the program, and swapping
between the terrain pass and a later object pass is one store and 32 KB per extra table.

**The bug this caused, for the record.** The first run drew a perfectly correct heightfield
in magenta confetti, because the shade table was still being built from the DOS sidebar's
palette while the atlas indices were the terrain's. The 2D blit takes its palette as an
argument and so was unaffected, which is exactly why the geometry looked right and the
colour did not.

## 5e. The cartridge's models, on the terrain (19 Aug 2026)

`tier1/t1_mesh.c` draws the N64 meshes: the gunboat in the river, the MCV on the shore,
the tree line, the Nod gun turrets, each at the position and facing the engine reports,
textured and lit from the pack's own art.

Measured: **11.6 FPS**, 239,000 terrain pixels plus 8,000 of models, 64 live objects.

Three things worth keeping:

- **Two palettes, one per texture bank.** The terrain quantises to 255 colours losing
  0.670% of texels; the mesh bank holds only 193 distinct opaque colours and loses
  **nothing at all**. Sharing one palette between them would have been measurably worse
  for both. Swapping is `sr_use_shade`, a single pointer store between passes.
- **Cutout transparency is per texture, not global.** `SR_Texture.ckey` makes palette
  index 0 a hole, which the models' 348 cutout triangles need. It cannot be a global rule
  because the terrain uses index 0 as a real colour: its water holes are drawn, not
  skipped.
- **The type lookup does not belong in the draw loop.** `t1_mesh_for_type` is a linear
  scan over 211 codes, and running it per object per frame was 13,500 string compares a
  frame and measured 8.63 ms. Resolved once when the object list refreshes at 5 Hz
  instead, it is 4.50 ms including all the drawing.

The camera opens on the centroid of the player's own force rather than the middle of the
map. The scenario's `VIEW|` line is the console's real answer and the brain does not
report it, so this is a stand-in that needs no new brain export and puts the mission's
opening on screen.

## 5f. The mission on the Voodoo 2: 93.69 FPS (19 Aug 2026)

`tier1/w98_glidegame.c` runs the same mission on the 3dfx. Same brain, same camera, same
terrain, same models. The only difference is that `t1_glide_open()` installs itself as
`t1_tri_hook`, so every triangle `t1_terrain.c` and `t1_mesh.c` submits goes to the card.
**Neither of those files knows, and neither of them changed.**

| | software, 640x400 | Voodoo 2, 640x480 |
|---|---|---|
| simulation (tick + object dump) | 3.65 | 3.61 |
| terrain | 33.50 | **4.16** |
| objects | 4.42 | **2.85** |
| depth clear | 6.95 | 0 (hardware) |
| sidebar + blit | 4.07 | not drawn yet |
| present | 15.35 | **0.02** (page flip) |
| **frame** | **83 ms, 12.0 FPS** | **10.67 ms, 93.69 FPS** (84.72 with the sidebar) |

Nearly eight times the frame rate at more pixels, with bilinear filtering the software
renderer cannot afford. **The frame is no longer fill bound: the biggest single cost is
now the simulation**, and the renderer is the cheap part.

### Three things the card needed that the CPU did not

- **The atlas had to be re-cut.** A Voodoo 2's maximum texture is 256x256 and the terrain
  atlas is 1024x1024. Every cell is a 24x24 tile at an integer origin, so a 256x256 page
  holds a 10x10 grid and this mission's 211 distinct tiles fit in three pages. That is not
  a Glide-only concession: it took the terrain art from 1,048,576 bytes to 196,608 and the
  cell table from 86,016 to 16,384, and the software renderer draws the pages unchanged
  because 256 is still a power of two.
- **Transparency is a chroma key, not a per-texture flag.** The software path skips
  palette index 0 per texture; Glide has one colour comparison, so it is switched per
  pass. The terrain must have it OFF, because index 0 there is a real colour: the water in
  its holes.
- **Filtering had to become per pass.** Bilinear is free on this card and the cartridge's
  own RDP filtered too, so the terrain uses it. But a filtered texel next to a transparent
  one blends TOWARD the key colour and lands near it without matching, so the cutouts
  fringe. The object pass is point sampled, which also matches the software rasteriser
  exactly and keeps the two backends comparable frame for frame.

### The sidebar is geometry now

A Voodoo 2 has no 2D output at all, so the 1995 sidebar cannot be blitted. `game/dosbar.c`
still draws it into its own 8-bit 320x200 surface exactly as always, and then the 80 wide
column it occupies is copied into a 256x256 page, uploaded as a palettised texture and put
on screen as **one quad** with depth off. Same code, same art, same palette; only the last
step differs.

It is re-uploaded every frame rather than treated as static art, because credits, build
clocks and text change. That is 64 KB a frame over PCI, and it measures **1.51 ms**, which
includes `dosbar.c` compositing the whole surface.

The frame with the sidebar in it is **84.72 FPS**, against 93.69 without. The DOS palette
is a third texture bank, so the passes now swap the TMU palette three times a frame, and
the terrain pass went from 4.16 ms to 5.50 which is where most of that went.

**The radar plate is see-through on purpose, for now.** Nothing draws a radar yet, so those
pixels are palette index 0, which the chroma key drops and the world shows through. When a
radar pass exists it fills exactly that hole.

### Infantry: 2D billboards, because that is what the console did

Infantry are not meshes and never were, so their type codes correctly resolve to no mesh
and nothing else was drawing them: **every infantryman in the mission was missing.**
`tier1/t1_dosinf.c` reads `dosinfantry.pack` and draws them as billboards.

The pack needed **no conversion at all**: 8-bit indices in the same TEMPERAT palette the
sidebar uses, house remap already applied, and the baker already sized its textures for
this card. Frame selection is the engine's own: `DoType` picks the anim slot (prone,
firing, crawling, dying), the stage picks the frame within it, and the facing goes through
the engine's 256-entry table. Both facing tables are copied verbatim rather than derived,
because the obvious formula disagrees with the real one on 79 of 256 directions.

Strips are uploaded **lazily**, on first use. The pack is 5 MB and the TMU is 4, but one
mission touches five strips. Measured: 14 infantry a frame, 5 strips, **0.07 ms**, and
3.8 MB of texture memory still free.

### The sidebar is live now

`wb_sidebar()` reads `GAME_STATE_SIDEBAR` and feeds `dosbar.c`'s `DB_State`: real credits,
real power produced and drained, the real buildable list with each item's construction
progress, hold and busy flags, and whether the radar is active.

Reading it correctly meant measuring the struct rather than trusting the source, because
**the whole ABI is `#pragma pack(1)`** and that lesson had just cost a day. A probe
compiled against the real header gives `CNCSidebarStruct` a 59 byte header and
`CNCSidebarEntryStruct` exactly 130 bytes, and those offsets are named constants in
`w98_brain.c` with the measurement recorded beside them.

First run on the box: `credits=2000 power=0/0 radar=0 buildables=0+0`. Every one of those
is right for GDI mission 1 at the start: the player has an MCV and no construction yard,
so nothing is buildable and there is no power or radar yet.

**And the sidebar drew the GDI eagle.** Nothing was added to make it: the 1995 code puts
the faction logo in the radar plate when the radar is inactive, and it was only ever
showing a black hole before because the radar flag was hardcoded true. Feeding it the
truth made it draw the right thing.

### The loop closes: deploy, build, place

`CNC_Handle_Sidebar_Request` is wired, and the sidebar strip is hit-tested with the 1995
engine's own geometry, quoted from `game/dosbar.h`: two columns at DOS x 248 and 283, rows
24 apart from y 92, cameos 32 by 24. The sidebar is one quad from DOS x 240..319 into
screen 480..639 doubled, so the inverse is exact and needs no fudge.

A cameo click starts construction; a click on a FINISHED item begins placement, and the
next click on the map puts it down. **D deploys**, because in the 1995 game you deploy an
MCV by clicking it while it is selected, which means aiming at a 24 pixel sprite; D issues
the same engine command at the selected object's own cell so it cannot miss. The engine
decides what the default action there means, and for an MCV on its own cell that is deploy.

Verified end to end on the box:

```
deploys=1  build_clicks=1
BUILDING FACT cell=(55,52)
sidebar_live: credits=1772 power=15/26 radar=0 buildables=1+0
```

Every number there is the engine's. The MCV became a construction yard, the yard offered
exactly one buildable, and **credits fell from 2000 to 1772** because construction actually
started. Power went from 0/0 to 15/26 because the yard produces some. The cameo draws with
its build clock sweeping round, which is `dosbar.c` doing what it always did once it is
handed a real completion percentage.

### THE BUTTON SIGNAL PULSES, and that is the whole story of the mouse

Read this before touching input on this platform.

`GetAsyncKeyState`'s high bit is documented as the CURRENT state of the button. Polled
from a windowless fullscreen Glide app on this machine it does not behave like a level:
**one physical press produces a train of ones and zeroes.** Taken at face value that turns
a single click into a dozen. Measured: two presses registered as **13 clicks and 21
selections**, and every selection was undone by the next spurious click a frame later, so
from the outside it looked exactly like clicking not working.

The fix is not to debounce by counting consecutive equal reads, which fails on a pulse
train. The raw read is treated as evidence the button is down RIGHT NOW, and the button is
considered held until it has been quiet for 120 ms, which is far longer than any gap
inside one press and far shorter than a deliberate double click.

**That fix then broke single clicks, for a reason worth knowing.** Holding for 120 ms means
the release edge arrives 120 ms after the finger lifts, and a real hand keeps moving in
that window, so every click crossed the drag threshold and became a tiny band that caught
nothing. A band select worked and a click did not, which is a very odd symptom until you
see why. So the pointer position is also latched: everything uses where the pointer was
when the button was last actually SEEN down, never the live pointer. Drift after release
cannot turn a click into a drag any more.

Verified with a script that clicks and then immediately moves away, which is what a hand
does: one click, one unit selected.

### The mouse took three attempts, and the second one was the clever one

The pointer could be moved and the keys scrolled, but nothing could be clicked.
That symptom is precise and it ruled out most of the stack: keys work, so
`GetAsyncKeyState` works; the pointer moves, so `GetCursorPos` works.

1. **Scale the desktop into the view.** The desktop is 1280x1024 and the card shows
   640x480, so the pointer crawled at half speed, and squashing 5:4 into 4:3 skews
   vertical against horizontal so it does not go where the hand sends it.
2. **Track motion and recentre the OS cursor each frame**, which is what a fullscreen
   game normally does and is the textbook answer. On the DESKTOP `SetCursorPos` applies
   immediately with zero drift, measured over 20,000 polls. **Under Glide it does not
   stick**, so every frame contributed a phantom delta and at eighty frames a second the
   pointer walked away on its own. Clicking repeatedly at the desktop centre left it at
   537,437 instead of 320,240.
3. **Absolute, 1:1, over a centred window of the desktop, and nothing written back.** The
   pointer is a pure function of where the cursor actually is, so nothing can fight it and
   nothing can drift. Verified exactly: desktop 740,562 with a 320,272 offset reports
   420,290, and one click selects one unit.

**My own probe was the other lesson.** It counted PHANTOM button presses and found none,
which I read as "the buttons work". It never tested that a real press is SEEN. Testing a
mechanism for false positives is not testing it.

### Input on a card that owns the screen

A fullscreen Glide app has no window, so there are no `WM_` messages and **no system
cursor: Windows draws nothing on a Voodoo's output.** Input is therefore asked for rather
than received: `GetAsyncKeyState` for keys and buttons, `GetCursorPos` for position, scaled
from desktop pixels into the 640x480 the card is showing. `GetCursorPos` does work under
fullscreen Glide on this hardware, which an earlier project established on this same box.

Proven on the card: clicking the MCV selects exactly it (`sel=1` on that object and no
other), and right clicking open ground takes it to `mission=Move` with a `nav` target set.
The picking is the same inverse camera the software build uses and none of it changed.

**The pointer is drawn art, and it lives in the spare corner of the sidebar page.** The
sidebar column is 80 wide inside a 256 wide texture, so there is room, and sharing the page
means the cursor cannot be lost to a texture that uploaded without error and drew nothing
anyway. That is not hypothetical: a first attempt with its own 16x16 texture did exactly
that, reported no error, and left no way to aim.

**Note for anyone driving this remotely: VNC is blind and deaf while Glide has the screen.**
The desktop comes back when the app exits. AutoIt still moves the real cursor, and
`GetCursorPos` still reports it, so `MouseMove`/`MouseClick` through the agent daemon is the
way to drive it.

### One chroma key value fixed two things

The DOS pass keyed on black, and the pack's index 0 is black, and so is `DB_BLACK` (12),
the engine's OPAQUE black. So the key cut out both: the cursor lost its outline and the
radar plate was see-through. The palette is now copied with index 0 forced to magenta, the
key follows it, and the same change gave the cursor its edge back and the radar plate its
proper black.

### The bug that nearly passed, and the design that caught it

The reserved transparent palette index was left black, so with no chroma key every cutout
texel painted black and **the tree line rendered as black silhouettes that looked
plausible**. It is now MAGENTA on purpose, in both converters. If either backend's
transparency is ever misconfigured the screen fills with magenta rather than with
something that reads as correct-but-dark artwork.

**a. `opengl32.dll` is on the box and it is Microsoft's software OpenGL 1.1.** Nobody had
counted on that. It raises a genuine option: the existing renderer could potentially run on
Windows 98 with only the platform layer replaced (SDL2 swapped for Win32 plus WGL) and no
renderer rewrite at all, because Tier 1 is already declared fixed function. It would be
slow, MS's implementation is famously so, and it has no extensions. But it would put the
ACTUAL GAME on Win98 in days rather than weeks and leave only the renderer to do, which is
a large de-risking step for a small cost. Worth costing before committing to the long road.

**b. When the shared-file refactor happens.** The additive approach runs out the moment the
port needs the real scene rather than a prototype, because the real scene is assembled
inside `cnc_eyes.cpp` and the GL headers. Pulling those call sites behind a backend is one
focused change on `main` that both other branches must then rebase onto. It is cheap to do
once and expensive to do while three agents are editing the same file, so it needs a slot,
not an ambush.

---

## The 640x480 HUD on the Voodoo (20 Aug 2026)

The direction for this build: use the new bar, not the 1995 one. `game/hud640.c` is
compiled **unedited**, exactly as `game/dosbar.c` is, and `tier1/t1_hud.c` is the Win98
side of the contract its own header sets out ("the Win98 port only has to hand it
memory").

**Three pages, because the maximum texture on this card is 256x256 and the bar is
160x480.** Rows 0..255 in one page, 256..479 in a second, and a 256x64 third page for the
two tab plates, the mouse pointer and a block of solid white that every screen-space line
is drawn from. 288 KB of a 4 MB TMU.

**The art is true colour, which nothing else on this branch is.** Everything from the
cartridge and from the 1995 CD is palettised and uploads as `GR_TEXFMT_P_8`. The HUD
uploads as `GR_TEXFMT_RGB_565` through the new `SR_Texture.px16`. Every chunk that
`hud640.c` composites was checked and is opaque, so no alpha has to survive; 565 rather
than 1555 buys a bit of green instead of an alpha bit nothing would read.

**Recomposing is not free, so it is conditional.** Composing writes 300 KB, converting
reads that and writes 150 KB, the upload sends 288 KB over PCI. The bar is composed only
when something it can see has changed, and the pointer counts because the controls light
up under it. Measured with everything on: **73 FPS at 640x480**, of which the HUD is
2.1 ms.

**Cameos come from the DOS pack, composed in the engine's own colour space** and doubled
1:1 into the 64x48 opening (the DOS cameo is 32x24, and the new art was cut so that
doubling is exact). That is the fallback path `game/cnc_sidebar.h` takes when true-colour
cameo art is absent, overlay for overlay: CLOCK ghosted through the pack's own clocktab,
PIPS for ready and on-hold, CLOCK frame 0 for a busy factory.

**The minimap is new.** The radar CONTENTS are registered as not
done on any build. Tier 1 has them: one colour per terrain cell, averaged at load from the
same atlas page and the same palette the ground is drawn with, so the radar cannot drift
away from the map it claims to show; house-coloured pips with buildings drawn over their
whole footprint; and the view TRAPEZOID, which is what a perspective camera at this pitch
actually sees rather than the rectangle it would be a lie to draw. Clicking it moves the
camera.

## Scripted input, and why it had to exist

**Synthetic mouse buttons do not reach this build.** Measured over 700 frames with three
injection methods (AutoIt `MouseDown`/`MouseUp`, AutoIt `MouseClick`, and `mouse_event`
called straight through user32): the pointer moved exactly where it was told every time,
and `GetAsyncKeyState(VK_LBUTTON)` saw the button **zero** times. The keyboard is fine, 17
frames of a scripted D. A real hand works: units have been selected, a band dragged and
given move orders.

So `tier1/t1_script.c` replaces the OS read at exactly the point the OS read happens. Put
an `input.script` beside the exe and every read of the pointer, the buttons and the
keyboard comes from the file instead; everything downstream is the same code a hand
drives, which is the right seam, because the OS layer is the one part already proven by
hand. It also takes screenshots, which is the only way to see this card's output at all.

    10  move 340 150      ; the MCV
    12  lb 1
    18  lb 0              ; select
    40  key D 3           ; deploy
    420 move 528 232      ; the first build cell
    424 lb 1
    430 lb 0              ; build
    620 shot building.bmp
    700 quit

That run reports `clicks=1 selected=1 deploys=1 build_clicks=1 buildables=1+0` and credits
falling from 2000 to 1962, which is the whole loop verified without a person in the chair.

---

**Arriving cold?** The short way in:
the development loop, the traps, the file map and what to do next. This document is the
long-form record behind it.

---

# The overnight pass, 19-20 Aug 2026

The brief: get this as feature complete as the Windows and macOS builds, without the
Tier 2 presentation chain, using the new HUD. What follows is what landed, what it cost,
and what is still missing. Everything here was verified ON THE BOX, not inferred.

## What the game now has that it did not

| | |
|---|---|
| **The 640x480 HUD** | `game/hud640.c` unedited, in two 256x256 texture pages plus a small third for the tab plates and the pointer. The first true-colour art on this branch, so the Glide backend learned RGB_565. Cameos composed in the engine's own colour space from the DOS pack's own CLOCK and PIPS |
| **The minimap** | Registered as not done on ANY build. One colour per terrain cell averaged from the atlas the ground is actually drawn with, house-coloured pips, buildings over their whole footprint, and the view TRAPEZOID a perspective camera really sees. Click it to go there |
| **Sound** | `audio/audio_win98.c` had never been compiled, let alone run. waveOut opens a real device, Act on Instinct plays off SCORES.MIX, and a mission produces its effects and its EVA lines. Verified by RECORDING the mix to a WAV and measuring it |
| **Fog of war** | Read from `GAME_STATE_SHROUD`, drawn on the terrain's own corners with per-corner alpha so the edges are soft. Buildings you have seen stay drawn; units need live sight |
| **Tiberium** | The cartridge's own ANY_TI filmstrips, 12 types x 12 frames, frame taken verbatim from the engine's OverlayData |
| **Walls** | SBAG/CYCL/BRIK/BARB/WOOD through the cartridge's own 16-way connectivity table |
| **Projectiles** | The console's own bullet models with its DOS name remaps |
| **House colours** | The sand table for GDI, the blue-grey one for everyone else, per triangle, from the pack's own variant textures |
| **Combat effects** | The cartridge's own particle ART, one billboard per anim, through the ROM's anim -> recipe -> source -> system chain. NOT the particle simulation; see below |
| **The mission end** | The console's gold banner on a screen black from edge to edge |
| **Scripted input** | The whole game drivable from a text file, with screenshots, because synthetic mouse buttons do not reach this build |

## Three bugs worth remembering

**1. The quad path had no bounds check, and that is what wedged the card.** The 2D
quad helper was written for the sidebar and the pointer, which are at fixed on-screen
coordinates, so it never needed one. The INFANTRY are not: a soldier off the side of the
view projects to tens of thousands of pixels. The sprite's size was clamped and its
position never was. Glide does no clipping, so that went to the card, and the card stopped
with the SLI pair's interleaved scanlines frozen and the process still running.

It reproduced only while scrolling, only with the infantry pass on, and only once the
soldiers left the view. Two wrong theories cost most of the hunt and both are the obvious
thing to reach for: **the frame rate** (a slower frame moves the camera further per step,
so it steps over the bad position -- turning tracing UP therefore "fixed" it) and **the
swap queue** (`grBufferSwap(1)` never returns on this driver, and spinning on
`grBufferNumPending` is millions of driver calls a frame). Neither remedy is available on
this hardware; both are recorded in `t1_glide.c` so the next person does not spend the
hour again.

**2. The object texture uploader only accepted SQUARE textures**, and refused 54 of 359.
The card takes any power of two up to 256 at up to 8:1, which the uploader's own aspect
code already works out. What that looks like from outside is not a missing texture, it is
a missing OBJECT: a triangle whose texture never uploaded is dropped. All 42 sandbag cells
were being submitted every frame and drawing three pixels between them. The identical
mistake had already been made and fixed once for the DOS infantry.

**3. The shroud entries are the MAP RECTANGLE, not the 64x64 grid**, and the rectangle is
the expanded one `GAME_STATE_STATIC_MAP` reports. Taking the index as a raw cell number
piles 700 entries into the top-left corner, which on screen reads as "the player can see
nothing anywhere" rather than as a mistake. The count is what gave it away: 700 is
precisely 28 x 25.

## Two hardening changes that came out of the hunt

The near plane is **one whole cell**, not a quarter. The card is in W-buffer mode, whose
contract is that 1/w stays inside the unit interval, and a quarter cell allowed FOUR. The
nearest ground this camera can see is over six cells deep, so nothing is lost.

A triangle whose screen area is under a fiftieth of a pixel is dropped. The setup unit
divides by that area, and an edge-on triangle hundreds of pixels wide makes the reciprocal
enormous.

## Performance, measured with no VNC attached

| build | FPS at 640x480 |
|---|---|
| terrain + models + infantry, no HUD | ~76 |
| everything except the shroud | ~70 |
| everything, unexplored map (worst case for the veil) | ~46-55 |

The shroud is the single largest cost because an unexplored map means every cell draws a
second time; it gets cheaper as the player explores. The HUD is 2.1 ms and only recomposes
when something it can see has changed. Sound costs about 3 FPS.

The 3000-frame camera sweep passes with everything on, 1.55 million guard rejects and no
wedge.

## How to run the regression

Put `input.script` beside `w98glide.exe` and run it. `tools/win98/regression.script` is
the standing one: it selects, deploys, builds, scrolls hard in every direction, jumps the
radar, forces both mission-end banners and takes screenshots at each step.

    w98glide.exe 4000                 the regression, with sound
    w98glide.exe 4000 recwav          record the mix instead of playing it
    w98glide.exe 3000 sweep           the camera-stress regression
    w98glide.exe 2000 trace           a heartbeat line per frame
    w98glide.exe 2000 trace2          a line per PASS per frame, for a wedge

Bisection switches, all off by default: `noshroud` `noover` `noobj` `noinf` `nohud`
`noterr` `nohouse` `freecam` `novsync`.

# The second overnight pass, 20 Aug 2026

The brief, verbatim: infantry and vehicle animations lacking most of their frames;
cursors not the correct mesh, and the 2D cursor must show over the HUD; building placement
has no preview; implement menus, movies, the 3D animated cursor, shadows, the build-up
animation and the MCV deploy rig, and the particle simulation rather than just its art;
and structure textures are very low and barely recognisable.

All of it is on the box. What follows is what it cost and what it taught.

## The one that mattered most, and it was not on the list

**A Voodoo's texture coordinates are not texels.** They live in a fixed 0..256 space
stretched over whatever LOD is bound, and this renderer hands the rasteriser texels
everywhere -- because that is what the software path wants and what the packs store -- and
the backend never converted. So `s = 8.0` meant texel 8 of a 256-wide page and texel 0.5
of a 16-wide object texture.

It hid for a fortnight because EVERY page-shaped texture in the game is 256 across: the
terrain atlas, the tiberium and smudge sheets, the HUD pages, the DOS infantry strips. For
those the scale is exactly 1, so everything anyone had ever looked at closely was correct.
The cartridge's own object art is 16x16 and 32x32, and all of it was drawing its corner
texel stretched over the whole polygon. That is the reported "textures on structures are very
low": they were not low, they were ONE TEXEL.

The rule is one uniform `256 / max(w, h)`, and the non-square case proves it rather than
being assumed: a 256x32 infantry strip draws correctly with raw texel coordinates, so the
scale must be 1 on both its axes, which rules out a per-axis 256/w, 256/h.

## What the converter was throwing away

`mkmesh.py` walked past the pack's node animation with a pointer arithmetic step and no
comment: 22 meshes, 1,091 KB of baked pose. It also skipped the SECTION table -- how a
building assembles piece by piece -- and dropped the shadow and translucent triangles
outright. All four are converted now (T1MESH05), along with per-vertex RGB (17.5% of the
bank's vertices are not grey) and the per-triangle wrap byte.

Empty parts are kept, because the animation block is indexed BY PART NUMBER and dropping
one would silently shift every node's pose after it.

## The shadow textures are alpha planes, and that is not a detail

Every shadow texture in the ROM is white RGB with its coverage in ALPHA, and 34 of the 35
peak below 128. The palettised bank's threshold is alpha >= 128, so all 34 were already
fully transparent in the pack, and the one exception (the SAM's, peaking at 184) would
have drawn a solid WHITE PLATE on the ground. Wiring a shadow pass up without noticing
that draws nothing and reports success.

They have their own bank now, one byte of coverage per texel, uploaded as
`GR_TEXFMT_ALPHA_8`. A triangle names one through a texture index with bit 30 set.

## Four traps, each of which cost real time

**1. `sr_texture()` does not clear a field it does not know about.** Texture banks are
malloc'd and never zeroed, and the Glide backend picks its upload FORMAT by testing the
optional pixel pointers in order. Adding `alpha8` therefore made it hand the card a stale
heap pointer, which on this hardware is not an error, it is a frozen machine -- and it
froze on the OBJECT bank, so `noshadow` did not help and the shadow work looked innocent.

**2. The heap id is only unique inside its own heap.** `id` is the object's slot in its
own `TFixedIHeapClass` pool, so the first BUILDING and the first UNIT are both 0.
Deploying an MCV deletes unit 0 and creates building 0; a tracker keyed on the id alone
already had a row for "0" marked not-a-building, and every Construction Yard from then on
was born finished.

**3. `SIDEBAR_REQUEST_PLACE` wants a cell relative to the expanded map rectangle**, not an
absolute world cell -- the engine adds `map_cell_x` back on itself. This build had been
handing it absolute cells since placement was written, so every placement was tens of
cells out or refused with no diagnostic, and the regression had been printing
`placements=0` the whole time without anyone reading it as a failure.

**4. A texture the card refused was invisible to the reject counter.** `glide_tri`'s
texture-refusal path was the one early return in the function that did not touch
`g_rejects`, so the card could be dropping a seventh of the cursor set while the report
said `guard_rejects=0`.

## The buildup clock is ours, and here is exactly why

Every building on this brain reports `makecnt = 1` and its `dostage` never leaves 0 while
BState is CONSTRUCTION. That is not a bridge bug: `BDATA.CPP` guards the whole
construction-animation block on MAKE.SHP having loaded out of a MIX, our headless brain
has no 1995 shape art, and Count stays at its default of 1. With Count 1 the engine also
leaves CONSTRUCTION almost immediately.

But the 1995 engine gives every structure the SAME duration and it is not a taste value:
`timedelay = (5 * TICKS_PER_SECOND) / count`, so `count * timedelay` is five seconds
whatever the frame count is. So the DURATION is the engine's and the COUNTER is ours, and
a brain that can load MAKE.SHP is the thing that deletes it.

Putting the real 268 KB `CONQUER.MIX` in the brain's content directory was tried and does
not change it; that MIX carries the engine's rules, not the shape art.

## Performance, measured with no VNC attached

The full 14-shot regression at 640x480 with every pass on: **52.6 FPS, 19.0 ms a frame**,
3,871 frames, no wedge. The shadow block is about 0.4 ms of it and the particle pool peaked
at 44 live sprites against a 512 cap.


# The second scenario, and the cull it forced (20 Aug 2026)

Converting a second mission was meant to be cheap verification. It was cheap -- one
`mkterrain.py` run, 0.4 s and 0.3 MB, plus the mission's own .INI and .BIN, because the
mesh bank is shared across every pack of a side. What it was not was uneventful.

## The playable rectangle is not the visible rectangle

The ground and the veil were drawn over the whole PLAYABLE RECTANGLE every frame, whatever
the camera was looking at. GDI mission 1's playable area is 28x25, which is 700 cells, and
at that size it went unnoticed. A full-sized map is 62x50, which is 3,100, and the frame went
from 19 ms to 44 -- terrain alone from 9.0 to 26.6 -- all of it for cells nowhere near the
screen. The camera sees about twenty cells across at the widest zoom.

**The cull is conservative by construction rather than by a margin somebody guessed.** The
four screen corners are intersected with the ground plane at the terrain's own highest and
lowest corner -- measured at load, not assumed -- which brackets every height a view ray
can cross, and the union of those eight ground points is the box. A hill cannot poke in
from outside it, because a hill IS the high plane.

Two cells of slop, and the second was earned: with one, an A/B against the un-culled frame
differed by 55 pixels in a ten-pixel square at the extreme top left. That is the FARTHEST
corner of a pitched view, where a fraction of a degree is a long way on the ground. One
cell is right for the near edge and short for the far one.

    SCG09EA   terrain 26.7 ms -> 6.7 ms      27 -> 60 FPS
    SCG01EA   terrain  8.6 ms -> 3.8 ms      58 -> 75 FPS
    the 14-shot regression   19.0 -> 14.4 ms  52.6 -> 69.5 FPS

## The scripted clock had to be fixed before any of that could be checked

Every timed thing downstream of `dt` -- the sim tick, the scroll, the particle step, the
movie -- ran off the wall clock. So two runs of the SAME script reached frame 420 at
different sim times, and two screenshots of "the same frame" were of different moments:
the first A/B differed by 102,210 pixels and none of it was the cull. Measured: 1,226
ticks in one run and about half that in the next, purely because one was faster.

A scripted run now uses a fixed 1/60. The first thing that proved is that both halves of
the A/B reach frame 461 at tick 100 -- and then that the cull differs by ZERO pixels on
both scenarios.

**This is the more important of the two changes.** Every paired switch on this branch --
`noshadow`, `noanim`, `nocursor`, `nocull` -- is a diff, and until now none of them could
be trusted.

## One character

`procrig=ABSENT` had been in the startup report since the rig was written and was recorded
as a baker gap. The converter wrote type codes as
`code[:7].ljust(8, 0)`. The pack has exactly two EIGHT-character codes, ATOMICUP and
PROCANIM, and PROCANIM is the refinery's animation model.

## What a second scenario is actually for

Rotor spin, turret facing and the refinery rig had all been "implemented and unproven" for
days, and the honest answer turned out to be three different answers:

  * the TURRET is proved in play, because GDI 13 puts two Medium Tanks under the player's
    hand at frame one;
  * the ROTOR cannot be proved by placing one, because rotor parts exist on exactly two
    meshes -- HELI and TRAN -- and no scenario places an aircraft at mission start;
  * the REFINERY's rig draws correctly and the engine has never asked for it: its stage
    cycles 0..11 across a thousand ticks and the arm wants 12.

So the report gained two counters that separate "the part exists" from "the part turned",
and a stage histogram for the refinery. A screenshot cannot tell those apart and a counter
can.

## The facing law, the escape key and the two camera moves (20 Aug 2026)

Six of the eight things found in play were one bug each; two of them were the same
bug wearing two hats.

**The MCV drove backwards and the gunboat sailed sideways.** One missing term. The Tier 1
loop handed `t1_mesh_draw_p` the engine's raw DirType facing, and the meshes are not
authored pointing north: `game/cnc_eyes.cpp` carries a MEASURED table (`g_meshForward`)
saying every hull is authored pointing SOUTH except the gunboat, whose hull runs east-west
with the bow at -x, and the Nod gun turret's barrel, which points north. Raw facing is
therefore 128 out on every vehicle -- a tank driving exactly backwards -- and 64 out on
the boat -- a boat sailing exactly sideways. `model_forward` / `draw_facing` /
`turret_delta` in `w98_glidegame.c` are that table and its three rules, ported rather than
re-derived.

The two renderers were checked to be the same rotation before the table was trusted, by
algebra rather than by screenshot: main's `facing_rot` builds `a = -face` and applies
`(x*rc + z*rs, -x*rs + z*rc)`, which expands to exactly the `(mx*cos - mz*sin,
mx*sin + mz*cos)` that `t1_mesh_draw_p` applies for a positive facing. Same number in,
same picture out. `tools/win98/mkmesh.py` copies vertices untransformed, so model space is
shared too.

Then it was measured on the box three ways, because "it compiles" is not a proof:

* `facing:` in the report is a new audit -- the mean angle, in DirType units, between the
  direction a unit actually MOVED and the facing it was drawn at. 0 is right, 64 is
  sideways, 128 is backwards. The regression reports `mean_err=0.0` over 169 samples.
* an A/B of the same sim frame with the old and new binaries: the gunboat went from
  crossing the river at 60 degrees to lying along it.
* the hull's own width profile in the frame buffer: the west end tapers to a point over
  16 px, the east end blunts off in 8, and the boat is travelling west. The bow leads.

**"Impossible to start GDI 1"** was the escape key. ESC skipped the briefing movie AND,
because a hand holds a key for a dozen frames at 60 FPS, quit the process on the very next
one. ESC is now an edge, consumed by whichever block handles it, and in a mission it means
BACK -- the main menu, sim stopped, resume by picking the mission again. Leaving for good
is EXIT GAME, where a player looks for it.

**The menu played the mission's music** because the music block ran before
`t1_menu_load`, so `g_inmenu` was still 0 when it chose. It now runs after, and picks
MAP1 (the console's own menu theme), handing over to AOI when a mission starts.

**Edge scroll and middle-drag** are new and now provable from a script: the middle button
goes through `key_down`, so `key MBUTTON n` holds it, while `raw_mbutton` keeps counting
the physical button underneath. `camera: edge_scroll_frames=120 drag_frames=156` on the
test run.
