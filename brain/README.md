# CNC3D — the brain

The GPL Tiberian Dawn game logic, built as a platform-agnostic library exposing the flat
`CNC_*` C ABI, **running missions extracted from the Nintendo 64 cartridge**.

Verified: `SCB01EA` (Nod 1) loads with **73/73 objects matching the N64 INI**
exactly (type, owner, cell, health); `SCG01EA` (GDI 1) matches 60/60. 3000 ticks with no
crash, deterministic across runs, and the simulation genuinely evolves (triggers fire,
`[TeamTypes]` reinforcements spawn and walk to `[Waypoints]`).

## Layout

```
patches/    our changes to upstream Vanilla Conquer (see "Rebuilding")
host/       cnc_host.c   — dependency-free host over the CNC_* API (dlopen / LoadLibrary)
            run1.cpp     — the loader used for the verified run above
missions/   SCB01EA/SCG01EA .INI (verbatim from the ROM) + converted .BIN terrain
            n64map_to_bin.py — N64 .MAP -> PC .BIN converter
stubcontent/ DESERT.MIX, TEMPERAT.MIX — synthetic palettes (see "Game data")
lib/        TiberianDawn.dylib — prebuilt macOS brain
```

## Running

```
cd host && ./run1 ../lib/TiberianDawn.dylib ../missions/ SCB01EA 1 ../stubcontent/ 300
```
Args: `<library> <mission dir> <scenario> <build level> <content dir> <ticks>`.

## Rebuilding

**Do not clone anything.** `vanilla/` is committed source, not a working copy someone is
expected to fetch. It was gitignored until recently on the theory that the patch beside
it recorded every change we make to the brain; the checkout had quietly drifted ahead of
that patch, and CI spent a day red proving it. Cloning over it now is what broke CI a
second time, because git stops at "destination path already exists".

On macOS, run the script rather than the commands. It is what CI runs and what the
release is built from, so there is one recipe:

```
tools/mac/build-brain-mac.sh              # -> game/TiberianDawn.dylib
tools/mac/build-brain-mac.sh game playable
```

What it adds to the raw cmake below is `CMAKE_OSX_DEPLOYMENT_TARGET`, taken from
`tools/mac/deployment-target.sh`, and an `otool` assertion that the finished library
really carries it. Without it clang stamps `LC_BUILD_VERSION minos` with whatever SDK the
build machine has, and dyld refuses a Mach-O whose floor is newer than the running
system: until recently the brain, `cnc3d` and `cnc_eyes` all said 26.0, so every Mac
below macOS 26 rejected the release before `main()` ran. All three load in ONE process,
so a brain left at 26.0 beside two fixed binaries still fails and merely looks fixed.

The raw commands, for reference and for platforms with no script:

```
cd vanilla
cmake -S . -B build-native -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
      -DBUILD_REMASTERTD=ON -DBUILD_VANILLATD=OFF \
      -DBUILD_VANILLARA=OFF -DBUILD_REMASTERRA=OFF -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF
cmake --build build-native --target TiberianDawn -j8
```

`CMAKE_OSX_DEPLOYMENT_TARGET` has to be passed on the command line, not left to the
`MACOSX_DEPLOYMENT_TARGET` environment variable: cmake only reads the variable when it
INITIALISES a cache, so an existing `build-native/` configured earlier keeps
its empty value and silently rebuilds at the old floor.

`patches/vanilla-cnc3d.patch` is still the readable record of what we changed and it must
stay true: `tools/check-brain-checkout.sh` fetches upstream at the commit pinned in
`patches/UPSTREAM.txt`, applies the patch to it, and demands the result equal `vanilla/`
byte for byte. CI runs it on every push. So if you edit the checkout, regenerate the patch
in the same commit; if you move upstream, re-pin `UPSTREAM.txt` in the same commit.

## The four portability patches (and why each is Win98-safe)

The Win98 + Voodoo2 target is the reason the brain must stay dependency-free. Every patch
is additive and disappears or still applies on a 32-bit Windows build.

