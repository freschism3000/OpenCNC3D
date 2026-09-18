#!/bin/sh
# Build the Windows half: cnc3d.exe, cnc_eyes.exe and the brain DLL beside them.
#
#   tools/win/build-win.sh              -> build/win/
#   tools/win/build-win.sh --no-brain   -> skip the brain (it changes rarely and is slow)
#
# Run tools/win/setup-toolchain.sh once first.
#
# WHY 32-BIT. The Remaster DLL's save/load path casts a BuildingTypeClass* through
# `unsigned int` (dllinterface.cpp, Decode_Pointers). At 32 bits that is lossless and was
# how the code was written; at 64 it truncates, and GCC is right to refuse it. Building
# i686 makes the question disappear instead of suppressing it, and it is the same
# architecture the promised Win98 / Voodoo 2 target needs. Nothing here depends on the
# larger address space. See BUILDING.md.
#
# WHY NO SOURCE WAS EDITED TO MAKE THIS WORK. Every file except game/cnc_eyes.cpp already
# carried its own `#ifdef __APPLE__` GL guard. The renderer did not, and it is the file
# most likely to be open in a concurrent edit, so compat/win/ supplies the three macOS-only
# headers it asks for by name instead. See compat/win/README.md.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

# THE BUILD NUMBER, regenerated before anything compiles. tools/version.sh reads the
# VERSION file at the repo root and writes game/cnc3d_build.h; nothing else in the tree
# is allowed to carry a version string, so there is no second copy to forget to bump.
# tools/release.sh has already written the header with the bare release number by
# the time it calls this, and regenerating it here would stamp the binary
# "+sha-dirty" for a build that is about to become the tag.
if [ -z "$CNC3D_SKIP_VERSION_HEADER" ]; then
    sh "$ROOT/tools/version.sh" --header "$ROOT/game/cnc3d_build.h"
fi

HOST=${CNC3D_WIN_HOST:-i686-w64-mingw32}
SDK="${CNC3D_WINSDK:-$HOME/.cnc3d-winsdk}"
OUT="$ROOT/build/win"
DO_BRAIN=1
[ "$1" = "--no-brain" ] && DO_BRAIN=0

SDL2_ROOT=$(ls -d "$SDK"/SDL2-*/"$HOST" 2>/dev/null | head -1)
[ -n "$SDL2_ROOT" ] || { echo "no SDL2 for $HOST in $SDK. Run tools/win/setup-toolchain.sh" >&2; exit 1; }
[ -f "$SDK/lib/$HOST/libz.a" ] || { echo "no zlib for $HOST in $SDK. Run tools/win/setup-toolchain.sh" >&2; exit 1; }
command -v $HOST-gcc >/dev/null 2>&1 || { echo "no $HOST-gcc on PATH (brew install mingw-w64)" >&2; exit 1; }

# The anti-drift guard, first, before anything is compiled. See tools/win/sources.sh.
tools/win/check-sources.sh

. tools/win/sources.sh

HDR=""
for c in brain/vanilla/tiberiandawn; do
    [ -f "$c/dllinterface.h" ] && HDR="$c" && break
done
[ -n "$HDR" ] || { echo "cannot find dllinterface.h (clone Vanilla Conquer into brain/vanilla)" >&2; exit 1; }

CC=$HOST-gcc
CXX=$HOST-g++
OBJ="$OUT/obj"
mkdir -p "$OBJ"

# compat/win FIRST: it answers <OpenGL/gl.h> and <dlfcn.h> for the renderer. Then game/,
# for the same reason app/build.sh puts it first -- game/dosbar.c is a strict superset of
# the sidebar copy and dosmenu.c must see the wider DB_State.
INC="-I$ROOT/compat/win -I$ROOT/game -I$ROOT/menu -I$ROOT/video -I$ROOT/audio \
     -I$SDL2_ROOT/include/SDL2 -I$SDK/include"
LIBDIRS="-L$SDL2_ROOT/lib -L$SDK/lib/$HOST"
CDEFS="-DGL_SILENCE_DEPRECATION"
# A COOKED BUILD has no F5 panel (see app/build.sh). $CDEFS already reaches every object
# below, so this one line is the whole of the Windows half.
[ -n "$CNC3D_COOKED" ] && CDEFS="$CDEFS -DCNC3D_COOKED=1"

