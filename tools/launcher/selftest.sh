#!/bin/sh
# Prove the launcher's update path, end to end, on this machine, in one command.
#
#   tools/launcher/selftest.sh
#
# It stands up a fake cnc3dgame.com that speaks the REAL three routes, builds a
# fake old install and a fake new release, points a launcher at it, and then
# checks what actually landed on the disk. Nothing is mocked inside the launcher:
# the binary under test is the shipping one, it speaks real HTTP, it follows a
# real 302 the way the live site issues one, and it unpacks a real zip whose real
# SHA-256 came out of a real manifest.
#
# TWO ROUNDS, because the launcher has two legitimate modes and only one of them
# is the interesting one:
#
#   A. THE RELEASE CARRIES A MANIFEST ASSET. The launcher can check a hash and can
#      prove the player's data already matches, so it takes the SMALL zip.
#   B. THE RELEASE CARRIES NO MANIFEST. It has to take the FULL package and can
#      only check its length. This is what every release before this one looks
#      like, so it is not a degraded case, it is the common one, and a test that
#      only covered round A would not cover today.
#
# WHAT IT ASSERTS, each of which has a way of silently not happening:
#
#   1. The check reads the site's own /api/builds and finds the newer version.
#   2. The changelog comes from /api/changelog and is written beside the game.
#   3. The 302 from /api/download is FOLLOWED. A test host that served bytes
#      directly would pass without exercising the thing most likely to break on
#      one of the two platforms.
#   4. With matching fingerprints the SMALL zip is chosen. If this regresses
#      nothing breaks: every update just quietly becomes 500 MB.
#   5. A TAMPERED download is refused on its hash, and nothing on disk is touched.
#   6. With no manifest, the full package is taken and a SHORT download is still
#      refused on its length.
#   7. The update replaces the binaries, keeps the executable bit, and rewrites
#      cnc3d-install.txt.
#   8. An update that does not carry a launcher does not delete the one running.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

PORT=""
WORK=$(mktemp -d)
SITEPID=""
FAILED=0

# PUT THE TREE BACK. This test builds a launcher with a throwaway
# http://127.0.0.1 site stamped into launcher/lcfg.h, and both that header and
# launcher/cnc3d-launcher are at fixed paths in the working copy. Left behind,
# the next person to package without rebuilding would ship a launcher pointed at
# a dead local port, and it would look perfectly fine until a player pressed
# Update. So the real config is regenerated and the launcher rebuilt on the way
# out, whatever happened in between, and the result is checked rather than
# assumed.
restore_tree() {
    tools/launcher/make-config.sh >/dev/null 2>&1 || true
    launcher/build.sh >/dev/null 2>&1 || true
    if grep -q '127\.0\.0\.1' launcher/lcfg.h 2>/dev/null; then
        echo
        echo "WARNING: launcher/lcfg.h still points at the local test site. Run" >&2
        echo "  tools/launcher/make-config.sh && launcher/build.sh" >&2
        echo "before packaging anything." >&2
    fi
}

cleanup() {
    [ -n "$SITEPID" ] && kill "$SITEPID" 2>/dev/null
    restore_tree
    rm -rf "$WORK"
}
trap cleanup EXIT

say()  { printf '\n== %s\n' "$*"; }
pass() { printf '   PASS  %s\n' "$*"; }
fail() { printf '   FAIL  %s\n' "$*"; FAILED=$((FAILED + 1)); }

[ -f playable/dosmenu.pack ] || {
    echo "playable/dosmenu.pack is missing. Run game/make-build.sh first: this test"
    echo "needs the real pack, because the launcher it builds has to actually start."
    exit 1
}

# ---------------------------------------------------------------- the old install
say "an install at v0.6.1"
OLD="$WORK/install"
mkdir -p "$OLD/missions"
cp playable/dosmenu.pack "$OLD/"
printf 'a data file that does not change between the two builds\n' > "$OLD/missions/data.bin"
printf 'OLD BINARY\n' > "$OLD/cnc3d"
chmod +x "$OLD/cnc3d"
printf 'OLD ENGINE\n' > "$OLD/TiberianDawn.dylib"
echo "   $OLD"

