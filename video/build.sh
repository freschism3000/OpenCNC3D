#!/bin/sh
# Build the standalone VQA movie player.
#
#   ./build.sh          SDL2 + OpenGL 1.1 window, with audio
#   ./build.sh nosdl    decode only (--scan / -o), no window, no audio
#
# vqaplay.c itself needs nothing but libc: it is a decoder, not a player.
set -e
cd "$(dirname "$0")"

CFLAGS="-O2 -Wall -Wextra -std=gnu89 -DGL_SILENCE_DEPRECATION"
SRC="playvqa.c vqaplay.c pngwrite.c"

if [ "$1" = "nosdl" ]; then
    cc $CFLAGS -DVQ_NO_SDL -o playvqa $SRC
    echo "built ./playvqa (no SDL)"
    exit 0
fi

case "$(uname -s)" in
Darwin) GL="-framework OpenGL" ;;
*)      GL="-lGL" ;;
esac

cc $CFLAGS -o playvqa $SRC $(sdl2-config --cflags --libs) $GL
echo "built ./playvqa"