echo "== C sources ($HOST)"
# THE C DIALECT SPLIT IS THE MAC BUILD'S, NOT A NEW ONE.
#
# game/build.sh holds the sidebar rasteriser, the 640x480 HUD, the Options dialog and the
# audio engine to -std=c89 on purpose, and says why: they carry no GL and no SDL, so a
# Win98 compiler must be able to take them unchanged. That promise is worth keeping and
# is kept here, which also means this build is the first thing that would notice if
# someone broke it.
#
# The rest gets gnu99. Apple clang quietly accepts C99 declarations under gnu89 as an
# extension and GCC does not, so app/campaign.c (declarations inside `for`) is the one
# file where the Mac flag would not have crossed. That is a compiler difference, not a
# portability problem in the code.
COBJ=""
compile_c() {
    std=$1; shift
    for f in "$@"; do
        o="$OBJ/$(basename "$f" .c).o"
        $CC -std=$std -O2 -g -Wall $CDEFS $INC -c "$f" -o "$o"
        COBJ="$COBJ $o"
    done
}
AUDIO_C89=$(echo $WIN_AUDIO_C | tr ' ' '\n' | grep -v audio_sdl.c | tr '\n' ' ')
compile_c c89   $WIN_GAME_C $AUDIO_C89
compile_c gnu99 audio/audio_sdl.c $WIN_MENU_C $WIN_VIDEO_C $WIN_APP_C
# The scheduler is strict C89; the socket file needs gnu89 for the winsock headers.
#
# NAMED ONE BY ONE, and that is a trap this build has already fallen into. Every other
# group above is compiled from its $WIN_* variable, so adding a file to tools/win/sources.sh
# is enough for it -- but these four carry three different C standards between them, so the
# variable is only ever READ by check-sources.sh and never compiled from. netbeacon.c was
# added to WIN_NET_C, passed the drift check on the strength of that, and then failed at
# the LINK with a dozen undefined nb_* symbols, because nothing here compiled it. If you
# add a net source, it needs a line here as well as a name there.
compile_c c89   net/lockstep.c
compile_c gnu89 net/net_udp.c
compile_c gnu89 net/netmatch.c
compile_c gnu89 net/netbeacon.c
# The room code is a relayed host's address, so it is part of the game and not a tool.
compile_c gnu89 net/roomcode.c

# The HTTP client, for the internet game list. gnu99 because it is written in the same
# style as the launcher that already builds it, and it needs a line here for the reason
# spelled out above: a name in sources.sh is read by the drift check and compiled by
# nobody.
compile_c gnu99 $WIN_HTTP_C

echo "== renderer"
# -fpermissive for the same reason the brain build needs it, and only for that reason:
# these translation units include the Remaster's dllinterface.h, whose MS-style anonymous
# unions GCC rejects and clang accepts. Nothing in our own code relies on it.
# -DCNC3D_NO_MAIN compiles cnc_eyes.cpp as a library for cnc3d.exe, exactly as the Mac
# app build does. It is compiled twice because cnc_eyes.exe (what the gates drive) needs
# the main() and cnc3d.exe must not have two.
$CXX -std=c++14 -O2 -g -fms-extensions -fpermissive -DCNC3D_NO_MAIN \
     $CDEFS $INC -I"$HDR" -c "$WIN_EYES_CPP" -o "$OBJ/cnc_eyes_lib.o"
$CXX -std=c++14 -O2 -g -fms-extensions -fpermissive \
     $CDEFS $INC -I"$HDR" -c "$WIN_EYES_CPP" -o "$OBJ/cnc_eyes_main.o"
$CXX -std=c++14 -O2 -g -fms-extensions -fpermissive \
     $CDEFS $INC -I"$HDR" -c "$WIN_APP_CPP" -o "$OBJ/cnc3d.o"

