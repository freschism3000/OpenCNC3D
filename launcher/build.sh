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
cc $CFLAGS -o cnc3d-launcher $LAUNCHER_SOURCES \
    $(sdl2-config --cflags --libs) -lcurl -lz -framework OpenGL

echo "built ./cnc3d-launcher"

if [ "$1" = "--shot" ]; then
    shift
    OUT="${1:-launcher.png}"
    ./cnc3d-launcher --dir ../playable --shot "$OUT" --scale 3
fi
