#!/bin/sh
# Build THE XL BRAIN for macOS: brain/xl -> TiberianDawnXL.dylib.
#
#   tools/mac/build-brain-xl-mac.sh [DEST ...]
#
# The Enhanced-only fork (docs/design-xl-brain.md): widened COORDINATE/CELL core
# types on a 1024-cell stride, built from the committed source copy at brain/xl.
# Mirrors tools/mac/build-brain-mac.sh deliberately, including the deployment-
# target assertion: all binaries that load in one process need the same floor,
# and the XL dylib will load beside cnc_eyes and the classic brain.
#
# With no argument the library stays in the build tree
# (brain/xl/build-native/tiberiandawn/TiberianDawnXL.dylib -- one of find_brain's
# XL candidates); each argument is a folder to drop a copy in. Unlike the classic
# script this does NOT default to game/: the classic brain is what the play folder
# is assembled around today, and the XL dylib joins release assembly in a later
# phase, so an implicit copy would only create a stale-file surface early.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

. tools/mac/deployment-target.sh

[ -f brain/xl/CMakeLists.txt ] || {
    echo "no XL brain checkout at brain/xl. It is COMMITTED SOURCE (the fork of" >&2
    echo "brain/vanilla at tag brain-xl-forkpoint, docs/design-xl-brain.md), so a" >&2
    echo "missing one means a broken checkout rather than a missing step." >&2
    exit 1
}

BUILD=brain/xl/build-native
echo "== XL brain (TiberianDawnXL.dylib), deployment target $CNC3D_MACOS_MIN"
cmake -S brain/xl -B "$BUILD" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="$CNC3D_MACOS_MIN" >/dev/null
cmake --build "$BUILD" --target TiberianDawnXL \
      -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null

LIB="$BUILD/tiberiandawn/TiberianDawnXL.dylib"
[ -f "$LIB" ] || {
    echo "cmake reported success but $LIB is not there; check the target list." >&2
    exit 1
}

# PROVE THE FLOOR, do not assume it -- a stale cache keeps old compile flags on
# objects it thinks are up to date, exactly as the classic script's comment records.
MIN=$(otool -l "$LIB" | awk '/LC_BUILD_VERSION/{f=1} f&&/^ *minos/{print $2; exit}')
[ "$MIN" = "$CNC3D_MACOS_MIN" ] || {
    echo "the XL brain came out with minos ${MIN:-none}, not $CNC3D_MACOS_MIN." >&2
    echo "Delete $BUILD and run this again." >&2
    exit 1
}
echo "   minos $MIN"

for d in "$@"; do
    [ -d "$d" ] || { echo "no such folder: $d" >&2; exit 1; }
    cp "$LIB" "$d/TiberianDawnXL.dylib"
    echo "   -> $d/TiberianDawnXL.dylib"
done
echo "   $LIB"
