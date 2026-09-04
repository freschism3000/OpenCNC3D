#!/usr/bin/env bash
#
# Prove that the committed engine checkout is EXACTLY upstream plus our patch.
#
# WHY THIS EXISTS. brain/vanilla is committed source rather than a patch applied to a
# clone, because a patch is only a DESCRIPTION of the checkout and the two drift apart
# without saying so.
#
# The checkout is therefore what we build, and this script keeps the patch honest beside
# it: it fetches upstream at the PINNED commit in brain/patches/UPSTREAM.txt, applies the
# patch to it, and demands the result be byte for byte the committed checkout. Two things
# can no longer happen quietly: the checkout drifting ahead of the patch, and the patch
# rotting against an upstream that moved.
#
# Run it locally exactly as CI does:  tools/check-brain-checkout.sh
#
# And when you have deliberately CHANGED the checkout, regenerate the patch from it:
#
#   tools/check-brain-checkout.sh --regen
#
# which rewrites brain/patches/vanilla-cnc3d.patch from the same pinned upstream, then
# re-verifies. That is the supported way to make the two agree again, and it exists so
# that "regenerate the patch in the same commit" is a command rather than a discipline.
# Done by hand, it is easy to file a hunk under the wrong file and not find out.
#
# RUN --regen BEFORE YOU BUILD THE BRAIN, NOT AFTER. This command TOUCHES
# brain/patches/cnc3d_compat.h, and that header is one of the sources G30 times the built
# dylib against. Regen after a build and G30 reports the brain STALE by however long the
# two commands were apart, on a dylib that is perfectly current and a header whose content
# did not change: measured 1 Sep 2026 at exactly one minute, with git diff on the header
# empty. G30 is right to complain and must not be loosened; the order is what is wrong.
# It is the same shape as app/build.sh regenerating game/cnc3d_build.h and making cnc_eyes
# older than a header it compiles. Registered with the other staleness traps.
set -euo pipefail

# LINE ENDINGS ARE NOT A DIFFERENCE. Git for Windows installs with core.autocrlf=true
# system wide, so the upstream fetched below materialises every file without an `eol`
# attribute as CRLF while the committed checkout is LF, and this guard then named 25 to
# 32 workflows, cmake files, icons and resource scripts as drift on a Windows checkout (3 Sep
# 2026) with not one engine source among them. With conversion off it names exactly the
# files that changed. Set for every git call this script makes, and nothing else.
export GIT_CONFIG_PARAMETERS="'core.autocrlf=false'"

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

sha="$(grep -v '^#' brain/patches/UPSTREAM.txt | tr -d '[:space:]')"
[ -n "$sha" ] || { echo "no upstream commit pinned in brain/patches/UPSTREAM.txt"; exit 1; }

[ -d brain/vanilla/tiberiandawn ] || {
  echo "brain/vanilla is not in the checkout. It is committed source now, not a clone;"
  echo "if it is missing, the checkout is broken rather than incomplete."
  exit 1
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "fetching upstream at pinned $sha"
git init -q "$work/upstream"
git -C "$work/upstream" remote add origin https://github.com/TheAssemblyArmada/Vanilla-Conquer.git
git -C "$work/upstream" fetch -q --depth 1 origin "$sha"
git -C "$work/upstream" checkout -q FETCH_HEAD

if [ "${1:-}" = "--regen" ]; then
  # Copy the committed checkout over the pristine upstream, minus the two files that are
  # COPIED rather than patched and the build dirs, then diff. The result is by
  # construction a patch that reproduces the checkout exactly.
  pristine="$work/pristine"
  cp -R "$work/upstream" "$pristine"
  rm -rf "$work/upstream"
  cp -R brain/vanilla "$work/upstream"
  rm -rf "$work/upstream/.git"
  find "$work/upstream" -maxdepth 1 -type d -name "build*" -exec rm -rf {} +
  cp -R "$pristine/.git" "$work/upstream/.git"
  rm -f "$work/upstream/common/wwkeyboard_null.cpp" "$work/upstream/tiberiandawn/cnc3d_compat.h"
  ( cd "$work/upstream" && git diff --no-color -- . ) > "$repo/brain/patches/vanilla-cnc3d.patch"
  cp brain/vanilla/common/wwkeyboard_null.cpp brain/patches/wwkeyboard_null.cpp
  cp brain/vanilla/tiberiandawn/cnc3d_compat.h brain/patches/cnc3d_compat.h
  echo "regenerated brain/patches/vanilla-cnc3d.patch from the checkout ($(wc -l < "$repo/brain/patches/vanilla-cnc3d.patch") lines)"
  echo "re-verifying..."
  rm -rf "$work"
  exec "$0"
fi

( cd "$work/upstream" && git apply "$repo/brain/patches/vanilla-cnc3d.patch" ) \
  || { echo "FAIL: brain/patches/vanilla-cnc3d.patch does not apply to upstream $sha"; exit 1; }
cp brain/patches/wwkeyboard_null.cpp "$work/upstream/common/"
cp brain/patches/cnc3d_compat.h      "$work/upstream/tiberiandawn/"

# Every build* directory is a build product: upstream's own .gitignore says "/build*" and
# ours makes three of them (build/ from CI, build-native/ from the Mac, build-win/ from the
# cross compile). Excluding them by name one at a time is how build-win got missed and made
# this guard cry wolf the first time the Windows build had run.
if diff -rq --exclude=.git --exclude='build*' \
        "$work/upstream" brain/vanilla > "$work/report" 2>&1; then
  echo "the committed brain checkout is exactly upstream $sha plus our patch"
  exit 0
fi

echo "FAIL: the committed brain/vanilla is NOT upstream $sha plus brain/patches/vanilla-cnc3d.patch."
echo
echo "Every line below is a place the two disagree. If you changed the checkout, regenerate"
echo "the patch from it (that is the rule: when the two disagree, the patch is the one that"
echo "is wrong). If you bumped upstream, re-pin brain/patches/UPSTREAM.txt in the same commit."
echo
sed -e "s#$work/upstream#UPSTREAM+PATCH#" -e 's#brain/vanilla#COMMITTED#' "$work/report"
exit 1
