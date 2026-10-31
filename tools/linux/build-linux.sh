#!/bin/sh
# Build OpenCNC3D on Linux: the game, the renderer, the launcher and both brains.
#
#   tools/linux/build-linux.sh              -> build/linux/
#   tools/linux/build-linux.sh --no-brain   -> skip the brains (they change rarely)
#
# Needs a C/C++ compiler, CMake, Python 3, and the SDL2, OpenGL, zlib and libcurl
# development packages. On Debian/Ubuntu:
#
#   sudo apt install build-essential cmake python3 \
#       libsdl2-dev libgl1-mesa-dev zlib1g-dev libcurl4-openssl-dev
#
# The binaries land in build/linux/. They still need your own game data before a
# mission will start; see BUILDING.md. The brain is libTiberianDawn.so (CMake drops
# the "lib" prefix) and is copied beside the executables, which is where the game
# looks last.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

if [ -z "$OPENCNC3D_SKIP_VERSION_HEADER" ] || [ ! -f "$ROOT/game/opencnc3d_build.h" ]; then
    sh "$ROOT/tools/version.sh" --header "$ROOT/game/opencnc3d_build.h"
fi

OUT="$ROOT/build/linux"
DO_BRAIN=1
[ "$1" = "--no-brain" ] && DO_BRAIN=0

command -v gcc >/dev/null 2>&1 || { echo "gcc is not installed" >&2; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo "g++ is not installed" >&2; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "cmake is not installed" >&2; exit 1; }
command -v sdl2-config >/dev/null 2>&1 || { echo "sdl2-config is not installed (libsdl2-dev)" >&2; exit 1; }
echo '#include <GL/gl.h>' | gcc -E - >/dev/null 2>&1 \
    || { echo "OpenGL headers are missing (libgl1-mesa-dev)" >&2; exit 1; }
echo '#include <zlib.h>' | gcc -E - >/dev/null 2>&1 \
    || { echo "zlib headers are missing (zlib1g-dev)" >&2; exit 1; }
echo '#include <curl/curl.h>' | gcc -E - >/dev/null 2>&1 \
    || { echo "libcurl headers are missing (libcurl4-openssl-dev)" >&2; exit 1; }

. tools/win/sources.sh

HDR=""
for c in brain/vanilla/tiberiandawn; do
    [ -f "$c/dllinterface.h" ] && HDR="$c" && break
done
[ -n "$HDR" ] || { echo "cannot find dllinterface.h under brain/vanilla" >&2; exit 1; }

CC=gcc
CXX=g++
OBJ="$OUT/obj"
mkdir -p "$OBJ"

# compat/linux answers <OpenGL/gl.h>, which cnc_eyes.cpp includes by the Apple name.
# It has to come first. game/ is next for the same reason as the other builds:
# game/dosbar.c is the copy the menu is compiled against.
INC="-I$ROOT/compat/linux -I$ROOT/game -I$ROOT/menu -I$ROOT/video -I$ROOT/audio \
     $(sdl2-config --cflags)"
CDEFS="-DGL_SILENCE_DEPRECATION"
[ -n "$OPENCNC3D_COOKED" ] && CDEFS="$CDEFS -DOPENCNC3D_COOKED=1"
PATHMAP="-ffile-prefix-map=$ROOT=."
CDEFS="$CDEFS $PATHMAP"

# Same dialect split as tools/win/build-win.sh. Apple clang accepts C99 under gnu89;
# GCC does not, so the files that declare variables in for-loops are gnu99.
COBJ=""
compile_c() {
    std=$1; shift
    for f in "$@"; do
        o="$OBJ/$(basename "$f" .c).o"
        $CC -std=$std -O2 -g -Wall $CDEFS $INC -c "$f" -o "$o"
        COBJ="$COBJ $o"
    done
}

echo "== C sources"
AUDIO_C89=$(echo $WIN_AUDIO_C | tr ' ' '\n' | grep -v audio_sdl.c | tr '\n' ' ')
compile_c c89   $WIN_GAME_C $AUDIO_C89
compile_c gnu99 audio/audio_sdl.c $WIN_MENU_C $WIN_VIDEO_C $WIN_APP_C
compile_c c89   net/lockstep.c
compile_c gnu89 net/net_udp.c
compile_c gnu89 net/netmatch.c
compile_c gnu89 net/netbeacon.c
compile_c gnu89 net/roomcode.c
compile_c gnu99 $WIN_HTTP_C

echo "== enhanced menu"
$CXX -std=c++14 -O2 -g $CDEFS $INC -c "$WIN_MENU_CPP" -o "$OBJ/enhanced_menu.o"
COBJ="$COBJ $OBJ/enhanced_menu.o"

