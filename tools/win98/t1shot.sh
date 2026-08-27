#!/bin/sh
# Build tools/win98/t1shot, the Mac-side model viewer. See t1shot.c for why it exists.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/t1shot"
mkdir -p "$(dirname "$OUT")"
# _snprintf is the msvcrt spelling t1_mesh.c uses; on a POSIX host it is snprintf.
cc -std=c89 -O2 -Wall -Wno-unused-function -D_snprintf=snprintf \
   -I"$ROOT/tier1" -o "$OUT" \
   "$ROOT/tools/win98/t1shot.c" "$ROOT/tier1/softras.c" "$ROOT/tier1/t1_mesh.c" \
   "$ROOT/tier1/t1_draw.c" -lm
echo "built $OUT"
