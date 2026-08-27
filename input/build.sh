#!/bin/sh
# CNC3D headless scripted-input harness -- macOS build.
#
# -fms-extensions -fdeclspec -D__int64="long long"
#     the brain's dllinterface.h is Win32-flavoured C++; these make it parse on clang.
# The brain itself is dlopen'd at runtime, so only its header is needed here.
set -e
cd "$(dirname "$0")"

HDR=""
for c in ../brain/vanilla/tiberiandawn ../../../brain/vanilla/tiberiandawn; do
    [ -f "$c/dllinterface.h" ] && HDR="$c" && break
done
[ -n "$HDR" ] || { echo "cannot find dllinterface.h" >&2; exit 1; }

clang++ -std=c++17 -O2 -g \
    -fms-extensions -fdeclspec -D__int64="long long" \
    -o inputtest inputtest.cpp -I"$HDR"

echo "built ./inputtest"
echo
echo "proof run:"
echo "  ./inputtest \$BRAIN ../brain/missions/ SCB01EA 1 ../brain/stubcontent/ \\"
echo "     \"view; select unit 2; probe 57 26; cmd 57 26; path 600\""