# ---------------------------------------------------------------- the new release
say "a release at v0.6.3"
NEWMAC="$WORK/pkg-macos"
NEWWIN="$WORK/pkg-windows"
for d in "$NEWMAC" "$NEWWIN"; do
    mkdir -p "$d/missions"
    cp playable/dosmenu.pack "$d/"
    # BYTE FOR BYTE the same data as the install above. That is what makes the
    # fingerprints match, which is what assertion 4 is about.
    printf 'a data file that does not change between the two builds\n' > "$d/missions/data.bin"
done
printf 'NEW BINARY\n' > "$NEWMAC/cnc3d"
chmod +x "$NEWMAC/cnc3d"
printf 'NEW ENGINE\n' > "$NEWMAC/TiberianDawn.dylib"
printf 'NEW BINARY\n' > "$NEWWIN/cnc3d.exe"
printf 'NEW ENGINE\n' > "$NEWWIN/TiberianDawn.dll"

# The install record each package carries, written the way the real packagers
# write it: the version, and the fingerprint from tools/launcher/data-id.sh. The
# manifest quotes this file rather than recomputing the number.
for d in "$NEWMAC" "$NEWWIN"; do
    printf '# selftest\nversion 0.6.3\ndata_id %s\n' \
        "$(tools/launcher/data-id.sh "$d")" > "$d/cnc3d-install.txt"
done

SITE="$WORK/site"
mkdir -p "$SITE"
( cd "$WORK" && cp -R pkg-macos "CNC3D-macos-v0.6.3" \
    && zip -qrX "$SITE/CNC3D-macos-v0.6.3.zip" "CNC3D-macos-v0.6.3" \
    && rm -rf "CNC3D-macos-v0.6.3" )
( cd "$WORK" && cp -R pkg-windows "CNC3D-windows-v0.6.3" \
    && zip -qrX "$SITE/CNC3D-windows-v0.6.3.zip" "CNC3D-windows-v0.6.3" \
    && rm -rf "CNC3D-windows-v0.6.3" )
# The binary-only zips are FLAT, which is what the launcher's strip=0 expects.
( cd "$NEWMAC" && zip -qX "$SITE/CNC3D-macos-v0.6.3-bins.zip" cnc3d TiberianDawn.dylib )
( cd "$NEWWIN" && zip -qX "$SITE/CNC3D-windows-v0.6.3-bins.zip" cnc3d.exe TiberianDawn.dll )