# -static-libgcc and -static-libstdc++ are NOT enough on their own: they leave
# libwinpthread-1.dll as a runtime dependency, and a stock Windows 11 does not have it,
# so the build would have died on the project owner's machine with a missing-DLL box and nothing else
# to go on. The explicit -Bstatic -lwinpthread folds that one in too.
#
# A blanket -static was tried first and is wrong: it also drags in the static libSDL2.a,
# which then wants every Windows multimedia import SDL normally resolves inside its own
# DLL (it failed on waveOutGetErrorTextW). SDL2 stays dynamic, everything else does not,
# which leaves SDL2.dll as the only file that has to travel beside the .exe.
#
# Console subsystem on purpose: the gates and every diagnostic in this project talk on
# stdout and stderr, and -mwindows would throw all of it away.
# The -Bdynamic island around -lSDL2 is the whole trick. -static on its own makes the
# linker pick the static libSDL2.a, which then wants every Windows multimedia import SDL
# normally resolves inside its own DLL (it failed on waveOutGetErrorTextW). Putting
# -static outside and flipping to dynamic for SDL2 alone gets both halves: SDL2 from its
# DLL, and the GCC runtime, the C++ runtime and winpthread folded into the binary.
# Naming -lwinpthread with -Bstatic was tried first and does not work, because the g++
# driver appends its own dynamic -lwinpthread after everything we pass.
# -lws2_32 is winsock, for net/net_udp.c. It goes AFTER the objects that reference it,
# which is what the rest of this line already assumes.
# -lwininet is the HTTP client's transport, and it sits inside the static island beside
# -lopengl32 for the same reason everything else here does: this linker resolves left to
# right, so a library named before the objects that need it resolves nothing. That slot is
# where the launcher's own working link line puts it.
LIBS="-lmingw32 -lSDL2main -Wl,-Bdynamic -lSDL2 -Wl,-Bstatic -lopengl32 -lwininet -lz -lws2_32 -static"

echo "== link"
# THE ICON AND THE VERSION BLOCK, compiled into cnc3d.exe as resources. One executable per
# platform, called C&C3D, with a game icon on it so it is obvious which file to run.
# tools/launchers/make_icon.py writes cnc3d.ico beside the .icns from ONE source image, so
# the Windows and macOS icons cannot drift apart.
#
# tools/win/cnc3d.rc is a TEMPLATE and is substituted here rather than compiled as it
# stands. The version it carries comes from tools/version.sh, which reads the VERSION file
# at the repo root, so Explorer's Properties tab and the menu plate cannot disagree and
# there is still only one place the number is written down.
#
# Only cnc3d.exe gets any of this. cnc_eyes.exe is the verification binary the gate suite
# drives on the Windows test machine, not something anybody double-clicks, and giving it the game's
# face would undo the point of the exercise.
RCOBJ=""
if [ -f "$ROOT/tools/win/cnc3d.rc" ] && [ -f "$ROOT/tools/launchers/cnc3d.ico" ]; then
    cp "$ROOT/tools/launchers/cnc3d.ico" "$OBJ/cnc3d.ico"
    VER_COMMA=$(sh "$ROOT/tools/version.sh" --rcversion)
    VER_STRING=$(cat "$ROOT/VERSION" | tr -d '[:space:]')
    sed -e "s/@VER_COMMA@/$VER_COMMA/g" -e "s/@VER_STRING@/$VER_STRING/g" \
        "$ROOT/tools/win/cnc3d.rc" > "$OBJ/cnc3d.rc"
    # An unsubstituted placeholder would reach windres and be reported as a syntax error
    # on a line number in a generated file, which is a long way from the cause. Say it here
    # instead. This is also the test that fails loudly if the template gains a placeholder
    # the substitution above does not know about.
    if grep -q '@VER_' "$OBJ/cnc3d.rc"; then
        echo "ERROR: tools/win/cnc3d.rc has a placeholder build-win.sh does not substitute:" >&2
        grep -n '@VER_' "$OBJ/cnc3d.rc" >&2
        exit 1
    fi
    # windres errors used to go to /dev/null, so a resource script that stopped compiling
    # reported itself as one warning line about a missing icon and the real message was
    # destroyed. The version block makes that worse, because a silent failure now costs the
    # version as well as the face. The output is kept and shown.
    if $HOST-windres "$OBJ/cnc3d.rc" -O coff -o "$OBJ/cnc3d_res.o" 2>"$OBJ/windres.err"; then
        RCOBJ="$OBJ/cnc3d_res.o"
        echo "== resources: icon + version $VER_STRING compiled into cnc3d.exe"
    else
        echo "WARNING: windres failed; cnc3d.exe will ship without its icon and version" >&2
        sed 's/^/  windres: /' "$OBJ/windres.err" >&2
    fi
