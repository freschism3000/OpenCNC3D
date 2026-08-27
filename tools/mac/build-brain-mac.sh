#!/bin/sh
# Build THE BRAIN for macOS: brain/vanilla -> TiberianDawn.dylib.
#
#   tools/mac/build-brain-mac.sh [DEST ...]
#
# with no argument it writes game/TiberianDawn.dylib, the copy the play folder is
# assembled from; each extra argument is another folder to drop the finished library in.
#
# WHY THIS SCRIPT EXISTS. The Windows half of the brain has had a driver since the cross
# build was written (tools/win/build-win.sh, the DO_BRAIN block), but the macOS half was
# only ever a set of commands in brain/README.md and a copy of the same commands in the
# CI workflow, run by hand and then hand copied into game/ and playable/. Two things
# followed from that. Nothing could set a compiler flag for the shipped library, and the
# library that shipped was whatever was last copied rather than whatever the checkout
# says. The first one is what this script was written for.
#
# WHAT IT SETS AND WHY: CMAKE_OSX_DEPLOYMENT_TARGET, from the one number in
# tools/mac/deployment-target.sh. Without it the brain was stamped LC_BUILD_VERSION
# minos 26.0 exactly like the two binaries beside it, and all three load in ONE process:
# a cnc3d fixed to 14.0 that dlopens a 26.0 TiberianDawn.dylib still fails on any Mac
# below macOS 26, it just fails a few milliseconds later. It is all three or none.
#
# Passed explicitly rather than left to the MACOSX_DEPLOYMENT_TARGET environment
# variable, because CMake only reads that variable when it INITIALISES the cache. A
# build-native directory configured earlier already holds an empty
# CMAKE_OSX_DEPLOYMENT_TARGET, and an inherited environment would not have overridden it.
# That is the silent-half-fix this whole item is about.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

. tools/mac/deployment-target.sh

[ -f brain/vanilla/CMakeLists.txt ] || {
    echo "no brain checkout at brain/vanilla. It is COMMITTED SOURCE (a project rule;" >&2
    echo "brain/README.md), not something to clone, so a missing one means a broken" >&2
    echo "checkout rather than a missing step." >&2
    exit 1
}

BUILD=brain/vanilla/build-native
echo "== brain (TiberianDawn.dylib), deployment target $CNC3D_MACOS_MIN"
cmake -S brain/vanilla -B "$BUILD" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="$CNC3D_MACOS_MIN" \
      -DBUILD_REMASTERTD=ON -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=OFF \
      -DBUILD_REMASTERRA=OFF -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF >/dev/null
cmake --build "$BUILD" --target TiberianDawn \
      -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null

LIB="$BUILD/tiberiandawn/TiberianDawn.dylib"
[ -f "$LIB" ] || {
    echo "cmake reported success but $LIB is not there." >&2
    echo "Vanilla Conquer's add_feature_info(RemasterTD ...) has a stray comma, so cmake" >&2
    echo "can print the target as disabled while building it; check the target list." >&2
    exit 1
}

# PROVE THE FLOOR, do not assume it. Changing CMAKE_OSX_DEPLOYMENT_TARGET in an existing
# cache is exactly the case where a stale object file can survive a rebuild, and a brain
# that is still 26.0 fails in dyld on the machine that downloads it rather than here.
MIN=$(otool -l "$LIB" | awk '/LC_BUILD_VERSION/{f=1} f&&/^ *minos/{print $2; exit}')
[ "$MIN" = "$CNC3D_MACOS_MIN" ] || {
    echo "the brain came out with minos ${MIN:-none}, not $CNC3D_MACOS_MIN." >&2
    echo "Delete $BUILD and run this again: a cache configured without a deployment" >&2
    echo "target keeps the old compile flags on objects it thinks are up to date." >&2
    exit 1
}
echo "   minos $MIN"

# game/ IS ALWAYS WRITTEN, and each argument is an ADDITIONAL folder. That is what the
# header above has always promised -- "with no argument it writes game/TiberianDawn.dylib
# ... each extra argument is another folder to drop the finished library in" -- but the
# code used to read "$#" > 0 and REPLACE the list, so passing any destination silently
# skipped game/. On 24 Aug 2026 that shipped a freshly built brain to playable/ and app/
# while game/ kept a copy four hours old, and G30 caught it as a stale binary. Nobody
# calling this wants game/ left behind; the code was the bug, not the comment.
DESTS="game $*"
for d in $DESTS; do
    [ -d "$d" ] || { echo "no such folder: $d" >&2; exit 1; }
    cp "$LIB" "$d/TiberianDawn.dylib"
    echo "   -> $d/TiberianDawn.dylib"
done
