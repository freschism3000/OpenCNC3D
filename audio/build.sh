#!/bin/sh
# C&C 3D audio engine build.
#
#   ./build.sh          core + harnesses (no SDL, headless proof)
#   ./build.sh sdl      also builds the SDL2 smoke test
#
# The core is C89 with no platform API in it at all. That is the whole point: the
# Windows 98 build replaces audio_sdl.c and nothing else.
set -e
cd "$(dirname "$0")"

CFLAGS="-O2 -g -Wall -Wextra -std=c89 -pedantic"
CORE="sosadpcm.c wsadpcm.c wsaud.c mixfile.c sndbank.c mixer.c sfxtable.c sfxname.c cncaudio.c"
TEST="wavio.c"

# The core, compiled strictly. snprintf is C99, so the two files that use it for
# error strings get gnu89 instead; everything in the sample path stays C89.
for f in sosadpcm.c wsadpcm.c mixer.c sfxtable.c; do
    cc $CFLAGS -c $f -o "${f%.c}.o"
done
for f in wsaud.c mixfile.c sndbank.c cncaudio.c sfxname.c audiotap.c audioboot.c; do
    cc -O2 -g -Wall -Wextra -std=gnu89 -c $f -o "${f%.c}.o"
done
cc -O2 -g -Wall -Wextra -std=gnu89 -c wavio.c -o wavio.o
cc -O2 -g -Wall -Wextra -std=gnu89 -c audio_null.c -o audio_null.o

OBJ="sosadpcm.o wsadpcm.o wsaud.o mixfile.o sndbank.o mixer.o sfxtable.o sfxname.o cncaudio.o wavio.o audiotap.o audioboot.o audio_null.o"

cc -O2 -g -Wall -Wextra -std=gnu89 -o audtest  audtest.c  $OBJ -lm
cc -O2 -g -Wall -Wextra -std=gnu89 -o banktest banktest.c $OBJ -lm
cc -O2 -g -Wall -Wextra -std=gnu89 -o mixtest  mixtest.c  $OBJ -lm
echo "built ./audtest ./banktest ./mixtest  (backend: null, headless)"

if [ "$1" = "sdl" ]; then
    cc -O2 -g -Wall -Wextra -std=gnu89 -c audio_sdl.c -o audio_sdl.o $(sdl2-config --cflags)
    OBJ_SDL="sosadpcm.o wsadpcm.o wsaud.o mixfile.o sndbank.o mixer.o sfxtable.o sfxname.o cncaudio.o wavio.o audiotap.o audioboot.o audio_sdl.o"
    cc -O2 -g -Wall -Wextra -std=gnu89 -o playtest playtest.c $OBJ_SDL -lm $(sdl2-config --cflags --libs)
    echo "built ./playtest (backend: SDL2)"
fi