# dllinterface.h uses Microsoft anonymous unions and __declspec. cnc_eyes.cpp
# blanks __declspec itself; -fpermissive is what lets GCC accept the unions,
# the same way the Windows build does.
# -fdeclspec is Clang's spelling. GCC takes __declspec through -fms-extensions,
# and cnc_eyes.cpp also blanks the macro itself before including the brain header.
# __int64 is kept as its own quoted argument: it has to be one argv element
# ("-D__int64=long long"), and an unquoted variable would split on the space.
CXXFLAGS="-std=c++14 -O2 -g -fms-extensions -fpermissive"
INT64='-D__int64=long long'

echo "== renderer"
$CXX $CXXFLAGS "$INT64" -DOPENCNC3D_NO_MAIN \
     $CDEFS -include "$ROOT/game/opencnc3d_build.h" $INC -I"$HDR" \
     -c "$WIN_EYES_CPP" -o "$OBJ/cnc_eyes_lib.o"
$CXX $CXXFLAGS "$INT64" \
     $CDEFS -include "$ROOT/game/opencnc3d_build.h" $INC -I"$HDR" \
     -c "$WIN_EYES_CPP" -o "$OBJ/cnc_eyes_main.o"
$CXX $CXXFLAGS "$INT64" \
     $CDEFS $INC -I"$HDR" -c "$WIN_APP_CPP" -o "$OBJ/opencnc3d.o"

# SDL2, OpenGL, zlib (the pack reader), libcurl (the game list), libdl (the brain).
LIBS="$(sdl2-config --libs) -lGL -lz -lcurl -ldl -lm"

echo "== link"
$CXX -o "$OUT/cnc_eyes" "$OBJ/cnc_eyes_main.o" $COBJ $LIBS
$CXX -o "$OUT/opencnc3d" "$OBJ/opencnc3d.o" "$OBJ/cnc_eyes_lib.o" $COBJ $LIBS

echo "== netcheck"
$CC -std=gnu89 -O2 -g -Wall $PATHMAP -o "$OUT/netcheck" \
    "$ROOT/net/netcheck.c" "$ROOT/net/lockstep.c" "$ROOT/net/net_udp.c" \
    "$ROOT/net/roomcode.c"

echo "== launcher"
sh "$ROOT/tools/launcher/make-config.sh"
(
    cd "$ROOT/launcher"
    # shellcheck disable=SC1091
    . ./sources.sh
    # gnu99: the launcher is written the way Apple clang accepts under gnu89,
    # which GCC rejects (declarations inside for, and mixed declarations).
    gcc -O2 -g -Wall -std=gnu99 -I. -I../game -I../menu -I../video \
        -DGL_SILENCE_DEPRECATION $PATHMAP \
        -o "$OUT/opencnc3d-launcher" $LAUNCHER_SOURCES \
        $(sdl2-config --cflags --libs) -lGL -lcurl -lz -lm
)

if [ $DO_BRAIN -eq 1 ]; then
    echo "== brains (TiberianDawn.so + RedAlert.so)"
    command -v cmake >/dev/null
    BUILD=brain/vanilla/build-native
    # A cache from another machine or another cmake refuses every later configure
    # and then a copy would ship the previous library. Throw that cache away.
    if [ -f "$BUILD/CMakeCache.txt" ]; then
        cached=$(sed -n 's/^CMAKE_COMMAND:INTERNAL=//p' "$BUILD/CMakeCache.txt")
        if [ "$cached" != "$(command -v cmake)" ]; then
            echo "   $BUILD was configured by another cmake: configuring it fresh"
            rm -rf "$BUILD"
        fi
    fi
    JOBS=$(nproc 2>/dev/null || echo 4)
    cmake -S brain/vanilla -B "$BUILD" \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DCMAKE_CXX_FLAGS="-fms-extensions -fpermissive -flifetime-dse=1 $PATHMAP" \
          -DCMAKE_C_FLAGS="$PATHMAP" \
          -DBUILD_REMASTERTD=ON -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=OFF \
          -DBUILD_REMASTERRA=ON -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF
    cmake --build "$BUILD" --target TiberianDawn RedAlert -j"$JOBS"
    TD_LIB="$BUILD/tiberiandawn/TiberianDawn.so"
    RA_LIB="$BUILD/redalert/RedAlert.so"
    for lib in "$TD_LIB" "$RA_LIB"; do
        [ -f "$lib" ] || { echo "cmake reported success but $lib is not there" >&2; exit 1; }
    done
    cp "$TD_LIB" "$OUT/TiberianDawn.so"
    cp "$RA_LIB" "$OUT/RedAlert.so"
    cp "$TD_LIB" "$ROOT/game/TiberianDawn.so"
fi

echo
echo "built:"
for f in "$OUT/opencnc3d" "$OUT/cnc_eyes" "$OUT/opencnc3d-launcher" \
         "$OUT/netcheck" "$OUT/TiberianDawn.so" "$OUT/RedAlert.so"; do
    [ -f "$f" ] && echo "  $f"
done
