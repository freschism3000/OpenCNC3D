#!/bin/sh
# One-time setup of the 3dfx Glide SDK for the Windows 98 Tier 1 build.
#
# Glide has no redistributable SDK any more, so this assembles one from two sources that
# both still exist, and pins nothing to a temporary directory the way the earlier Game
# Browser work did (its SDK path pointed into /private/tmp and evaporated).
#
#   1. HEADERS come from sezero/glide, the maintained mirror of 3dfx's own GPL release.
#   2. The IMPORT LIBRARY is built from the REAL glide2x.dll off the Voodoo 2 machine.
#      That matters: it guarantees we link against exactly the entry points that
#      machine's driver exports, rather than against a header's idea of them.
#
#   tools/win98/setup-glide.sh          -> ~/.cnc3d-win98sdk/glide
#
# Safe to re-run. Needs glide2x.dll; it looks in the usual places and says where to get
# one if it cannot find it.
set -e

SDK="${CNC3D_GLIDE_SDK:-$HOME/.cnc3d-win98sdk/glide}"
INC="$SDK/include"
LIB="$SDK/lib"
mkdir -p "$INC" "$LIB"

RAW=https://raw.githubusercontent.com/sezero/glide/master

fetch() {   # fetch <remote-path> <local-name>
    if [ -f "$INC/$2" ]; then return 0; fi
    echo "   header : $2"
    curl -fsSL "$RAW/$1" -o "$INC/$2.tmp"
    # A 404 from raw.githubusercontent is an HTML page, not a header, and the only
    # symptom later is a parse error a hundred lines into a file that looks fine.
    if head -c 200 "$INC/$2.tmp" | grep -qi '<!DOCTYPE\|<html'; then
        echo "DOWNLOAD IS NOT A HEADER: $1" >&2
        rm -f "$INC/$2.tmp"; exit 1
    fi
    mv "$INC/$2.tmp" "$INC/$2"
}

echo "== 3dfx Glide SDK -> $SDK"
for h in glide.h glidesys.h glideutl.h gsstdef.h; do
    fetch "glide2x/sst1/glide/src/$h" "$h"
done
fetch swlibs/fxmisc/3dfx.h        3dfx.h
fetch swlibs/fxmisc/fxos.h        fxos.h
fetch swlibs/fxmisc/fxver.h       fxver.h
fetch swlibs/fxmisc/fxdll.h       fxdll.h
# sst1vid.h carries the GR_RESOLUTION_* constants and lives under the SST-1 init tree,
# not beside glide.h. Found by walking the repo rather than guessing: the obvious two
# paths are both 404s and a 404 from raw.githubusercontent is an HTML page.
fetch glide2x/sst1/init/sst1vid.h sst1vid.h

# ---------------------------------------------------------------------------
# The import library, from the box's own driver.
# ---------------------------------------------------------------------------
DLL=""
for c in "$CNC3D_GLIDE_DLL" ./glide2x.dll; do
    [ -n "$c" ] && [ -f "$c" ] && DLL="$c" && break
done
if [ -z "$DLL" ]; then
    echo "no glide2x.dll found." >&2
    echo "Copy it off the Win98 box (C:\\WINDOWS\\SYSTEM\\GLIDE2X.DLL) to the share," >&2
    echo "or set CNC3D_GLIDE_DLL=/path/to/glide2x.dll and re-run." >&2
    exit 1
fi
echo "   driver : $DLL"

if [ ! -f "$LIB/libglide2x.a" ]; then
    # The DLL exports stdcall names ALREADY decorated and underscored, e.g.
    # _grDrawTriangle@12, which is exactly what GCC emits for __stdcall. So the names go
    # into the .def verbatim and dlltool is told not to add an underscore of its own.
    i686-w64-mingw32-objdump -p "$DLL" \
        | sed -n '/\[Ordinal\/Name Pointer\] Table/,$p' \
        | awk '/\[[ 0-9]+\]/ { print $NF }' \
        | grep -E '^_' > "$LIB/glide2x.exports"
    n=$(wc -l < "$LIB/glide2x.exports" | tr -d ' ')
    [ "$n" -gt 100 ] || { echo "only $n exports read from $DLL; that is not a Glide driver" >&2; exit 1; }
    { echo "LIBRARY glide2x.dll"; echo "EXPORTS"; cat "$LIB/glide2x.exports"; } > "$LIB/glide2x.def"
    i686-w64-mingw32-dlltool --no-leading-underscore \
        -d "$LIB/glide2x.def" -D glide2x.dll -l "$LIB/libglide2x.a"
    echo "   import : libglide2x.a from $n exports"
fi

echo "   OK  (CFLAGS: -I$INC   LDFLAGS: -L$LIB -lglide2x)"
