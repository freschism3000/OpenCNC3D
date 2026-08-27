#!/bin/sh
# Build the standalone DOS main menu preview.
#
#   ./build.sh            SDL2 + OpenGL 1.1 window, plus the PNG dump mode
#   ./build.sh nosdl      PNG dump only, no SDL, no GL (for headless checks)
#
# dosmenu.c and sidebar/dosbar.c need nothing but libc: both are CPU-side 8-bit
# compositing. Only preview.c touches SDL and GL, and only for one texture and
# one quad, which is what the Voodoo 2 target can do through Glide.
#
# dosbar.c is compiled from ../game, not ../sidebar. The two files were the same
# once; game/dosbar.c has since grown the NOD radar bezel, the disabled button look
# and db_draw_credits_tab, and its header carries the extern "C" guards the C++
# renderer needs. One binary cannot contain both, and the merged program links the
# game copy, so the preview must build against the same one or the DB_State it fills
# in is three fields short of the one dosbar.c reads.
set -e
cd "$(dirname "$0")"

# GL_SILENCE_DEPRECATION: macOS deprecated the fixed-function pipeline in 10.14.
# We use it on purpose. The shipping target is OpenGL 1.1 / Glide on Windows 98,
# where immediate mode is not legacy, it is the only thing there is.
CFLAGS="-O2 -Wall -Wextra -std=gnu89 -I../game -I../video -I../audio -DGL_SILENCE_DEPRECATION"
SRC="preview.c dosmenu.c dosops.c doslobby.c ../game/dosbar.c ../video/vqaplay.c"
MOVIE="../video/movieplay.c ../video/moviesnd.c ../video/pngwrite.c"
SDLSRC="dosmenu_shell.c"
# The audio engine, only in the SDL build: the PNG dump mode has no window, no clock
# and no speakers, and dragging a sound bank into it would only make a headless check
# depend on a sound card.
AUDIO="../audio/sosadpcm.c ../audio/wsadpcm.c ../audio/wsaud.c ../audio/mixfile.c \
       ../audio/sndbank.c ../audio/mixer.c ../audio/sfxtable.c ../audio/sfxname.c \
       ../audio/cncaudio.c ../audio/wavio.c ../audio/audiotap.c ../audio/audioboot.c \
       ../audio/audio_sdl.c"

if [ "$1" = "nosdl" ]; then
    cc $CFLAGS -DDB_NO_SDL -o preview $SRC
    echo "built ./preview (no SDL; use -o out.png)"
    exit 0
fi

case "$(uname -s)" in
Darwin) GL="-framework OpenGL" ;;
*)      GL="-lGL" ;;
esac

cc $CFLAGS -o preview $SRC $SDLSRC $MOVIE $AUDIO $(sdl2-config --cflags --libs) $GL
echo "built ./preview"
