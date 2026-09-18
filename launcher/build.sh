#!/bin/sh
# Build the macOS launcher.
#
#   ./build.sh                  -> ./cnc3d-launcher
#   ./build.sh --shot out.png   -> build, then render one frame to a PNG
#
# The Windows half is tools/win/build-launcher-win.sh and reads the SAME source
# list out of sources.sh, so the two cannot drift apart. That is the arrangement
# repository rules rule 4 already imposes on the game (tools/win/check-sources.sh), for
# the same reason: a file added to one platform and forgotten on the other is a
# build that breaks on the machine you are not sitting at.
set -e
cd "$(dirname "$0")"

# THE OLDEST macOS THESE BINARIES RUN ON, sourced the way every other build script here
# sources it. It was missing, and the failure was silent in the worst way: the
# cnc3d-launch line below expands "$CNC3D_MACOS_MIN" into an empty string, clang refuses
# "missing version number in -mmacosx-version-min=", the `&& echo` swallows the failure
# because a failing left side of && is not an error under set -e, and build.sh prints
# "built ./cnc3d-launcher" and exits 0 having produced NO app-bundle executable at all.
# Anyone running this script on its own got a stale cnc3d-launch or none.
#
# It also means cnc3d-launcher itself was stamped with the build machine's SDK floor --
# minos 26.0 on the shipped v0.6.7 -- rather than the project's 14.0.
. ../tools/mac/deployment-target.sh

# The update host, stamped in. Always run: it writes lcfg.h whether or not a key
# file exists, so a fresh clone compiles without one.
../tools/launcher/make-config.sh

. ./sources.sh

# GL_SILENCE_DEPRECATION: macOS deprecated fixed-function GL in 10.14. We use it
# on purpose; the shipping target is OpenGL 1.1 and Glide on Windows 98, where
# immediate mode is not legacy, it is all there is.
CFLAGS="-O2 -Wall -Wextra -std=gnu89 -I. -I../game -I../menu -I../video -DGL_SILENCE_DEPRECATION"

# -lcurl: libcurl ships in the macOS SDK, so this adds nothing for the player to
# install. -lz: the zip extractor inflates with it, and the game already links it.
cc $CFLAGS -mmacosx-version-min="$CNC3D_MACOS_MIN" -o cnc3d-launcher $LAUNCHER_SOURCES \
    $(sdl2-config --cflags --libs) -lcurl -lz -framework OpenGL

echo "built ./cnc3d-launcher"

# AND IT CARRIES ITS OWN SDL, exactly as game/build.sh and app/build.sh do for the two
# game halves. The linker records Homebrew's ABSOLUTE path, so a launcher built here and
# copied to any other Mac dies in dyld before main():
#
#     Library not loaded: /usr/local/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib
#
# The packager ran this over the finished folder, so releases were fine and only a
# hand-copied launcher was broken -- which is exactly how it was found. The two game
# binaries have been self-contained since the bundling work; the launcher was the one
# Mach-O left out of it, and one line is cheaper than remembering.
if [ "$(uname -s)" = "Darwin" ]; then
    sh ../tools/bundle-sdl.sh . cnc3d-launcher
fi

# THE APP BUNDLE'S OWN EXECUTABLE, on macOS. It was a bash script,
# and a bundle whose executable is a script cannot carry a code signature: macOS 15
# refuses to open a signed one, and refuses an unsigned one's local-network packets
# without ever asking the player, which is what made LAN play impossible on a Mac.
# Native, so the bundle can be signed, named, and therefore asked about.
# UNIVERSAL AND AGAINST THE PROJECT'S OWN MINIMUM, both load-bearing: a build machine
# runs a newer SDK than players do, and a binary stamped with a minimum newer than the
# player's macOS makes LaunchServices refuse the whole app with an error that reads like
# a corrupt download (-10825). Apple Silicon runs the game itself under Rosetta, but the
# app's own executable is what the system loads first, so it carries both slices.
if [ "$(uname -s)" = "Darwin" ]; then
    cc -arch x86_64 -arch arm64 -mmacosx-version-min="$CNC3D_MACOS_MIN" -O2 -Wall \
       -o cnc3d-launch mac_launch.c
    echo "built ./cnc3d-launch (the .app executable)"
fi

if [ "$1" = "--shot" ]; then
    shift
    OUT="${1:-launcher.png}"
    ./cnc3d-launcher --dir ../playable --shot "$OUT" --scale 3
fi
