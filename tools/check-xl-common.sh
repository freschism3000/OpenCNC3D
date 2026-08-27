#!/usr/bin/env bash
#
# THE XL COMMON LAW: brain/xl/common must equal brain/vanilla/common BYTE FOR BYTE.
#
# The XL brain (docs/design-xl-brain.md) is a real source fork of the engine, but the
# fork's licence to diverge covers tiberiandawn/ ONLY. common/ was verified
# coordinate-free at the fork point (zero COORDINATE/CELL hits), so there is no XL
# reason for it to ever differ, and keeping it identical means every upstream or
# portability fix to common/ flows to both brains by editing one place. The moment
# somebody "just quickly" patches brain/xl/common instead, the two engines start
# drifting in the one directory nobody reviews for drift -- exactly the failure mode
# the vanilla checkout gate (tools/check-brain-checkout.sh) exists to prevent.
#
# So: any byte of difference is a FAIL, including a file existing on only one side.
# The fix is always to make brain/xl/common identical to brain/vanilla/common again,
# never the other way around (vanilla is pinned to upstream by its own gate).
#
#   tools/check-xl-common.sh
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

[ -d brain/vanilla/common ] || { echo "check-xl-common: no brain/vanilla/common (broken checkout)"; exit 1; }
[ -d brain/xl/common ]      || { echo "check-xl-common: no brain/xl/common (the XL fork is missing its common/)"; exit 1; }

if diff -r brain/vanilla/common brain/xl/common >/tmp/xl-common-diff.$$ 2>&1; then
    rm -f /tmp/xl-common-diff.$$
    echo "check-xl-common: OK -- brain/xl/common is byte-identical to brain/vanilla/common"
    exit 0
else
    echo "check-xl-common: FAIL -- brain/xl/common differs from brain/vanilla/common:"
    head -40 /tmp/xl-common-diff.$$
    rm -f /tmp/xl-common-diff.$$
    echo "check-xl-common: the fix is to restore brain/xl/common to a byte-exact copy;"
    echo "only brain/xl/tiberiandawn may diverge (docs/design-xl-brain.md, fork shape)."
    exit 1
fi
