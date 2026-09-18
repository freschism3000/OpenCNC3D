#!/bin/sh
# CNC3D renderer build (macOS). Absolute brain path so the workspace can live anywhere.
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

# A COOKED BUILD has no F5 panel; see app/build.sh for the whole of it. This binary ships
# in every package (make-build.sh), so it has to be able to be cooked too.
CDEFS=""
[ -n "$CNC3D_COOKED" ] && CDEFS="-DCNC3D_COOKED=1"

# The old ROOT pointed at a temporary folder that the OS cleans; the checkout now
# lives in the repo (brain/vanilla, gitignored). Fallback kept for odd layouts.
ROOT=..
HDR=""
for c in "$ROOT/brain/vanilla/tiberiandawn" ../brain/vanilla/tiberiandawn ../../../brain/vanilla/tiberiandawn; do
    [ -f "$c/dllinterface.h" ] && HDR="$c" && break
done
[ -n "$HDR" ] || { echo "cannot find dllinterface.h" >&2; exit 1; }

# dosbar.c is plain C89 with no GL and no SDL in it: it rasterises the 1995 MS-DOS
# sidebar into an 8-bit surface and nothing else. Compiled separately so it stays that
# way and so the Win98 build can hand it to a different C compiler unchanged.
cc -std=c89 -O2 -g -c dosbar.c -o dosbar.o
cc -std=c89 -O2 -g -c dossave.c -o dossave.o

# hud640.c is the 640x480 replacement sidebar. Same rule as dosbar.c: plain C89, no GL,
# no SDL, so the Win98 build takes it unchanged.
cc -std=c89 -O2 -g -c hud640.c -o hud640.o

# The 1995 in-game Options dialog. Same rule as dosbar.c: plain C89, no GL, no SDL,
# it rasterises into an 8-bit surface and nothing else.
cc -std=c89 -O2 -g -c dosopt.c -o dosopt.o

# THE 3D FACTION LOGOS. app/logo3d.c is compiled into BOTH binaries now: the app draws the
# spinning emblem on the score screen and the renderer draws it, plus the EVA wordmark, in
# the DATABASE page's two top corners. One file, one format, one baker (game/bake_logos.py)
# -- a second copy of a mesh reader is a second thing to keep in step with the pack.
# gnu89 rather than c89 because it includes <OpenGL/gl.h>, whose headers are not strict C89.
cc -std=gnu89 -O2 -g -c ../app/logo3d.c -o logo3d.o

# THE LOCKSTEP NETWORKING, same rule again and for the same reason. lockstep.c is the turn
# scheduler and has no I/O in it whatsoever, which is what lets it be tested by running two
# schedulers in one process with no network; net_udp.c is the only file that knows what a
# socket is. Keeping them apart is the discipline this file already applies to the DOS
# rasterisers, and it is what will let a relay or a different platform be dropped
# underneath without touching the rules above it.
cc -std=c89   -O2 -g -c ../net/lockstep.c -o lockstep.o
cc -std=gnu89 -O2 -g -c ../net/net_udp.c  -o net_udp.o
# netmatch.c is the match itself: the handshake, one turn per tick and the desync alarm,
# between the scheduler and the socket. It is the only net file the game calls.
# roomcode is a relayed host's whole address. netmatch renders a room's code from the id
# it registered, so this object comes BEFORE it and links with it everywhere.
cc -std=gnu89 -O2 -g -c ../net/roomcode.c -o roomcode.o
cc -std=gnu89 -O2 -g -c ../net/netmatch.c -o netmatch.o
# netbeacon.c is LAN discovery: a host announces by broadcast and a browser listens. Same
# rule as the two above -- it knows about a socket and nothing about a game -- so the
# server browser can be tested with no game running at all, which is what gate_beacon does.
cc -std=gnu89 -O2 -g -c ../net/netbeacon.c -o netbeacon.o

