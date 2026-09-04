#!/bin/sh
# Cross compile the Windows launcher, from the Mac, with the toolchain the game
# already uses.
#
#   tools/win/build-launcher-win.sh          -> build/win/C&C3D.exe
#
# It reads launcher/sources.sh, the SAME list launcher/build.sh reads, so a file
# added on one platform cannot be forgotten on the other. That is repository rules rule 4
# expressed by construction rather than by a guard script: there is one list.
#
# THREE THINGS ARE DIFFERENT FROM THE GAME'S OWN WINDOWS BUILD, each for a reason:
#
#  1. -lwininet instead of libcurl. WinINet is a system DLL, so the launcher needs
#     no bundled SSL stack, trusts the certificate store Windows already trusts,
#     and adds nothing for the player to install. lnet.c holds both backends.
#  2. -mwindows, so double-clicking the launcher does NOT open a black console
#     window behind the dialog. The game is built console-subsystem on purpose
#     (its diagnostics and the whole gate suite talk on stdout); a launcher has no
#     such traffic in the case that matters, and launcher.c attaches to the parent
#     console when there is one, so --check from a cmd prompt still prints.
#  3. The icon and version resource are compiled in from the same tools/win/cnc3d.rc
#     template the game uses, so the file Explorer shows carries the eagle and the
#     build number.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

HOST=${CNC3D_WIN_HOST:-i686-w64-mingw32}
SDK="${CNC3D_WINSDK:-$HOME/.cnc3d-winsdk}"
OUT="$ROOT/build/win"
OBJ="$OUT/obj-launcher"

SDL2_ROOT=$(ls -d "$SDK"/SDL2-*/"$HOST" 2>/dev/null | head -1)
[ -n "$SDL2_ROOT" ] || { echo "no SDL2 for $HOST in $SDK. Run tools/win/setup-toolchain.sh" >&2; exit 1; }
[ -f "$SDK/lib/$HOST/libz.a" ] || { echo "no zlib for $HOST in $SDK. Run tools/win/setup-toolchain.sh" >&2; exit 1; }
command -v $HOST-gcc >/dev/null 2>&1 || { echo "no $HOST-gcc on PATH (brew install mingw-w64)" >&2; exit 1; }

# The update host, stamped in, exactly as the Mac build does it.
tools/launcher/make-config.sh

. launcher/sources.sh

mkdir -p "$OBJ"
CC=$HOST-gcc

INC="-I$ROOT/launcher -I$ROOT/game -I$ROOT/menu -I$ROOT/video \
     -I$SDL2_ROOT/include/SDL2 -I$SDK/include"
LIBDIRS="-L$SDL2_ROOT/lib -L$SDK/lib/$HOST"
CFLAGS="-O2 -Wall -Wextra -std=gnu89 $INC"

echo "== compile"
# From inside launcher/, because the shared source list is written relative to it
# (../game/dosbar.c and the rest). Objects go to absolute paths so the build tree
# stays where build-win.sh puts everything else.
OBJS=""
for src in $LAUNCHER_SOURCES; do
    o="$OBJ/$(echo "$src" | sed 's|[./]|_|g').o"
    ( cd "$ROOT/launcher" && $CC $CFLAGS -c "$src" -o "$o" )
    OBJS="$OBJS $o"
done

echo "== resources"
# Same template, same substitution, same single source of truth for the number as
# tools/win/build-win.sh. An unsubstituted @VER@ reaching windres is a syntax
# error rather than a silent blank, which is why they are checked.
RCOBJ=""
if [ -f tools/launchers/cnc3d.ico ]; then
    VER_COMMA=$(sh "$ROOT/tools/version.sh" --rcversion)
    VER_STRING=$(tr -d '[:space:]' < "$ROOT/VERSION")
    sed -e "s/@VER_COMMA@/$VER_COMMA/g" -e "s/@VER_STRING@/$VER_STRING/g" \
        tools/win/cnc3d.rc > "$OBJ/cnc3d.rc"
    # The EXACT placeholders, not the string "@VER": cnc3d.rc's own header comment
    # contains the phrase "the two @VER placeholders", so a loose match fails every
    # build on a comment that is doing its job.
    grep -qE '@VER_(COMMA|STRING)@' "$OBJ/cnc3d.rc" \
        && { echo "a version placeholder was not substituted" >&2; exit 1; }
    cp tools/launchers/cnc3d.ico "$OBJ/"
    if $HOST-windres "$OBJ/cnc3d.rc" -O coff -o "$OBJ/res.o" 2>"$OBJ/windres.err"; then
        RCOBJ="$OBJ/res.o"
        echo "   icon + version $VER_STRING compiled in"
    else
        echo "WARNING: windres failed; the launcher will ship without its icon" >&2
        sed 's/^/  windres: /' "$OBJ/windres.err" >&2
    fi
else
    echo "WARNING: tools/launchers/cnc3d.ico is missing; no icon" >&2
fi

echo "== link"
# The -Bdynamic island around -lSDL2 is the same trick build-win.sh documents at
# length: -static on its own picks the static libSDL2.a, which then wants every
# Windows multimedia import SDL normally resolves inside its own DLL. SDL2 stays
# dynamic and everything else, including winpthread, folds into the binary, so
# SDL2.dll is the only file that has to travel beside the .exe.
LIBS="-lmingw32 -lSDL2main -Wl,-Bdynamic -lSDL2 -Wl,-Bstatic -lopengl32 -lwininet -lz -static"

$CC -mwindows -o "$OUT/C&C3D.exe" $OBJS $RCOBJ $LIBDIRS $LIBS

cp "$SDL2_ROOT/bin/SDL2.dll" "$OUT/" 2>/dev/null || true
ls -la "$OUT/C&C3D.exe"
echo "built $OUT/C&C3D.exe"