tools/launcher/make-manifest.sh v0.6.3 "$SITE/CNC3D-v0.6.3-manifest.txt" \
    "$SITE"/*.zip | sed 's/^/   /'
cp "$SITE/CNC3D-v0.6.3-manifest.txt" "$WORK/manifest-kept.txt"

# The install now claims the SAME data fingerprint the release carries, which is
# what an install produced by that release's own packager would say. Read out of
# the manifest rather than recomputed here, so this test cannot pass by agreeing
# with itself.
MACDATA=$(awk '$1=="macos_data_id"{print $2}' "$SITE/CNC3D-v0.6.3-manifest.txt")
[ -n "$MACDATA" ] || { echo "the manifest carries no macos_data_id"; exit 1; }
printf 'version 0.6.1\ndata_id %s\n' "$MACDATA" > "$OLD/cnc3d-install.txt"

# ---------------------------------------------------------------- the fake site
# A FREE PORT, NOT A CHOSEN ONE, AND THEN PROOF THAT THE SERVER ANSWERING IS OURS.
# An earlier version of this test hardcoded 8099, a concurrent edit on this machine
# already had it, the bind failed, and eleven assertions then ran against THAT
# server's 404s. Not one of them said the host was not ours.
say "a fake cnc3dgame.com"
tools/launcher/fake-site.py "$SITE" --tag v0.6.3 > "$WORK/site.log" 2>&1 &
SITEPID=$!
i=0
while [ $i -lt 200 ]; do
    PORT=$(sed -n 's/^PORT //p' "$WORK/site.log" | head -1)
    [ -n "$PORT" ] && break
    kill -0 "$SITEPID" 2>/dev/null || { echo "the fake site died:"; cat "$WORK/site.log"; exit 1; }
    i=$((i + 1))
done
[ -n "$PORT" ] || { echo "the fake site never said which port it took"; cat "$WORK/site.log"; exit 1; }
echo "   http://127.0.0.1:$PORT/"

# Whitespace-tolerant on purpose: the fake site answers through json.dumps,
# which writes `{"ok": true`, and the live site writes `{"ok":true`. A pattern
# that only matched one of them would fail on a server that was perfectly right.
PROOF=$(curl -s "http://127.0.0.1:$PORT/api/builds" | head -c 60)
case "$PROOF" in
    *'"ok"'*true*'"latest"'*) pass "the server on :$PORT is this test's fake site" ;;
    *) fail "something else is answering on :$PORT (got '$PROOF')"; exit 1 ;;
esac

# ---------------------------------------------------------------- the launcher
# The tree's own launcher is rebuilt against the fake site here and rebuilt
# against the real config by restore_tree on the way out.
say "build a launcher pointed at it"
CNC3D_SITE="http://127.0.0.1:$PORT" launcher/build.sh > "$WORK/build.log" 2>&1 \
    || { cat "$WORK/build.log"; exit 1; }
grep -q "127.0.0.1:$PORT" launcher/lcfg.h \
    && pass "it was built against the fake site, not the live one" \
    || { fail "lcfg.h does not name the fake site; the rest would test production"; exit 1; }

# 1. THE CHECK.
say "check"
launcher/cnc3d-launcher --dir "$OLD" --check > "$WORK/check.log" 2>&1 || true
sed 's/^/   /' "$WORK/check.log"
grep -q "latest:    v0.6.3" "$WORK/check.log" \
    && pass "the check read /api/builds and found v0.6.3" \
    || fail "the check did not find v0.6.3"
grep -q "result:    v0.6.3 is available" "$WORK/check.log" \
    && pass "and reported it as available" || fail "it did not report it as available"

# 5. A TAMPERED DOWNLOAD IS REFUSED. Done before the good run, so a pass here
#    cannot be a leftover from an update that already succeeded.
say "round A, a tampered zip"
cp "$SITE/CNC3D-macos-v0.6.3-bins.zip" "$WORK/good-bins.zip"
printf 'tamper' >> "$SITE/CNC3D-macos-v0.6.3-bins.zip"
launcher/cnc3d-launcher --dir "$OLD" --update > "$WORK/tamper.log" 2>&1 || true
sed 's/^/   /' "$WORK/tamper.log"
grep -q "did not arrive intact" "$WORK/tamper.log" \
    && pass "a modified zip is refused on its SHA-256" \
    || fail "a modified zip was NOT refused"
grep -q "OLD BINARY" "$OLD/cnc3d" \
    && pass "and nothing on disk was touched" \
    || fail "the refused update still wrote to the install"
cp "$WORK/good-bins.zip" "$SITE/CNC3D-macos-v0.6.3-bins.zip"

# 2, 3, 4, 7, 8. THE REAL UPDATE, WITH A MANIFEST.
say "round A, update"
launcher/cnc3d-launcher --dir "$OLD" --update > "$WORK/update.log" 2>&1 || true
sed 's/^/   /' "$WORK/update.log"
grep -q 'GET /api/download?asset=' "$WORK/site.log" \
    && pass "the download went through /api/download" \
    || fail "the launcher did not use the site's download route"
grep -q 'GET /files/CNC3D-macos-v0.6.3-bins.zip' "$WORK/site.log" \
    && pass "and the 302 was followed to the file" \
    || fail "the redirect was not followed (curl FOLLOWLOCATION / WinINet)"
grep -q 'GET /files/CNC3D-macos-v0.6.3.zip' "$WORK/site.log" \
    && fail "the full package was fetched too, which defeats the point" \
    || pass "the small binary-only zip was taken, not the full package"
grep -q "result:    installed v0.6.3" "$WORK/update.log" \
    && pass "the update reported success" || fail "the update did not succeed"
grep -q "NEW BINARY" "$OLD/cnc3d" \
    && pass "the game binary on disk is the new one" \
    || fail "the game binary was not replaced"
[ -x "$OLD/cnc3d" ] \
    && pass "and it is still executable" \
    || fail "the replaced binary lost its executable bit"
grep -q "version 0.6.3" "$OLD/cnc3d-install.txt" \
    && pass "cnc3d-install.txt names v0.6.3" \
    || fail "cnc3d-install.txt was not rewritten"
grep -q "The Front Door" "$OLD/CHANGELOG.txt" 2>/dev/null \
    && pass "the changelog came from /api/changelog and was written beside the game" \
    || fail "no changelog from the site was written"
[ -f "$OLD/cnc3d-update.zip" ] \
    && fail "the downloaded zip was left behind" \
    || pass "the downloaded zip was cleaned up"

# THE LAUNCHER IS STILL THERE. A regression test for a real defect: the update
# stepped the running launcher aside to make room for its replacement without
# first asking whether the zip carried one. Against a binary-only zip it did not,
# so the update succeeded and deleted the launcher.
[ -x launcher/cnc3d-launcher ] \
    && pass "the launcher survived an update that did not carry one" \
    || fail "the launcher was renamed away and never replaced"
[ -f launcher/cnc3d-launcher.old ] \
    && fail "a .old launcher was left behind by an update that carried none" \
    || pass "and no stray .old was left beside it"

say "check again"
launcher/cnc3d-launcher --dir "$OLD" --check > "$WORK/recheck.log" 2>&1 || true
sed 's/^/   /' "$WORK/recheck.log"
grep -q "result:    up to date" "$WORK/recheck.log" \
    && pass "the updated install reports up to date" \
    || fail "the updated install still thinks it is behind"

# ---------------------------------------------------------------- round B
# NO MANIFEST. This is what every release before this one looks like, so it is
# the common case rather than a degraded one: the launcher must take the FULL
# package and must still refuse a short download on its length alone.
say "round B, a release with no manifest asset"
rm -f "$SITE/CNC3D-v0.6.3-manifest.txt"

# The site is restarted on the SAME port, so the launcher built against it does
# not have to be rebuilt. That the restart actually took the port is checked
# rather than assumed: a failed bind would leave every assertion below talking
# to nothing, and "connection refused" reads a lot like a launcher bug.
restart_site() {   # restart_site [--short <name>]
    kill "$SITEPID" 2>/dev/null || true
    wait "$SITEPID" 2>/dev/null || true
    : > "$WORK/site.log"
    tools/launcher/fake-site.py "$SITE" --tag v0.6.3 --port "$PORT" "$@" \
        >> "$WORK/site.log" 2>&1 &
    SITEPID=$!
    i=0
    while [ $i -lt 400 ]; do
        curl -s -o /dev/null "http://127.0.0.1:$PORT/api/builds" && return 0
        kill -0 "$SITEPID" 2>/dev/null || break
        i=$((i + 1))
    done
    echo "the fake site did not come back on :$PORT"; cat "$WORK/site.log"; exit 1
}

# A TRANSFER THAT DIES MID-FLIGHT, to prove the length check is wired up when
# there is no hash to check instead. The site advertises the true size and sends
# half. Truncating the file on disk was tried first and proved nothing: it shrank
# what /api/builds reported too, so the launcher compared a short download
# against a short expectation, agreed, and the failure it eventually reported
# came from the zip reader rather than from the check under test.
restart_site --short CNC3D-macos-v0.6.3.zip
printf 'version 0.6.1\ndata_id %s\n' "$MACDATA" > "$OLD/cnc3d-install.txt"
printf 'OLD BINARY\n' > "$OLD/cnc3d"
launcher/cnc3d-launcher --dir "$OLD" --update > "$WORK/short.log" 2>&1 || true
sed 's/^/   /' "$WORK/short.log"
grep -q "stopped early" "$WORK/short.log" \
    && pass "with no hash published, a short download is refused on its length" \
    || fail "a truncated download was accepted"
grep -q "OLD BINARY" "$OLD/cnc3d" \
    && pass "and nothing on disk was touched" \
    || fail "the refused update still wrote to the install"

say "round B, update"
restart_site
launcher/cnc3d-launcher --dir "$OLD" --update > "$WORK/full.log" 2>&1 || true
sed 's/^/   /' "$WORK/full.log"
grep -q 'GET /files/CNC3D-macos-v0.6.3.zip' "$WORK/site.log" \
    && pass "with no fingerprint to compare, the FULL package was taken" \
    || fail "it did not fall back to the full package"
grep -q "result:    installed v0.6.3" "$WORK/full.log" \
    && pass "and the update succeeded" || fail "the full-package update failed"
grep -q "NEW BINARY" "$OLD/cnc3d" \
    && pass "the game binary on disk is the new one" \
    || fail "the game binary was not replaced"
grep -q "data_id unknown" "$OLD/cnc3d-install.txt" \
    && pass "the install records that it could not learn a fingerprint" \
    || fail "the install record claims a fingerprint it never saw"

echo
if [ "$FAILED" = "0" ]; then
    echo "LAUNCHER SELFTEST: all assertions passed"
else
    echo "LAUNCHER SELFTEST: $FAILED FAILED"
fi
exit $FAILED