# The two gates and the connectivity tool are STANDALONE binaries, the same category as
# gate_optlayout below: nothing links them into the game. gate_lockstep drives the
# scheduler with no sockets at all; gate_netloop does it again over real loopback
# datagrams; netcheck is the two machine version a person runs by hand before blaming the
# game for a match that will not start. tools/win/check-sources.sh excludes all three by
# name, for the reason it excludes gate_optlayout.
cc -std=c89   -O2 -g -o gate_lockstep ../net/gate_lockstep.c lockstep.o
cc -std=gnu89 -O2 -g -o gate_netloop  ../net/gate_netloop.c  lockstep.o net_udp.o
# gate_tunnel is the third of the same kind: it runs a relay, two peers and the
# transport between them in one process, so the relayed path is tested with no relay
# server, no second machine and no internet.
cc -std=gnu89 -O2 -g -o gate_tunnel   ../net/gate_tunnel.c   net_udp.o
# gate_beacon proves LAN discovery with no game and no second machine: it announces and
# browses in one process. It runs BEFORE any server-browser pixel is drawn, because an
# empty browser and a quiet network look identical and only one of them is a bug.
cc -std=gnu89 -O2 -g -o gate_beacon   ../net/gate_beacon.c   netbeacon.o net_udp.o
# gate_lobby drives the LOBBY state machine end to end -- open a room, seat a joiner,
# refuse the start until they ready, start them both -- with no SDL and no second
# machine. The screens call these functions and own no socket, so this is where the
# question "does hosting and joining actually work" is answerable.
cc -std=gnu89 -O2 -g -o gate_lobby    ../net/gate_lobby.c    netmatch.o lockstep.o net_udp.o roomcode.o
cc -std=gnu89 -O2 -g -o netcheck      ../net/netcheck.c      lockstep.o net_udp.o roomcode.o
cc -std=gnu89 -O2 -g -o gate_roomcode ../net/gate_roomcode.c roomcode.o
# gate_mpbrowse: the game's own half of the internet game list. It needs the list
# service to answer, so the suite starts a stand-in and hands it the address; it needs
# no relay and no second machine, because the half that talks to another player is
# gated elsewhere.
cc -std=gnu99 -O2 -g -o gate_mpbrowse ../menu/gate_mpbrowse.c ../menu/mpbrowse.c \
   ../launcher/lnet.c ../launcher/ljson.c roomcode.o -I../menu -I../launcher \
   $(sdl2-config --cflags) $(sdl2-config --libs) -lcurl

# cnc_twobrain: TWO brain instances in ONE process, compared per tick. It is the gate that
# one-binary-twice cannot be: it proves two independent copies of the engine AGREE, which is
# what lockstep is built on. Dependency free, no sockets, dlopens the brain itself.
cc -std=gnu99 -O2 -g -o cnc_twobrain  ../brain/host/cnc_twobrain.c

# gate_optlayout MEASURES that dialog's geometry, and it is a separate binary precisely
# BECAUSE of the rule above: with no SDL and no GL in dosopt.c there is nothing to open a
# window for, so the whole layout can be interrogated headlessly in milliseconds. It
# exists because the suite could not see this dialog at all -- gates.sh claimed G13
# covered it, and G13 measures no rectangle -- while reports were coming in about two geometry bugs
# in it. See the header of gate_optlayout.c.
#
# It is a BUILD PRODUCT and stays out of git (.gitignore), like every other binary here.
# It links only dosopt.o and dosbar.o; -lz is dosbar's pack reader.
cc -std=c89 -O2 -g -o gate_optlayout gate_optlayout.c dosopt.o dosbar.o -lz

# gate_rminf MEASURES WHERE A REMASTERED INFANTRY STRIP PUTS THE GROUND, off the player's
# own install, and it is a separate binary for the same reason gate_optlayout is: the
# arithmetic under test is in remaster_inf.h, which needs no window and no GL, so the whole
# derivation can be re-read from the .meta files in milliseconds. C++ because
# remaster_tex.h pulls in terrain_tiles.h. -lz is the zip reader: the sprite archives are
# raw deflate. It exits 77 when the machine has no Remastered Collection, which is not a
# failure and which the gate reports as a skip.
c++ -std=gnu++98 -O2 -g -I. -o gate_rminf gate_rminf.cpp -lz

# The audio engine: the mixer, the 1995 .AUD decoders and the MIX reader. Same C89
# rule as dosbar.c and for the same reason. Every file here is portable C with no
# platform call in it EXCEPT audio_sdl.c, which is the device and the one file a
# Win98 backend replaces.
AUDIO="sosadpcm wsadpcm wsaud mixfile sndbank mixer sfxtable sfxname cncaudio \
       wavio audiotap audioboot"
AUDOBJ=""
for f in $AUDIO; do
    cc -std=c89 -O2 -g -c "../audio/$f.c" -o "aud_$f.o" -I../audio
    AUDOBJ="$AUDOBJ aud_$f.o"
done
cc -std=gnu89 -O2 -g -c ../audio/audio_sdl.c -o aud_sdl.o -I../audio $(sdl2-config --cflags)
AUDOBJ="$AUDOBJ aud_sdl.o"

# -headerpad_max_install_names: reserve room in the Mach-O header so the dylib paths can
# be rewritten after the link without relinking. Same flag and same reason as app/build.sh.
clang++ -std=c++14 -O2 -g \
    -fms-extensions -fdeclspec -D__int64="long long" $CDEFS \
    -o cnc_eyes cnc_eyes.cpp dosbar.o hud640.o dosopt.o dossave.o logo3d.o lockstep.o net_udp.o netmatch.o netbeacon.o roomcode.o $AUDOBJ \
    -I"$HDR" -I../audio \
    $(sdl2-config --cflags) $(sdl2-config --libs) \
    -Wl,-headerpad_max_install_names \
    -framework OpenGL -lz

# MAKE THE BINARY LOAD SDL FROM BESIDE ITSELF, not from the build machine's Homebrew. Same
# defect and same fix as app/build.sh: the linker records an absolute Intel-Homebrew path
# that no other Mac has, so the gates binary and the shipped binary both have to be
# repointed at @executable_path. tools/bundle-sdl.sh carries the full account, including
# why SDL3 has to travel too even though otool -L never mentions it.
sh ../tools/bundle-sdl.sh . cnc_eyes

