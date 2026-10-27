#!/bin/sh
# Build THE PROGRAM: menu and game in one binary, one window, one GL context.
#
#   ./build.sh          -> ./opencnc3d
#
# Notes that matter for the Win98 port:
#
#  * dosbar.c is compiled ONCE, from ../game. game/dosbar.c is a strict superset of
#    sidebar/dosbar.c (the NOD radar bezel, the disabled Repair/Sell look, and
#    db_draw_credits_tab) and its header carries the extern "C" guards a C++ caller
#    needs. Linking the two copies into one binary would be a duplicate symbol; using
#    the sidebar copy would compile dosmenu.c against a DB_State that has three
#    fewer fields than the one dosbar.c writes. So -I../game comes FIRST.
#  * -DOPENCNC3D_NO_MAIN compiles cnc_eyes.cpp as a library. Everything else about it is
#    unchanged, which is why ../game/build.sh still produces the standalone renderer
#    all five verification gates run against.
#  * GL_SILENCE_DEPRECATION: macOS deprecated the fixed function pipeline. We use it
#    on purpose; on the Voodoo 2 target it is the only thing there is.
set -e
cd "$(dirname "$0")"
python3 ../tools/ra3d/bake-enhanced-hud.py ../game/ra-enhanced-hud.pack --header ../game/ra_hud_layout.h >/dev/null

# A current executable also needs the calibrated approved building bibs.
python3 ../tools/art/remaster/bib/bake_bibenh.py --pack ../game/bibenh.pack >/dev/null

# THE OLDEST macOS THIS BINARY RUNS ON. Without it clang stamps LC_BUILD_VERSION minos
# with whatever SDK it finds (whatever is installed), and dyld refuses a Mach-O whose minos
# is newer than the running system, so the build is rejected on every Mac below macOS 26
# before main() runs. tools/mac/deployment-target.sh carries the full account and the
# reason the floor is 14.0 rather than lower. Sourced, so there is one number.
. ../tools/mac/deployment-target.sh
opencnc3d_need_mac_sdl

# THE BUILD NUMBER, regenerated before anything compiles. tools/version.sh reads the
# VERSION file at the repo root and writes game/opencnc3d_build.h; nothing else in the tree
# is allowed to carry a version string, so there is no second copy to forget to bump.
# tools/release.sh has already written the header with the bare release number by
# the time it calls this, and regenerating it here would stamp the binary
# "+sha-dirty" for a build that is about to become the tag.
if [ -z "$OPENCNC3D_SKIP_VERSION_HEADER" ]; then
    sh "../tools/version.sh" --header "../game/opencnc3d_build.h"
fi

# Freeze this build's identity. Another current-main build can regenerate the
# shared header between translation units or architectures; never mix its stamp.
BUILD_STAMP=$(mktemp "${TMPDIR:-/tmp}/cnc3d-build-stamp.XXXXXX")
cp "../game/opencnc3d_build.h" "$BUILD_STAMP"
trap 'rm -f "$BUILD_STAMP"' 0

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
# A COOKED BUILD: tools/release.sh sets OPENCNC3D_COOKED=1 for the packages it cuts, and the
# define removes the F5 tuning panel's every door (game/fx_panel.h). A developer's own
# build never sets it. It has to reach cnc_eyes.cpp and opencnc3d.cpp, which is why both
# clang++ lines below now take $CDEFS -- neither did before, and a define that missed
# the very file the panel lives in would be a cook that cooked nothing.
[ -n "$OPENCNC3D_COOKED" ] && CDEFS="$CDEFS -DOPENCNC3D_COOKED=1"

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
# mpbrowse.c is the internet game list; lnet.c and ljson.c are the HTTP client and JSON
# reader it asks through, shared with the launcher rather than copied into a second set
# that would drift away from it.
for f in ../game/dosbar.c ../game/hud640.c ../game/dosopt.c ../game/dossave.c ../menu/dosmenu.c ../menu/dosops.c ../menu/doslobby.c ../menu/dosmp.c ../menu/dosmenu_shell.c \
         ../menu/mpbrowse.c ../launcher/lnet.c ../launcher/ljson.c \
         ../video/vqaplay.c ../video/movieplay.c ../video/moviesnd.c \
         ../video/pngwrite.c campaign.c logo3d.c $AUDIO_SRC; do
    o=$(basename "$f" .c).o
    cc $OPENCNC3D_ARCH -std=gnu89 -O2 -g -Wall $CDEFS $INC $(sdl2-config --cflags) -c "$f" -o "$o"
done
for f in $AUDIO_SRC; do AUDIO_OBJ="$AUDIO_OBJ $(basename "$f" .c).o"; done

