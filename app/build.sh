#!/bin/sh
# Build THE PROGRAM: menu and game in one binary, one window, one GL context.
#
#   ./build.sh          -> ./cnc3d
#
# Notes that matter for the Win98 port:
#
#  * dosbar.c is compiled ONCE, from ../game. game/dosbar.c is a strict superset of
#    sidebar/dosbar.c (the NOD radar bezel, the disabled Repair/Sell look, and
#    db_draw_credits_tab) and its header carries the extern "C" guards a C++ caller
#    needs. Linking the two copies into one binary would be a duplicate symbol; using
#    the sidebar copy would compile dosmenu.c against a DB_State that has three
#    fewer fields than the one dosbar.c writes. So -I../game comes FIRST.
#  * -DCNC3D_NO_MAIN compiles cnc_eyes.cpp as a library. Everything else about it is
#    unchanged, which is why ../game/build.sh still produces the standalone renderer
#    all five verification gates run against.
#  * GL_SILENCE_DEPRECATION: macOS deprecated the fixed function pipeline. We use it
#    on purpose; on the Voodoo 2 target it is the only thing there is.
set -e
cd "$(dirname "$0")"

# THE OLDEST macOS THIS BINARY RUNS ON. Without it clang stamps LC_BUILD_VERSION minos
# with whatever SDK it finds (whatever is installed), and dyld refuses a Mach-O whose minos
# is newer than the running system, so the build is rejected on every Mac below macOS 26
# before main() runs. tools/mac/deployment-target.sh carries the full account and the
# reason the floor is 14.0 rather than lower. Sourced, so there is one number.
. ../tools/mac/deployment-target.sh

# THE BUILD NUMBER, regenerated before anything compiles. tools/version.sh reads the
# VERSION file at the repo root and writes game/cnc3d_build.h; nothing else in the tree
# is allowed to carry a version string, so there is no second copy to forget to bump.
# tools/release.sh has already written the header with the bare release number by
# the time it calls this, and regenerating it here would stamp the binary
# "+sha-dirty" for a build that is about to become the tag.
if [ -z "$CNC3D_SKIP_VERSION_HEADER" ]; then
    sh "../tools/version.sh" --header "../game/cnc3d_build.h"
fi

# The old ROOT pointed at a temporary folder that the OS cleans; the checkout now
# lives in the repo (brain/vanilla, gitignored).
ROOT=..
HDR=""
for c in "$ROOT/brain/vanilla/tiberiandawn" ../brain/vanilla/tiberiandawn ../../brain/vanilla/tiberiandawn; do
    [ -f "$c/dllinterface.h" ] && HDR="$c" && break
done
[ -n "$HDR" ] || { echo "cannot find dllinterface.h" >&2; exit 1; }

INC="-I../game -I../menu -I../video -I../audio"
CDEFS="-DGL_SILENCE_DEPRECATION"

# The C half: DOS rasteriser, menu, menu shell, movie decoder, and the audio engine.
#
# AUDIO_SRC is every file of audio/ except the harnesses and audio_null.c. It is plain
# portable C; the one file in it that knows what platform this is, is audio_sdl.c,
# which owns the device. That is the file a Win98 backend replaces, and nothing else
# in the list has to change with it.
AUDIO_SRC="../audio/sosadpcm.c ../audio/wsadpcm.c ../audio/wsaud.c ../audio/mixfile.c \
           ../audio/sndbank.c ../audio/mixer.c ../audio/sfxtable.c ../audio/sfxname.c \
           ../audio/cncaudio.c ../audio/wavio.c ../audio/audiotap.c \
           ../audio/audioboot.c ../audio/audio_sdl.c"
AUDIO_OBJ=""
for f in ../game/dosbar.c ../game/hud640.c ../game/dosopt.c ../game/dossave.c ../menu/dosmenu.c ../menu/dosops.c ../menu/doslobby.c ../menu/dosmenu_shell.c \
         ../video/vqaplay.c ../video/movieplay.c ../video/moviesnd.c \
         ../video/pngwrite.c campaign.c logo3d.c $AUDIO_SRC; do
    o=$(basename "$f" .c).o
    cc -std=gnu89 -O2 -g -Wall $CDEFS $INC $(sdl2-config --cflags) -c "$f" -o "$o"
done
for f in $AUDIO_SRC; do AUDIO_OBJ="$AUDIO_OBJ $(basename "$f" .c).o"; done

# The C++ half: the tactical renderer as a library, plus the state machine.
clang++ -std=c++14 -O2 -g \
    -fms-extensions -fdeclspec -D__int64="long long" -DCNC3D_NO_MAIN \
    -c ../game/cnc_eyes.cpp -o cnc_eyes.o \
    -I"$HDR" $INC $(sdl2-config --cflags)

clang++ -std=c++14 -O2 -g -c cnc3d.cpp -o cnc3d.o -I"$HDR" $INC $(sdl2-config --cflags)

# -headerpad_max_install_names: reserve room in the Mach-O header so the dylib paths can
# be rewritten afterwards without relinking. Today's rewrite happens to shrink the string
# (a 50 character Homebrew path becomes a 36 character @executable_path one) so it would
# fit anyway, but that is luck, not a property, and it stops being true the moment the
# library or the prefix is renamed.
clang++ -o cnc3d cnc3d.o cnc_eyes.o dosbar.o hud640.o dosopt.o dossave.o dosmenu.o dosops.o doslobby.o dosmenu_shell.o \
    vqaplay.o movieplay.o moviesnd.o pngwrite.o campaign.o logo3d.o $AUDIO_OBJ \
    $(sdl2-config --libs) -Wl,-headerpad_max_install_names -framework OpenGL -lz

# MAKE THE BINARY LOAD SDL FROM BESIDE ITSELF, not from the build machine's Homebrew.
# The linker writes the absolute path it linked against (/usr/local/opt/sdl2-compat/...),
# which is a path no other Mac has, so until recently every macOS release died in dyld
# on any machine but this one. tools/bundle-sdl.sh copies SDL beside the binary and
# rewrites the load command to @executable_path; its header carries the full account,
# including why SDL3 has to travel too. Run here rather than only in make-build.sh so
# that app/cnc3d is runnable in place and so the fix cannot be skipped by a hand build.
sh ../tools/bundle-sdl.sh . cnc3d

echo "built ./cnc3d"
