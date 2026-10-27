#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
. tools/mac/deployment-target.sh
. launcher/sources.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-reinstall-gate.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sources=$(printf '%s' "$LAUNCHER_SOURCES" | sed 's/^launcher.c //')
cd launcher
cc -O1 -Wall -Wextra -std=gnu89 -I. -I../game -I../menu -I../video -DGL_SILENCE_DEPRECATION -o "$work/gate" ../tools/launcher/gate-module-reinstall.c $sources $(sdl2-config --cflags --libs) -lcurl -lz -framework OpenGL
cd ..
sh tools/bundle-sdl.sh "$work" gate
python3 tools/launcher/gate-module-reinstall.py "$work/gate" "$work/fixture"