else
    echo "WARNING: no cnc3d.ico (run tools/launchers/make_icon.py); shipping without an icon" >&2
fi

# THE UTF-8 MANIFEST, on both executables. The per-user directory SDL_GetPrefPath answers
# is UTF-8, and it is handed to the C runtime's fopen for the log, the saves and the
# world dump. Windows reads that path in the process code page, so a user name the code
# page cannot spell (a Polish, Cyrillic or Japanese one on most machines) gave the game a
# path it could not open: no per-user log, no saves, and an empty world where the dump
# was. The launcher has carried this manifest since it was written; the game did not.
# It makes the process code page UTF-8 on Windows 10 version 1903 and later, and earlier
# Windows ignores the element. Resource 1 of type 24 (RT_MANIFEST) is the one Windows
# reads when it starts a process. Compiled into its own object rather than folded into
# cnc3d.rc, because cnc_eyes.exe carries no icon and no version block and needs this.
cat > "$OBJ/game.manifest" <<'XML'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
    </windowsSettings>
  </application>
</assembly>
XML
printf '1 24 "game.manifest"\n' > "$OBJ/game_manifest.rc"
MANOBJ=""
if ( cd "$OBJ" && $HOST-windres game_manifest.rc -O coff -o game_manifest.o 2>"$OBJ/manifest.err" ); then
    MANOBJ="$OBJ/game_manifest.o"
    echo "== manifest: UTF-8 code page declared for cnc3d.exe and cnc_eyes.exe"
else
    echo "WARNING: windres could not compile the manifest; both executables will ship without the UTF-8 code page" >&2
    sed 's/^/  windres: /' "$OBJ/manifest.err" >&2
fi

# LARGE ADDRESS AWARE, on both. A 32-bit process is given 2 GB of address space unless
# its header says it can take more, and a mission start maps the engine, every pack, the
# GL driver and the audio into that space at once. The engine's pointer encoding goes
# through intptr_t and the same code already runs at 64-bit addresses on macOS, so
# nothing in it assumes the top bit is clear. With the flag a 64-bit Windows gives the
# process 4 GB. Read back with objdump -p: the Characteristics word carries 0x20.
$CXX -Wl,--large-address-aware -o "$OUT/cnc_eyes.exe" "$OBJ/cnc_eyes_main.o" $MANOBJ $COBJ $LIBDIRS $LIBS
$CXX -Wl,--large-address-aware -o "$OUT/cnc3d.exe"    "$OBJ/cnc3d.o" "$OBJ/cnc_eyes_lib.o" $RCOBJ $MANOBJ $COBJ $LIBDIRS $LIBS

cp "$SDL2_ROOT/bin/SDL2.dll" "$OUT/"

# THE CONNECTIVITY TOOL SHIPS WITH THE WINDOWS BUILD, and it has to, because the pairing
# it is most needed for is this machine against the Mac one. It links only the two net
# modules and needs no SDL and no engine, so it is a few kilobytes and cannot be affected
# by anything else in the build going wrong. Standalone, exactly like the gates: nothing
# links it into the game, and tools/win/check-sources.sh excludes it by name.
#
# A 16 MB STACK, because an LsState is a megabyte (LS_HISTORY x LS_MAX_SEATS turns of
# LS_TURN_BYTES) and netcheck keeps them as locals in main. macOS gives the main thread
# 8 MB and never noticed; Windows gives 2 MB, and the v0.6.4 netcheck.exe died on launch
# with STATUS_STACK_OVERFLOW (0xC00000FD) before printing a byte. Found on the Windows
# box, 3 Sep 2026. The lasting fix is to stop putting the scheduler state on the stack
# at all, which is the net/ author's call; this makes the shipped tool run today.
#
# ITS SOURCE LIST IS ITS OWN AND IS NOT CHECKED BY ANYTHING. The drift guard compares the
# two builds' GAME sources; netcheck is a standalone tool, so a file the Mac links into it
# and this line does not is caught by nothing until the link fails. That is how the room
# code broke this build: net/roomcode.c was added to the game's list on both platforms
# correctly, and netcheck, which prints and reads those codes, was linked without it here
# and kept linking on the Mac. A new net source that netcheck calls needs adding HERE as
# well as to sources.sh.
echo "== netcheck.exe"
$CC -std=gnu89 -O2 -g -Wall -Wl,--stack,16777216 -o "$OUT/netcheck.exe" \
    "$ROOT/net/netcheck.c" "$ROOT/net/lockstep.c" "$ROOT/net/net_udp.c" \
    "$ROOT/net/roomcode.c" -lws2_32