1. **`wwkeyboard_null.cpp`** — `COMMONR_SRC` was already headless (`soundio_null`,
   `video_null`) but hardcoded `wwkeyboard_win32.cpp`, the sole reason the target refused
   to build off Windows. All input arrives via `CNC_Handle_Input()`, so a null backend is
   correct. Deliberately NOT `wwkeyboard_sdl2.cpp` — that would bind the game logic to SDL.
2. **`-fms-extensions -fdeclspec`** — `dllinterface.h` is MSVC-flavoured (`__int64`,
   `__declspec`, types declared in anonymous unions). Compiler flags, not source edits.
3. **`cnc3d_compat.h`** — `timeGetTime`, `itoa`, `UNREFERENCED_PARAMETER`, `HINSTANCE`,
   `DWORD`, `MB_*`, `ProgramInstance`, `GetModuleFileNameA`, `MessageBoxA`. Entirely inside
   `#if !defined(_WIN32)`, so it vanishes on the Windows build.
4. **`dllinterfaceeditor.cpp` excluded off Windows** — the map-editor API is COM
   (`SAFEARRAY`/`HRESULT`/`VT_UI1`); the headless brain has no use for it.

## Game data: you do NOT need the original PC release

The only hard requirement is a theater palette — `<THEATER>.MIX` containing
`<THEATER>.PAL`, or `Init_Theater` memcpys from NULL and segfaults. `stubcontent/` holds
synthetic ones and that is enough for a full load. Original unit/building art is only
needed if you want `GAME_STATE_LAYERS`, which we don't: we have the N64 art.

## Terrain: the cartridge ships its own conversion table

N64 `.MAP` = 64×64 `u16` **big-endian direct tile IDs**, clear = `0xFFFF`.
PC `.BIN` = 64×64 cells of `(template, icon)`, clear = `0xFF 0x00`.

The bridge is `<THEATER>.TL4`/`.TL8` in the extracted assets — **arrays of PC
(template, icon) pairs indexed by N64 tile ID**. (They were originally mis-catalogued as
palettes; TL = tile lookup.) Validated across all 77 usable maps: 93,126 cells,
24,216 template placements, **zero unmapped IDs, zero wrong-theater templates**.

The `.BIN` is optional — without it `Read_Binary_File` returns false and the engine falls
back to a legacy `[TEMPLATE]` INI section the N64 maps don't have, so the map loads clear.

## Gotchas that cost real time

- **`CNC_Start_Custom_Instance` concatenates `"%s%s.INI"` with no separator** — the mission
  directory argument must end in `/`.
- **Do NOT pass `-CD<path>` to `CNC_Init`.** `Parse_Command_Line` uppercases every argument
  and returns false on an unrecognised switch, silently skipping `Main_Game` entirely.
- **`GAME_STATE_LAYERS` is a rendering API, not a state API.** `Get_Layer_State` harvests
  objects as a side effect of `object->Draw_It()`, so with no SHP art it collects nothing
  and returns false *by design*. Enumerate the heaps instead.
- **`Get_Occupier_State` stride bug in EA's own code** (`dllinterface.cpp:5628`): it does
  `occupier + 1` after already advancing past the n objects, so the real per-cell stride is
  `4 + 8*n + 8`, while `memory_needed` is computed as `4 + 8*n`.
- **MEGAMAPS**: the DLL is built with it, so the engine map is 128×128 and cell numbers are
  not the INI's. Inverse transform: `ini_cell = y*64 + x` (`function.h:871 Confine_Old_Cell`).
- Westwood sources contain **Latin-1 bytes** — patch them binary-safe; a UTF-8 read of
  `startup.cpp` throws.
- Vanilla Conquer's `add_feature_info(RemasterTD BUILD_REMASTERTD, ...)` has a stray comma,
  so cmake **falsely reports the target as disabled**. Check the target list instead.
- `SCG71EB.MAP` / `SCG74EA.MAP` were previously 992 bytes (dumped raw before method `0x11`
  was reversed). **Fixed** — re-extracted with `tools/romdump/decomp11.py`, both now decode
  to exactly 8192 bytes. They are *genuinely all-clear* maps, which is not a decode bug:
  this codec's maximum match length is 18 bytes, so filling 8192 identical bytes needs ~455
  match commands (455x2 + flags + header ~= 975 vs the actual 992). They are almost
  certainly placeholders for the dormant, unplayable missions left in the ROM.
