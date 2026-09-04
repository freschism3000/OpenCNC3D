#!/bin/sh
# Build the Tiberian Dawn brain as a Windows 98 DLL.
#
# Same GPL source and the same cmake targets the Mac and modern Windows builds use. The
# only differences are the toolchain file (tools/win98/toolchain-win98.cmake, which
# carries the msvcrt link recipe) and the build directory, which lives under build/ so
# that .gitignore already keeps it out of history.
#
#   tools/win98/build-brain.sh          -> build/win98/TiberianDawn.dll
#
# -DWIN9X=ON IS THE WHOLE REASON THE SCENARIO LOADS.
#
# Vanilla Conquer already has first-class Windows 95/98/ME support (CMakeLists.txt:29) and
# nobody had switched it on. Without it, common/CMakeLists.txt:191 builds the whole common
# library with UNICODE and _WIN32_WINNT=0x501, which makes rawfile.cpp's raw_fopen expand
# to _tfopen -> _wfopen. On Windows 98 the wide entry points are stubs that fail, so the
# brain loaded and initialised perfectly and then could not open a single file:
#
#   [event] DEBUG_PRINT   Failed to load scenario file
#
# With WIN9X=ON it is _WIN32_WINNT=0x400 and plain ANSI fopen. This is upstream's own
# switch, so it costs us no patch to brain/vanilla, which repository rules keeps our hands out of.
#
# -fms-extensions -fpermissive -flifetime-dse=1 are NOT ours to choose: they are what the
# modern Windows build already passes (tools/win/build-win.sh:147-151) and each one is
# argued for there. Changing them would mean building a different brain from the one the
# other platforms run, which is the one thing this must not do.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/win98"
BUILD="$OUT/brain"
mkdir -p "$BUILD"

[ -f "$ROOT/brain/vanilla/CMakeLists.txt" ] || {
    echo "brain/vanilla is missing. It is COMMITTED source, not a clone target." >&2
    echo "See brain/README.md: cloning over it has broken CI twice." >&2
    exit 1
}

echo "== brain for Windows 98 (TiberianDawn.dll, i686, msvcrt)"
cd "$BUILD"
cmake "$ROOT/brain/vanilla" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/win98/toolchain-win98.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-fms-extensions -fpermissive -flifetime-dse=1" \
    -DWIN9X=ON \
    -DBUILD_REMASTERTD=ON -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=OFF \
    -DBUILD_REMASTERRA=OFF -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF > "$OUT/brain-cmake.log" 2>&1
cmake --build . --target TiberianDawn -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" \
    > "$OUT/brain-build.log" 2>&1

DLL=$(find "$BUILD" -name TiberianDawn.dll | head -1)
[ -n "$DLL" ] || { echo "no TiberianDawn.dll produced; see $OUT/brain-build.log" >&2; exit 1; }
cp "$DLL" "$OUT/TiberianDawn.dll"
echo "   built  : $OUT/TiberianDawn.dll ($(wc -c < "$OUT/TiberianDawn.dll" | tr -d ' ') bytes)"

# The same acceptance test the exe gets: if it imports a DLL Win98 lacks, it cannot load,
# and finding that out here beats finding it out in a message box on the box.
DLLS=$(i686-w64-mingw32-objdump -p "$OUT/TiberianDawn.dll" | awk '/DLL Name:/ { print $NF }')
[ -n "$DLLS" ] || { echo "REFUSING: no import table readable" >&2; exit 1; }
BAD=$(echo "$DLLS" | grep -viE '^(KERNEL32|USER32|GDI32|WINMM|MSVCRT|ADVAPI32|SHELL32|OLE32|OLEAUT32)\.dll$' || true)
if [ -n "$BAD" ]; then
    echo "REFUSING: brain imports a DLL Windows 98 does not have:" >&2
    echo "$BAD" >&2
    exit 1
fi
echo "   imports: $(echo "$DLLS" | tr '\n' ' ')"
echo "   OK"