# THE HEADLESS BRAIN HOST AND THE TWO-BRAIN GATE, for the Windows half of Phase 0
# (docs/design-multiplayer.md section 10). Both are dependency-free C over LoadLibrary,
# compiled unedited from brain/host/, and both need the DLL built below beside them to
# be useful. Until 3 Sep 2026 the only Windows build rule for cnc_host.c was the Win98
# one, so the cross architecture determinism run could be captured on the Mac and not
# here, which is the one machine it needs. gnu99 for the same reason tools/win98/build.sh
# gives: loop variables in for statements and _Static_assert on the struct mirrors.
# Standalone, like netcheck: nothing links them into the game, and brain/host/* is
# excluded from tools/win/check-sources.sh by directory.
echo "== cncbrain.exe, cnc_twobrain.exe"
$CC -std=gnu99 -O2 -g -Wall -I"$ROOT/brain/host" -o "$OUT/cncbrain.exe" "$ROOT/brain/host/cnc_host.c"
$CC -std=gnu99 -O2 -g -Wall -I"$ROOT/brain/host" -o "$OUT/cnc_twobrain.exe" "$ROOT/brain/host/cnc_twobrain.c"

if [ $DO_BRAIN -eq 1 ]; then
    echo "== brain (TiberianDawn.dll)"
    # -fpermissive: GCC rejects the Remaster header's MS-style anonymous unions with
    # named struct types, and one extra qualification on a constructor. Both are ISO
    # conformance complaints about name lookup with no effect on generated code, and
    # clang accepts them silently on the Mac side. The one -fpermissive diagnostic that
    # WOULD have mattered, the pointer-to-unsigned-int truncation, does not arise at all
    # at 32 bits, which is why this is a safe flag here and would not have been at 64.
    tc="$ROOT/brain/vanilla/cmake/$(echo $HOST | cut -d- -f1)-mingw-w64-toolchain.cmake"
    mkdir -p brain/vanilla/build-win
    (cd brain/vanilla/build-win && \
     cmake .. -DCMAKE_TOOLCHAIN_FILE="$tc" \
              -DCMAKE_CXX_FLAGS="-fms-extensions -fpermissive -flifetime-dse=1" \
              -DCMAKE_SHARED_LINKER_FLAGS="-static" \
              -DBUILD_REMASTERTD=ON -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=OFF \
              -DBUILD_REMASTERRA=OFF -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF >/dev/null && \
     cmake --build . --target TiberianDawn -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null) \
        || { echo "ERROR: the Windows brain did not build. Re-run with the cmake output" >&2
             echo "       visible, or delete brain/vanilla/build-win and try again: a cache" >&2
             echo "       generated under a different path refuses every later configure." >&2
             exit 1; }
    # THE COPY IS CONDITIONAL, and it used to be unconditional.
    #
    # Both cmake steps send their output to /dev/null and sit inside a subshell, so a
    # configure that refused left no message; the cp beneath it then copied whatever
    # TiberianDawn.dll happened to be in the build directory from a previous run, and the
    # script printed the file under "built:" as though it had just made it. A working copy
    # cloned from another path hits exactly that, because a CMake cache records the
    # directory it was generated in and refuses to be reused from anywhere else. The
    # result is a Windows package carrying a renderer from this commit and a brain from
    # some earlier one, which is the precise failure the both-platforms rule exists to
    # prevent, reported as success.
    cp brain/vanilla/build-win/TiberianDawn.dll "$OUT/"
fi

echo
echo "built:"
for f in "$OUT"/*.exe "$OUT"/*.dll; do [ -f "$f" ] && echo "  $f"; done
