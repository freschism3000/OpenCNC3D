#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
. tools/mac/deployment-target.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-http-retry.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -O1 -Wall -Wextra -Werror -std=gnu89 -o "$work/gate" \
    tools/launcher/gate-http-retry.c -lcurl
python3 tools/launcher/gate-http-retry.py "$work/gate"
