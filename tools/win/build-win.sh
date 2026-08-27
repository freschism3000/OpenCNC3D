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
# larger address space.
#
# WHY NO SOURCE WAS EDITED TO MAKE THIS WORK. Every file except game/cnc_eyes.cpp already
# carried its own `#ifdef __APPLE__` GL guard. The renderer did not, and it is the file
# most likely to be open elsewhere, so compat/win/ supplies the three macOS-only
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
# so the build would have died on the target machine with a missing-DLL box and nothing else
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
LIBS="-lmingw32 -lSDL2main -Wl,-Bdynamic -lSDL2 -Wl,-Bstatic -lopengl32 -lz -static"

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
# drives on Windows, not something anybody double-clicks, and giving it the game's
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

$CXX -o "$OUT/cnc_eyes.exe" "$OBJ/cnc_eyes_main.o" $COBJ $LIBDIRS $LIBS
$CXX -o "$OUT/cnc3d.exe"    "$OBJ/cnc3d.o" "$OBJ/cnc_eyes_lib.o" $RCOBJ $COBJ $LIBDIRS $LIBS

cp "$SDL2_ROOT/bin/SDL2.dll" "$OUT/"

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