# THE LOCKSTEP NETWORKING, compiled here as well as in the renderer build because this
# binary links its own objects and this is the binary that ships. The Windows build has
# always put both into opencnc3d-game.exe as well as cnc_eyes.exe; without these two lines the two
# platforms disagree about what the program contains, and the source-list guard cannot see
# it because it unions the two macOS scripts into one list.
#
# NOT folded into the loop above, and that is the whole reason they are their own lines:
# that loop is -std=gnu89 with SDL's cflags, while lockstep.c is held to strict C89 with no
# I/O and no platform header in it at all. net_udp.c is the one file that knows what a
# socket is and needs gnu89 for the platform headers. Same split, same dialects and same
# reasoning as the renderer build.
cc $OPENCNC3D_ARCH -std=c89   -O2 -g -c ../net/lockstep.c -o lockstep.o
cc $OPENCNC3D_ARCH -std=gnu89 -O2 -g -c ../net/net_udp.c  -o net_udp.o
# roomcode: a relayed host's address, rendered by netmatch from the id it registered.
cc $OPENCNC3D_ARCH -std=gnu89 -O2 -g -c ../net/roomcode.c -o roomcode.o
cc $OPENCNC3D_ARCH -std=gnu89 -O2 -g -c ../net/netmatch.c -o netmatch.o
# netbeacon.c is LAN discovery, which the multiplayer screen's server browser reads.
cc $OPENCNC3D_ARCH -std=gnu89 -O2 -g -c ../net/netbeacon.c -o netbeacon.o

clang++ $OPENCNC3D_ARCH -std=c++14 -O2 -g -DGL_SILENCE_DEPRECATION $CDEFS $INC $(sdl2-config --cflags) -c ../menu/enhanced_menu.cpp -o enhanced_menu.o

# The C++ half: the tactical renderer as a library, plus the state machine.
clang++ $OPENCNC3D_ARCH -std=c++14 -O2 -g \
    -fms-extensions -fdeclspec -D__int64="long long" -DOPENCNC3D_NO_MAIN $CDEFS \
    -include "$BUILD_STAMP" -c ../game/cnc_eyes.cpp -o cnc_eyes.o \
    -I"$HDR" $INC $(sdl2-config --cflags)

clang++ $OPENCNC3D_ARCH -std=c++14 -O2 -g -include "$BUILD_STAMP" -c opencnc3d.cpp -o opencnc3d.o $CDEFS -I"$HDR" $INC $(sdl2-config --cflags)

# -headerpad_max_install_names: reserve room in the Mach-O header so the dylib paths can
# be rewritten afterwards without relinking. Today's rewrite happens to shrink the string
# (a 50 character Homebrew path becomes a 36 character @executable_path one) so it would
# fit anyway, but that is luck, not a property, and it stops being true the moment the
# library or the prefix is renamed.
clang++ $OPENCNC3D_ARCH -o opencnc3d opencnc3d.o cnc_eyes.o dosbar.o hud640.o dosopt.o dossave.o dosmenu.o dosops.o doslobby.o dosmp.o dosmenu_shell.o enhanced_menu.o \
    vqaplay.o movieplay.o moviesnd.o pngwrite.o campaign.o logo3d.o lockstep.o net_udp.o netmatch.o netbeacon.o roomcode.o \
    mpbrowse.o lnet.o ljson.o $AUDIO_OBJ \
    $(sdl2-config --libs) -Wl,-headerpad_max_install_names -framework OpenGL -lz -lcurl

# MAKE THE BINARY LOAD SDL FROM BESIDE ITSELF, not from the build machine's Homebrew.
# The linker writes the absolute path it linked against (/usr/local/opt/sdl2-compat/...),
# which is a path no other Mac has, so until recently every macOS release died in dyld
# on any machine but this one. tools/bundle-sdl.sh copies SDL beside the binary and
# rewrites the load command to @executable_path; its header carries the full account,
# including why SDL3 has to travel too. Run here rather than only in make-build.sh so
# that app/opencnc3d is runnable in place and so the fix cannot be skipped by a hand build.
sh ../tools/bundle-sdl.sh . opencnc3d

# STAGE IT, for the same reason game/build.sh stages cnc_eyes: the play folder is what is
# actually run and what the suite gates, and a binary that stops at app/ is a correct fix
# nobody can see. G30 catches the gap by comparing the DEPLOYED binaries against their
# sources, and it has caught exactly this twice, both times because the copy was a
# separate step somebody had to remember. It is not a separate step now.
if [ -d ../playable ]; then
    python3 ../tools/mac/stage-runtime.py ../playable opencnc3d
    echo "built ./opencnc3d  (and staged to ../playable/)"
else
    echo "built ./opencnc3d"
fi