# A CONTENT STAMP OF WHAT WAS JUST COMPILED. playable/Editor.app warns when the binary
# it is about to run predates the source, and mtime cannot answer that question: an edit
# that rewrites a file byte-identically -- which any read-modify-write script does when
# its change is a no-op -- moves the mtime and nothing else. Comparing content instead
# means the warning fires when the code really did change and stays quiet when it did
# not. Written LAST, so a failed build leaves the previous stamp and still reads stale.
# NO SECOND cd. This script already did "cd $(dirname "$0")" at line 4, so a second one
# resolves "game" against game/ and fails with "cd: game: No such file or directory" for
# every invocation that is not from the repo root -- and under `set -e` in any caller that
# wraps this, the whole build reports failure while having actually succeeded. The stamp
# was then never written, so the editor's staleness warning it exists to feed read stale
# for ever. Found 25 Aug 2026 by a caller that checked build.sh's exit status.
# The stamp carries the FILE LIST as well as the hash. Editor.app recomputes the hash to
# decide whether to warn that the binary is behind its sources, and it used to keep its
# own copy of this list -- so the day enhanced_mod.h was added here and not there, the two
# could never agree again and the launcher warned "rebuild" after every successful build.
# A warning that is always on is a warning nobody reads. One list, published here.
BUILD_SOURCES="cnc_eyes.cpp cnc_sidebar.h edit_mod.h edit_tables.h enhanced_mod.h \
edit_emblem.h codex_mod.h codex_table.h unitcard_mod.h dosbar.c hud640.c hud640.h dosopt.c ../app/logo3d.c \
../net/lockstep.c ../net/net_udp.c ../net/lockstep.h ../net/net_udp.h \
../net/netmatch.c ../net/netmatch.h ../net/netbeacon.c ../net/netbeacon.h"
{
    cat $BUILD_SOURCES 2>/dev/null | shasum -a 1 | cut -d" " -f1
    echo "$BUILD_SOURCES"
} > .build-sources.sha

# STAGE IT. Every gate in the suite runs ../playable/cnc_eyes, and building without
# copying is a trap that has now cost real time twice: the build succeeds, the gate runs
# the PREVIOUS binary, and the result is a failure in code that is already fixed or --
# far worse -- a pass on code that is already broken. There is no reason for the two to
# be able to disagree, so they no longer can.
if [ -d ../playable ]; then
    cp cnc_eyes ../playable/cnc_eyes
    # THE NET TEST BINARIES GO WITH IT, because the gate that runs them runs from the
    # PLAY folder and looks for ./gate_lockstep beside itself. Built here and left here,
    # they were invisible to the suite: the gate reported exit 99, "the binary was not
    # built", when all three had been built and were simply somewhere else. Same shape as
    # the engine and the app, so it is staged in the same place rather than in a third.
    for b in gate_lockstep gate_netloop gate_tunnel gate_beacon gate_lobby gate_roomcode gate_mpbrowse netcheck cnc_twobrain \
             gate_rminf; do
        [ -x "$b" ] && cp "$b" "../playable/$b"
    done
    # THE WORKED EXAMPLE. examples/ is tracked, playable/ is not, so the demo mission has
    # to be staged or it only exists on the machine it was made on. Copied ONLY when it is
    # not already there: it is meant to be edited, and a build that silently reverted
    # somebody's changes to it would be worse than not shipping it at all.
    if [ -d examples ]; then
        mkdir -p ../playable/missions/user_maps
        for f in examples/*.INI; do
            [ -f "$f" ] || continue
            b=$(basename "$f" .INI)
            if [ ! -f "../playable/missions/user_maps/$b.INI" ]; then
                cp examples/"$b".INI examples/"$b".BIN ../playable/missions/user_maps/ 2>/dev/null || true
                [ -f examples/"$b".HGT ] && cp examples/"$b".HGT ../playable/missions/user_maps/
                echo "staged the worked example $b into playable/missions/user_maps/"
            fi
        done
    fi
    # THE WINTER DONOR. New Map -> WINTER reboots onto SCW01EA the way a desert map
    # reboots onto SCB01EA -- but SCB01EA is on the 1995 disc and SCW01EA is ours, so
    # it must be staged wherever a session's --dir can point. Same only-if-absent rule
    # as the worked example.
    for d in ../playable/missions ../playable/missions/user_maps; do
        [ -d "$d" ] || continue
        for don in SCW01EA SCS01EA SCA01EA; do
            if [ ! -f "$d/$don.INI" ] && [ -f "missions/$don.INI" ]; then
                cp "missions/$don.INI" "missions/$don.BIN" "$d/"
                echo "staged the $don donor into $d/"
            fi
        done
    done
    echo "built ./cnc_eyes  (and staged to ../playable/)"
else
    echo "built ./cnc_eyes"
fi
