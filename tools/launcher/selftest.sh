#!/bin/sh
# Prove the launcher's update path, end to end, on this machine, in one command.
#
#   tools/launcher/selftest.sh
#
# It stands up a fake opencnc3d.com that speaks the REAL three routes, builds a
# fake old install and a fake new release, points a launcher at it, and then
# checks what actually landed on the disk. Nothing is mocked inside the launcher:
# the binary under test is the shipping one, it speaks real HTTP, it follows a
# real 302 the way the live site issues one, and it unpacks a real zip whose real
# SHA-256 came out of a real manifest.
#
# macOS ONLY, because the stand-ins below use chflags and BSD stat, and because
# the launcher it builds is launcher/build.sh's.
#
# FOUR ROUNDS, because the launcher has two legitimate modes, the binary-only zip
# comes in two shapes, and the hard cases need a zip of their own:
#
#   A. THE RELEASE CARRIES A MANIFEST ASSET. The launcher can check a hash and can
#      prove the player's data already matches, so it takes the SMALL zip.
#   B. THE RELEASE CARRIES NO MANIFEST. It has to take the FULL package and can
#      only check its length and the archive's own CRCs. This is what every
#      release before this one looks like, so it is not a degraded case, it is
#      the common one, and a test that only covered round A would not cover today.
#   C. THE SMALL ZIP IS WRAPPED IN A FOLDER, which is how the Windows packager
#      makes it (the macOS one is flat). Extracted as if it were flat, every
#      binary lands in a subfolder named after the zip and the update reports
#      success over a folder it never touched.
#   D. A SMALL ZIP BUILT FOR THE HARD CASES: both commit markers listed first, a
#      file in a subfolder, and a large entry to be killed in the middle of.
#   E. STARTS OVER A FOLDER AN UPDATE WAS KILLED IN, in two states a kill leaves
#      that the rounds above cannot stop at on purpose: between the renames of the
#      install record, and between the two renames of dosmenu.pack. Planted by hand,
#      the journal line included, exactly as the extractor writes them.
#   F. A LAUNCHER KILLED WHILE IT WRITES ITS OWN FILES, the site's changelog and
#      the install record, after every entry of a binary-only zip that carries no
#      record of its own, as the real ones do not.
#   G. THE macOS APP'S OWN EXECUTABLE, launcher/mac_launch.c, over a folder whose
#      update did not finish; and the app, and the bundle's tracked script, started
#      away from the game, the way macOS runs a downloaded copy.
#   H. HOSTILE FOLDERS: a folder that is not an install, full of files named like
#      an update's leftovers; journals planted to name files outside the folder;
#      and installs where a shipped name, or a folder on its way, is a symbolic link
#      to somewhere else. Everything the launcher must not touch is compared before
#      and after, byte for byte and to the nanosecond of its mtime.
#   I. A PLAYER'S OWN FILES ON THE NAMES AN UPDATE KEEPS FOR ITS COPIES, beside files
#      an update skips, replaces and fails on, beside a done journal's files and beside
#      the undo's spare; and install records an older launcher can leave empty or cut
#      short.
#   J. THE CUTOFF: the site shows a launcher of a release below 0.8.0 only the
#      releases below 0.8.0, by the User-Agent it sends, and shows this one all of
#      them.
#   K. THE MOVIES AND MUSIC, and the first install of the game into a folder that
#      holds nothing but the launcher: --media-install and --install-game, against
#      synthetic disc images the fake site serves as mirrors, each mirror going wrong
#      in one of the ways real ones do.
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
#      opencnc3d-install.txt.
#   8. An update that does not carry a launcher does not delete the one running.
#   9. A file already IDENTICAL to the zip's copy is not rewritten at all.
#  10. A changed file that cannot be opened for writing is stepped aside and
#      replaced, and what was stepped aside is removed on a later start, by the
#      name the update kept in opencnc3d-update.done, but not while something still
#      holds it. A .old no journal names is never touched.
#  11. An update that fails part way puts back every file it had already written,
#      and never writes BUILD-ID.txt, even from a zip that lists it first.
#  12. A binary-only zip wrapped in a folder lands at the top of the install.
#  13. While another launcher holds the folder, an update is refused before it
#      downloads anything and the start-up recovery leaves an unfinished update
#      alone.
#  14. With no hash to check, an entry whose bytes do not match the archive's CRC
#      fails the update, which is then undone.
#  15. A changed file that can be written but not renamed fails the update rather
#      than being written over with nothing to put back.
#  16. A launcher KILLED part way leaves both commit markers naming the old build,
#      and the next start undoes what it had written, from its journal.
#  17. Killed inside an entry, that entry's own name holds nothing half written:
#      the new copy is written beside it and only renamed in once it is whole.
#  18. The Windows installer run over a killed update renames the journal to
#      opencnc3d-update.done before its files go in, and the next start then leaves the
#      reinstalled folder whole instead of undoing the reinstall, and removes the
#      killed update's copies by the names that journal kept.
#  19. The installed version is read after the recovery, so a start over an update
#      killed after its record was renamed in offers the update again.
#  20. A start finds its folder, and puts dosmenu.pack back, while the pack is
#      between its two renames, instead of stopping at "could not find its game
#      files" before the recovery can run.
#  21. The install record and CHANGELOG.txt are written inside the update's journal,
#      so a kill while the launcher writes them undoes the update rather than
#      leaving a record claiming it over an empty or half-written file.
#  22. The macOS app never starts the game while an update is unfinished: it starts
#      the launcher, or the stepped-aside launcher, or says why it cannot. And it
#      still recognises the folder while any file it looks for is between its two
#      renames, so a launcher is started to finish the update.
#  23. A launcher pointed at, or copied into, a folder that is not an install changes
#      nothing in it or above it.
#  24. A planted journal changes nothing in a folder that is not an install; in an
#      install, its lines that climb out or name an absolute path change nothing
#      outside, and nothing a journal does not name changes inside.
#  25. Nothing is written, renamed, removed or re-moded through a symbolic link: not
#      by a journal's undo, and not by an update, which fails naming the file whose
#      way passes a linked folder, and that folder, and is undone, the link on a
#      shipped name included.
#  26. A --dir that is itself a link to a folder that is not an install changes
#      nothing behind the link.
#  27. An update leaves a player's own "<name>.old" to ".old9" and "<name>.opencnc3d-new"
#      as they were, beside a file it finds identical, one it replaces and one it fails
#      on, and fails naming a "<name>.opencnc3d-new" in its way instead of removing it.
#  28. A start removes, by opencnc3d-update.done, exactly the copies its lines record the
#      update making, and nothing else beside the same files.
#  29. The undo's spare copy of a file something still holds is journalled before it
#      is made, and a later start removes it by that name alone.
#  30. An install whose record is empty or cut short is updated and recovered; one
#      whose record is not a plain file is refused, saying so.
#  31. The macOS app, and the bundle's tracked script, started away from the game
#      find the folder the zip expanded to by its whole name, and pass over a folder
#      that only shares its first letters.
#  32. A launcher sends the User-Agent its own lnet.c names. The site, keyed on that
#      prefix, shows the last launcher below 0.8.0 nothing at or above 0.8.0, and
#      this one everything, and this one's agent does not start with the older one's.
#  33. --media-install movies,music fetches every file of the manifest by Range out of
#      the disc images, each with the SHA-256 the manifest pins, and the record says
#      both are complete. A mirror that answers Range with the whole disc is refused
#      at its first bytes, and not asked again; a 302 is followed with Range kept; a
#      mirror whose bytes are a web page fails the hash and the next mirror is asked;
#      a disc served inside a zip is reached through the manifest's base offset; a 500
#      is retried.
#  34. A transfer cut part way is resumed from where it stopped, a stalled one is
#      abandoned within the stall bound and tried elsewhere, and a file no mirror has
#      fails the job with the record saying pending.
#  35. Cancel keeps the part file and records pending, and the next run resumes it.
#  36. With nothing reachable the job fails as offline (exit 3), quickly.
#  37. --media-install none removes exactly the manifest's file and legacy names and
#      their part files, and nothing else: a player's own movie beside them stays. A
#      dosdata that is a link is refused and nothing behind it is touched, either way;
#      a folder that is not an install is refused and gets no record.
#  38. The progress file always ends with "end", carries its fields in order, its
#      percentage never goes down, its beat goes up, its sizes are the manifest's,
#      and its last state is the job's.
#  39. --install-game into a folder holding only the launcher installs the newest build
#      the site lists, resuming a package an earlier attempt cut short, writes a whole
#      install record, and then the movies and music; while an ordinary --update still
#      refuses a folder before it is an install.
#  40. An update from v0.8.0 to v0.8.1 afterwards, through the site with this launcher's
#      agent, leaves the movies, the music and the record exactly as they were, and an
#      update zip that carries the record's name is refused.
#  41. Settings never removes what the player did not untick: over a score that is only
#      partly there it ticks the music, Apply pressed for the movies sends both and the
#      job keeps every music file; unticking the partial music asks first, naming it; a
#      record left pending ticks what it chose and is said on the module grid.
#  42. A Stop that arrives as the game package finishes unpacking ends the setup
#      cancelled, with no movies or music job started and no choice recorded.
#  43. A first install's part file is tagged with the package it is a piece of: a new
#      build published in between is downloaded from its first byte, the old part
#      thrown away; and an ordinary start over an older install keeps the part, which
#      the next Setup then resumes.
#  44. --fetch-menu-art puts the newest build's dosmenu.pack, and nothing else, into a
#      folder holding only the launcher, and changes nothing when the build carries no
#      pack; the launcher there opens on Install, and a folder holding the game and no
#      record is not offered one.
#  45. The progress file's text says the same percentage as its pct, whatever the bytes
#      do underneath.
#
# THE STAND-INS FOR "WINDOWS WILL NOT LET YOU WRITE THIS". On Windows a DLL or
# .exe that a running process has loaded cannot be opened for writing and cannot
# be deleted, but CAN be renamed. macOS lets a loaded file be written, so a real
# loaded file proves nothing here. These reproduce the parts that matter, and
# each was checked against fopen/rename/remove before it was used:
#
#   a file with mode 0444      fopen "wb" refuses; rename and remove both work.
#                              THE UNIX CASE ONLY: Windows has no file that cannot
#                              be written and can be deleted (a read-only file is
#                              refused deletion too), so what this proves about
#                              removing a .old after a successful update is a path
#                              Windows does not take. There the .old waits for the
#                              sweep, which the next stand-in covers.
#   a non-empty directory      fopen "wb" refuses; rename works; remove refuses
#                              until it is emptied. That is a loaded DLL exactly,
#                              and emptying it is the process that held it exiting.
#   a file with chflags uchg   fopen "wb" AND rename both refuse. A file held open
#                              by another process without delete sharing behaves
#                              this way on Windows, so no amount of stepping aside
#                              can get past it and the update has to fail cleanly.
#   a writable file in a       rename refuses, fopen "wb" works, and no new file
#   folder made 0555           can be made beside it. The new copy of a changed
#                              file is written beside it first, so an update fails
#                              there, by name, before anything is renamed; what
#                              it proves is that a failure at a later entry puts
#                              back the earlier ones.
#   ulimit -f                  SIGXFSZ kills the launcher while it writes a large
#                              file, so no undo runs in the process that started
#                              the update: only the journal is left to go on.
#   a planted journal          the state a kill leaves between two renames, which
#                              no signal can be timed to hit. The lines are the
#                              extractor's own format, so a change to that format
#                              has to change them too.
#   mac_launch.c with its      the app's executable, compiled with system(), which
#   dialogs logged             is how it shows a dialog, writing the message to a
#                              file instead, so the test can read what it would
#                              have said and nothing appears on screen.
#   perl flock on the folder   another launcher holding the install lock. POSIX
#                              flock only; the Windows lock is a file opened with
#                              no sharing, and nothing here exercises it.
#   ln -s                      a symbolic link. Windows junctions and reparse points
#                              are asked about through a different call
#                              (GetFileAttributesA), which nothing here exercises.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

PORT=""
WORK=$(mktemp -d)
SITEPID=""
HOLDER=""
FAILED=0

# PUT THE TREE BACK. This test builds a launcher with a throwaway
# http://127.0.0.1 site stamped into launcher/lcfg.h, and both that header and
# launcher/opencnc3d-launcher are at fixed paths in the working copy. Left behind,
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
    [ -n "$HOLDER" ] && kill "$HOLDER" 2>/dev/null
    restore_tree
    # An immutable stand-in left behind by a run that stopped early would make
    # rm -rf fail on it, so the flag comes off everything first.
    chflags -R nouchg "$WORK" 2>/dev/null || true
    chmod -R u+w "$WORK" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

say()  { printf '\n== %s\n' "$*"; }
pass() { printf '   PASS  %s\n' "$*"; }
fail() { printf '   FAIL  %s\n' "$*"; FAILED=$((FAILED + 1)); }
nap()  { perl -e 'select(undef, undef, undef, 0.05)'; }

[ -f playable/dosmenu.pack ] || {
    echo "playable/dosmenu.pack is missing. Run game/make-build.sh first: this test"
    echo "needs the real pack, because the launcher it builds has to actually start."
    exit 1
}

# The binary-only zip's contents, in the order it lists them. BUILD-ID.txt is
# FIRST on purpose: the launcher has to write it last whatever order the zip
# uses, and a zip that already put it last could not show that.
BINS="BUILD-ID.txt opencnc3d TiberianDawn.dylib cnc_eyes netcheck libSDL2-2.0.0.dylib"

# ---------------------------------------------------------------- the old install
say "an install at v0.6.1"
OLD="$WORK/install"
mkdir -p "$OLD/missions"
cp playable/dosmenu.pack "$OLD/"
printf 'a data file that does not change between the two builds\n' > "$OLD/missions/data.bin"
printf 'OLD BINARY\n' > "$OLD/opencnc3d"
chmod +x "$OLD/opencnc3d"
printf 'OLD ENGINE\n' > "$OLD/TiberianDawn.dylib"
printf 'OLD EDITOR\n' > "$OLD/cnc_eyes"
printf 'OLD NETCHECK\n' > "$OLD/netcheck"
chmod +x "$OLD/cnc_eyes" "$OLD/netcheck"
printf 'the same SDL in both builds\n' > "$OLD/libSDL2-2.0.0.dylib"
printf 'v0.6.1  macos\n' > "$OLD/BUILD-ID.txt"
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
printf 'NEW BINARY\n' > "$NEWMAC/opencnc3d"
chmod +x "$NEWMAC/opencnc3d"
printf 'NEW ENGINE\n' > "$NEWMAC/TiberianDawn.dylib"
printf 'NEW EDITOR\n' > "$NEWMAC/cnc_eyes"
printf 'NEW NETCHECK\n' > "$NEWMAC/netcheck"
chmod +x "$NEWMAC/cnc_eyes" "$NEWMAC/netcheck"
printf 'the same SDL in both builds\n' > "$NEWMAC/libSDL2-2.0.0.dylib"
printf 'v0.6.3  macos\n' > "$NEWMAC/BUILD-ID.txt"
printf 'NEW BINARY\n' > "$NEWWIN/opencnc3d-game.exe"
printf 'NEW ENGINE\n' > "$NEWWIN/TiberianDawn.dll"

# The install record each package carries, written the way the real packagers
# write it: the version, and the fingerprint from tools/launcher/data-id.sh. The
# manifest quotes this file rather than recomputing the number.
for d in "$NEWMAC" "$NEWWIN"; do
    printf '# selftest\nversion 0.6.3\ndata_id %s\n' \
        "$(tools/launcher/data-id.sh "$d")" > "$d/opencnc3d-install.txt"
done

SITE="$WORK/site"
mkdir -p "$SITE"
( cd "$WORK" && cp -R pkg-macos "OpenCNC3D-macos-v0.6.3" \
    && zip -qrX "$SITE/OpenCNC3D-macos-v0.6.3.zip" "OpenCNC3D-macos-v0.6.3" \
    && rm -rf "OpenCNC3D-macos-v0.6.3" )
( cd "$WORK" && cp -R pkg-windows "OpenCNC3D-windows-v0.6.3" \
    && zip -qrX "$SITE/OpenCNC3D-windows-v0.6.3.zip" "OpenCNC3D-windows-v0.6.3" \
    && rm -rf "OpenCNC3D-windows-v0.6.3" )
# The macOS binary-only zip is FLAT, as tools/release.sh makes it.
( cd "$NEWMAC" && zip -qX "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip" $BINS )
( cd "$NEWWIN" && zip -qX "$SITE/OpenCNC3D-windows-v0.6.3-bins.zip" opencnc3d-game.exe TiberianDawn.dll )
# And the same contents WRAPPED, the shape tools/win/make-build-win.sh --bins-only
# gives the Windows one: a directory entry, then every file under it. Served in
# round C.
( cd "$WORK" && mkdir wrap && cp -R pkg-macos "wrap/OpenCNC3D-macos-v0.6.3" \
    && cd wrap && zip -qX "$WORK/wrapped-bins.zip" "OpenCNC3D-macos-v0.6.3/" \
       $(for f in $BINS; do printf 'OpenCNC3D-macos-v0.6.3/%s ' "$f"; done) \
    && cd .. && rm -rf wrap )
# The full package again, STORED rather than deflated, with one byte of the game
# binary changed and the length unchanged. Stored, because a flipped byte inside
# deflated data usually trips zlib itself, and the case under test is the one
# zlib cannot see. Served in round B, where there is no hash to catch it first.
( cd "$WORK" && cp -R pkg-macos "OpenCNC3D-macos-v0.6.3" \
    && zip -qrX -0 "$WORK/damaged-full.zip" "OpenCNC3D-macos-v0.6.3" \
    && rm -rf "OpenCNC3D-macos-v0.6.3" )
DAMAGED=$(perl -0777 -i -pe '$n = s/NEW BINARY/NEW BINERY/g; END { print STDERR $n }' \
    "$WORK/damaged-full.zip" 2>&1)
# The hard cases' zip for round D, flat, listed in this order: both commit markers
# first, a file in a subfolder, and 32 MB of zeros, which deflate to almost nothing
# so the download stays small while writing the entry does not.
HARD="$WORK/pkg-hard"
mkdir -p "$HARD/ro"
cp "$NEWMAC/BUILD-ID.txt" "$NEWMAC/opencnc3d" "$NEWMAC/TiberianDawn.dylib" "$HARD/"
cp "$NEWMAC/opencnc3d-install.txt" "$HARD/"
printf 'NEW P\n' > "$HARD/ro/p"
dd if=/dev/zero of="$HARD/big.bin" bs=1048576 count=32 2>/dev/null
# Executable, so a half-written big.bin on its own name would also be one without
# its executable bit, which is the state a macOS launcher must never be left in.
chmod +x "$HARD/big.bin"
( cd "$HARD" && zip -qX "$WORK/hard-bins.zip" BUILD-ID.txt opencnc3d-install.txt opencnc3d ro/p \
    big.bin TiberianDawn.dylib )

tools/launcher/make-manifest.sh v0.6.3 "$SITE/OpenCNC3D-v0.6.3-manifest.txt" \
    "$SITE"/*.zip | sed 's/^/   /'
cp "$SITE/OpenCNC3D-v0.6.3-manifest.txt" "$WORK/manifest-kept.txt"

# The install now claims the SAME data fingerprint the release carries, which is
# what an install produced by that release's own packager would say. Read out of
# the manifest rather than recomputed here, so this test cannot pass by agreeing
# with itself.
MACDATA=$(awk '$1=="macos_data_id"{print $2}' "$SITE/OpenCNC3D-v0.6.3-manifest.txt")
[ -n "$MACDATA" ] || { echo "the manifest carries no macos_data_id"; exit 1; }
printf 'version 0.6.1\ndata_id %s\n' "$MACDATA" > "$OLD/opencnc3d-install.txt"

# ---------------------------------------------------------------- the fake site
# A FREE PORT, NOT A CHOSEN ONE, AND THEN PROOF THAT THE SERVER ANSWERING IS OURS.
# An earlier version of this test hardcoded 8099, another process on this machine
# already had it, the bind failed, and eleven assertions then ran against THAT
# server's 404s. Not one of them said the host was not ours.
say "a fake opencnc3d.com"
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
OPENCNC3D_SITE="http://127.0.0.1:$PORT" launcher/build.sh > "$WORK/build.log" 2>&1 \
    || { cat "$WORK/build.log"; exit 1; }
grep -q "127.0.0.1:$PORT" launcher/lcfg.h \
    && pass "it was built against the fake site, not the live one" \
    || { fail "lcfg.h does not name the fake site; the rest would test production"; exit 1; }

# 1. THE CHECK.
say "check"
launcher/opencnc3d-launcher --dir "$OLD" --check > "$WORK/check.log" 2>&1 || true
sed 's/^/   /' "$WORK/check.log"
grep -q "latest:    v0.6.3" "$WORK/check.log" \
    && pass "the check read /api/builds and found v0.6.3" \
    || fail "the check did not find v0.6.3"
grep -q "result:    v0.6.3 is available" "$WORK/check.log" \
    && pass "and reported it as available" || fail "it did not report it as available"

# 5. A TAMPERED DOWNLOAD IS REFUSED. Done before the good run, so a pass here
#    cannot be a leftover from an update that already succeeded.
say "round A, a tampered zip"
cp "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip" "$WORK/good-bins.zip"
printf 'tamper' >> "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip"
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/tamper.log" 2>&1 || true
sed 's/^/   /' "$WORK/tamper.log"
grep -q "did not arrive intact" "$WORK/tamper.log" \
    && pass "a modified zip is refused on its SHA-256" \
    || fail "a modified zip was NOT refused"
grep -q "OLD BINARY" "$OLD/opencnc3d" \
    && pass "and nothing on disk was touched" \
    || fail "the refused update still wrote to the install"
cp "$WORK/good-bins.zip" "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip"

# 11. AN UPDATE THAT CANNOT FINISH. The engine is made immutable, the stand-in for
#     a file another process holds without delete sharing: it can be neither
#     written nor stepped aside, so nothing the launcher does can get past it.
#     opencnc3d comes before it in the zip and IS written first, so this is the case
#     that used to leave a new game binary beside an old engine. And BUILD-ID.txt
#     is the zip's first entry, so zip order alone would have written it.
say "round A, an update that cannot finish"
chflags uchg "$OLD/TiberianDawn.dylib"
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/stuck.log" 2>&1 || true
chflags nouchg "$OLD/TiberianDawn.dylib"
sed 's/^/   /' "$WORK/stuck.log"
grep -q "result:    FAILED" "$WORK/stuck.log" \
    && pass "an update with a file that can be neither replaced nor stepped aside fails" \
    || fail "the update did not report the failure"
grep -q "TiberianDawn.dylib" "$WORK/stuck.log" \
    && pass "and the failure names the file" \
    || fail "the failure does not say which file"
grep -q "OLD BINARY" "$OLD/opencnc3d" \
    && pass "opencnc3d, written before the failure, was put back: no new game over an old engine" \
    || fail "the failed update left the NEW opencnc3d beside the OLD engine"
grep -q "OLD ENGINE" "$OLD/TiberianDawn.dylib" \
    && pass "the file that could not be replaced is intact" \
    || fail "the file that could not be replaced was damaged"
grep -q "v0.6.1" "$OLD/BUILD-ID.txt" \
    && pass "BUILD-ID.txt was not written, although the zip lists it first" \
    || fail "BUILD-ID.txt names the new build after an update that did not finish"
grep -q "version 0.6.1" "$OLD/opencnc3d-install.txt" \
    && pass "the install record still names v0.6.1, so the update is offered again" \
    || fail "the install record claims an update that did not finish"
STRAY=$(find "$OLD" -name '*.old*' | head -3 | tr '\n' ' ')
[ -z "$STRAY" ] \
    && pass "and nothing was left stepped aside" \
    || fail "the rolled back update left stepped-aside files: $STRAY"
[ ! -e "$OLD/opencnc3d-update.journal" ] \
    && pass "and the undo, being complete, removed its journal" \
    || fail "a complete undo left its journal behind, so the next update would refuse to start"

# 13. ANOTHER LAUNCHER HOLDS THE FOLDER. An update killed between the two renames of
#     the engine is planted, which any start would undo, and the folder is locked the
#     way a second launcher locks it. Neither the start-up recovery nor the update may
#     touch anything until it lets go.
say "round A, another launcher holds the folder"
mv "$OLD/TiberianDawn.dylib" "$OLD/TiberianDawn.dylib.old"
printf 'HALF WAY\n' > "$OLD/TiberianDawn.dylib"
printf 'opencnc3d update journal 1\nA 1 TiberianDawn.dylib\n' > "$OLD/opencnc3d-update.journal"
perl -e 'use Fcntl qw(:flock); $| = 1; open(my $h, "<", $ARGV[0]) or die "open: $!";
         flock($h, LOCK_EX | LOCK_NB) or die "flock: $!"; print "held\n"; sleep 120' \
    "$OLD" > "$WORK/holder.log" 2>&1 &
HOLDER=$!
i=0
while [ $i -lt 100 ] && ! grep -q held "$WORK/holder.log"; do nap; i=$((i + 1)); done
grep -q held "$WORK/holder.log" || { cat "$WORK/holder.log"; fail "the stand-in could not lock the folder"; }
# ZIPS ONLY. The check before any update fetches the manifest through the same
# /files/ route, and counting that would fail a launcher that did nothing wrong.
FILES_BEFORE=$(grep -c 'GET /files/[^ ]*\.zip' "$WORK/site.log" || true)
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/locked.log" 2>&1 || true
sed 's/^/   /' "$WORK/locked.log"
grep -q "another OpenCNC3D launcher is using this game folder" "$WORK/locked.log" \
    && pass "an update is refused while another launcher holds the folder" \
    || fail "an update ran while another launcher held the folder"
[ "$(grep -c 'GET /files/[^ ]*\.zip' "$WORK/site.log" || true)" = "$FILES_BEFORE" ] \
    && pass "and it was refused before downloading anything into the folder" \
    || fail "the refused update still downloaded into the folder"
grep -q "OLD BINARY" "$OLD/opencnc3d" \
    && pass "and nothing was installed" \
    || fail "an update went ahead under another launcher's lock"
[ -f "$OLD/TiberianDawn.dylib.old" ] && grep -q "HALF WAY" "$OLD/TiberianDawn.dylib" \
    && [ -f "$OLD/opencnc3d-update.journal" ] \
    && pass "the start-up recovery left the unfinished update alone while another launcher held the folder" \
    || fail "a launcher recovered an update while another launcher held the folder"
kill "$HOLDER" 2>/dev/null || true
wait "$HOLDER" 2>/dev/null || true
HOLDER=""
launcher/opencnc3d-launcher --dir "$OLD" --check > "$WORK/unlocked.log" 2>&1 || true
[ ! -e "$OLD/TiberianDawn.dylib.old" ] && grep -q "OLD ENGINE" "$OLD/TiberianDawn.dylib" \
    && [ ! -e "$OLD/opencnc3d-update.journal" ] \
    && pass "once it lets go, the next start finishes it" \
    || fail "the first start after the lock was released did not recover"

# 2, 3, 4, 7, 8, 9, 10. THE REAL UPDATE, WITH A MANIFEST, OVER THE STAND-INS.
say "round A, update"
#  9: the SDL library is identical in both builds and cannot be opened for writing.
chmod 0444 "$OLD/libSDL2-2.0.0.dylib"
SDL_BEFORE=$(stat -f '%i %m' "$OLD/libSDL2-2.0.0.dylib")
# 10, the unix case: netcheck changed and cannot be opened for writing, but can be
#     removed, which no file on Windows allows (see the stand-ins in the header).
chmod 0444 "$OLD/netcheck"
# 10: cnc_eyes changed and is HELD: a non-empty directory where the file was.
rm -f "$OLD/cnc_eyes"
mkdir "$OLD/cnc_eyes"
printf 'held\n' > "$OLD/cnc_eyes/held"
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/update.log" 2>&1 || true
sed 's/^/   /' "$WORK/update.log"
grep -q 'GET /api/download?asset=' "$WORK/site.log" \
    && pass "the download went through /api/download" \
    || fail "the launcher did not use the site's download route"
grep -q 'GET /files/OpenCNC3D-macos-v0.6.3-bins.zip' "$WORK/site.log" \
    && pass "and the 302 was followed to the file" \
    || fail "the redirect was not followed (curl FOLLOWLOCATION / WinINet)"
grep -q 'GET /files/OpenCNC3D-macos-v0.6.3.zip' "$WORK/site.log" \
    && fail "the full package was fetched too, which defeats the point" \
    || pass "the small binary-only zip was taken, not the full package"
grep -q "result:    installed v0.6.3" "$WORK/update.log" \
    && pass "the update reported success" || fail "the update did not succeed"
grep -q "NEW BINARY" "$OLD/opencnc3d" \
    && pass "the game binary on disk is the new one" \
    || fail "the game binary was not replaced"
[ -x "$OLD/opencnc3d" ] \
    && pass "and it is still executable" \
    || fail "the replaced binary lost its executable bit"
# The update has to have SUCCEEDED for this to mean anything: an update that
# stopped at the SDL stand-in, or before it, leaves the same inode too.
grep -q "result:    installed v0.6.3" "$WORK/update.log" \
    && [ "$(stat -f '%i %m' "$OLD/libSDL2-2.0.0.dylib")" = "$SDL_BEFORE" ] \
    && pass "an identical file is skipped: the unwritable SDL stand-in is the same inode, same mtime" \
    || fail "an identical file was rewritten, or the update never got past it"
grep -q "NEW NETCHECK" "$OLD/netcheck" 2>/dev/null \
    && pass "a changed file that cannot be opened for writing was replaced" \
    || fail "a changed file that cannot be opened for writing was not replaced"
grep -q "NEW NETCHECK" "$OLD/netcheck" 2>/dev/null && [ ! -e "$OLD/netcheck.old" ] \
    && pass "and, on unix, what it stepped aside was removed once the update succeeded" \
    || fail "the netcheck it stepped aside was left behind, or it was never replaced"
[ -f "$OLD/cnc_eyes" ] && grep -q "NEW EDITOR" "$OLD/cnc_eyes" \
    && pass "a changed file still HELD was stepped aside and the new one written on its name" \
    || fail "a held file blocked its own replacement"
[ -d "$OLD/cnc_eyes.old" ] \
    && pass "and the held one is still there under .old, because it cannot be removed yet" \
    || fail "the held file is not where the sweep will look for it"
grep -q "result:    installed v0.6.3" "$WORK/update.log" && grep -q "v0.6.3" "$OLD/BUILD-ID.txt" \
    && pass "BUILD-ID.txt was written by the update that succeeded" \
    || fail "BUILD-ID.txt was not written by a successful update"
grep -q "version 0.6.3" "$OLD/opencnc3d-install.txt" \
    && pass "opencnc3d-install.txt names v0.6.3" \
    || fail "opencnc3d-install.txt was not rewritten"
grep -q "The Front Door" "$OLD/CHANGELOG.txt" 2>/dev/null \
    && pass "the changelog came from /api/changelog and was written beside the game" \
    || fail "no changelog from the site was written"
[ -f "$OLD/opencnc3d-update.zip" ] \
    && fail "the downloaded zip was left behind" \
    || pass "the downloaded zip was cleaned up"
[ ! -e "$OLD/opencnc3d-update.journal" ] \
    && pass "and the update journal is gone, since the update finished" \
    || fail "a successful update left its journal behind"
grep -qx "A 1 cnc_eyes" "$OLD/opencnc3d-update.done" 2>/dev/null \
    && pass "the held copy's name is kept in opencnc3d-update.done, for a later start to remove it by" \
    || fail "nothing kept the held copy's name, so no start can remove it without searching for it"

# THE LAUNCHER IS STILL THERE. A regression test for a real defect: the update
# stepped the running launcher aside to make room for its replacement without
# first asking whether the zip carried one. Against a binary-only zip it did not,
# so the update succeeded and deleted the launcher.
[ -x launcher/opencnc3d-launcher ] \
    && pass "the launcher survived an update that did not carry one" \
    || fail "the launcher was renamed away and never replaced"
[ -f launcher/opencnc3d-launcher.old ] \
    && fail "a .old launcher was left behind by an update that carried none" \
    || pass "and no stray .old was left beside it"

# 10. THE SWEEP, which runs whenever the launcher starts.
say "check again"
launcher/opencnc3d-launcher --dir "$OLD" --check > "$WORK/recheck.log" 2>&1 || true
sed 's/^/   /' "$WORK/recheck.log"
grep -q "result:    up to date" "$WORK/recheck.log" \
    && pass "the updated install reports up to date" \
    || fail "the updated install still thinks it is behind"
HELD_SEEN=0
[ -d "$OLD/cnc_eyes.old" ] && HELD_SEEN=1
[ "$HELD_SEEN" = 1 ] \
    && pass "a start while the stepped-aside file is still held leaves it alone" \
    || fail "the sweep got rid of a .old something still holds, or there never was one"

# The holder exits. Two files no journal names are planted, of the kind a search by
# pattern would take for leftovers: a player's own backup beside a file the install
# ships, in a subfolder, and one whose original is missing.
rm -f "$OLD/cnc_eyes.old/held"
printf 'my own copy\n' > "$OLD/missions/data.bin.old"
printf 'the only copy\n' > "$OLD/orphan.dat.old"
PLANTED=$(stat -f '%i %Fm' "$OLD/missions/data.bin.old" "$OLD/orphan.dat.old" | tr '\n' ' ')
launcher/opencnc3d-launcher --dir "$OLD" --check > "$WORK/sweep.log" 2>&1 || true
[ "$HELD_SEEN" = 1 ] && [ ! -e "$OLD/cnc_eyes.old" ] && [ ! -e "$OLD/opencnc3d-update.done" ] \
    && pass "once nothing holds it, the next start removes it by its kept name, and then the name" \
    || fail "the next start did not remove a stepped-aside file nothing holds"
[ "$(stat -f '%i %Fm' "$OLD/missions/data.bin.old" "$OLD/orphan.dat.old" 2>/dev/null | tr '\n' ' ')" = "$PLANTED" ] \
    && grep -qx "my own copy" "$OLD/missions/data.bin.old" \
    && grep -qx "the only copy" "$OLD/orphan.dat.old" && [ ! -e "$OLD/orphan.dat" ] \
    && pass "a .old no journal names is left exactly as it was, beside a shipped file or with no original" \
    || fail "a start removed or renamed a .old that no journal names"
rm -f "$OLD/missions/data.bin.old" "$OLD/orphan.dat.old" "$OLD/orphan.dat"

# ---------------------------------------------------------------- round B
# NO MANIFEST. This is what every release before this one looks like, so it is
# the common case rather than a degraded one: the launcher must take the FULL
# package and must still refuse a short download on its length alone.
say "round B, a release with no manifest asset"
rm -f "$SITE/OpenCNC3D-v0.6.3-manifest.txt"

# The site is restarted on the SAME port, so the launcher built against it does
# not have to be rebuilt. That the restart actually took the port is checked
# rather than assumed: a failed bind would leave every assertion below talking
# to nothing, and "connection refused" reads a lot like a launcher bug.
restart_site() {   # restart_site [--short <name>]; SITE_DIR names another folder of zips
    kill "$SITEPID" 2>/dev/null || true
    wait "$SITEPID" 2>/dev/null || true
    : > "$WORK/site.log"
    tools/launcher/fake-site.py "${SITE_DIR:-$SITE}" --tag v0.6.3 --port "$PORT" "$@" \
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
restart_site --short OpenCNC3D-macos-v0.6.3.zip
printf 'version 0.6.1\ndata_id %s\n' "$MACDATA" > "$OLD/opencnc3d-install.txt"
printf 'OLD BINARY\n' > "$OLD/opencnc3d"
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/short.log" 2>&1 || true
sed 's/^/   /' "$WORK/short.log"
grep -q "stopped early" "$WORK/short.log" \
    && pass "with no hash published, a short download is refused on its length" \
    || fail "a truncated download was accepted"
grep -q "OLD BINARY" "$OLD/opencnc3d" \
    && pass "and nothing on disk was touched" \
    || fail "the refused update still wrote to the install"

# 14. A DAMAGED ENTRY WITH NO HASH TO CATCH IT. The length is right, so only the
#     archive's own CRC-32 can tell. unzip is asked first, so the damage is known
#     to be real rather than assumed.
say "round B, a damaged zip and no hash"
[ "$DAMAGED" = 1 ] && ! unzip -tqq "$WORK/damaged-full.zip" >/dev/null 2>&1 \
    && pass "the damaged package has one changed byte, and unzip -t reports a bad CRC" \
    || fail "the damaged package is not damaged as intended (changed $DAMAGED), so this round proves nothing"
cp "$SITE/OpenCNC3D-macos-v0.6.3.zip" "$WORK/good-full.zip"
cp "$WORK/damaged-full.zip" "$SITE/OpenCNC3D-macos-v0.6.3.zip"
restart_site
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/damaged.log" 2>&1 || true
sed 's/^/   /' "$WORK/damaged.log"
grep -q 'GET /files/OpenCNC3D-macos-v0.6.3.zip' "$WORK/site.log" \
    && pass "the damaged full package was the one taken" \
    || fail "the damaged package was not downloaded, so this round proves nothing"
grep -q "result:    FAILED" "$WORK/damaged.log" && grep -q "damaged" "$WORK/damaged.log" \
    && pass "an entry that does not match its CRC fails the update" \
    || fail "a corrupt entry was installed and the update reported success"
grep -q "OLD BINARY" "$OLD/opencnc3d" \
    && pass "and the corrupt binary is not on disk" \
    || fail "the corrupt binary was left in the install"
STRAY=$(find "$OLD" -name '*.old*' -o -name 'opencnc3d-update.journal' | head -3 | tr '\n' ' ')
[ -z "$STRAY" ] \
    && pass "and the undo left nothing behind" \
    || fail "the undone update left files behind: $STRAY"
cp "$WORK/good-full.zip" "$SITE/OpenCNC3D-macos-v0.6.3.zip"

say "round B, update"
restart_site
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/full.log" 2>&1 || true
sed 's/^/   /' "$WORK/full.log"
grep -q 'GET /files/OpenCNC3D-macos-v0.6.3.zip' "$WORK/site.log" \
    && pass "with no fingerprint to compare, the FULL package was taken" \
    || fail "it did not fall back to the full package"
grep -q "result:    installed v0.6.3" "$WORK/full.log" \
    && pass "and the update succeeded" || fail "the full-package update failed"
grep -q "NEW BINARY" "$OLD/opencnc3d" \
    && pass "the game binary on disk is the new one" \
    || fail "the game binary was not replaced"
grep -q "data_id unknown" "$OLD/opencnc3d-install.txt" \
    && pass "the install record records that it could not learn a fingerprint" \
    || fail "the install record claims a fingerprint it never saw"

# ---------------------------------------------------------------- round C
# 12. THE WINDOWS SHAPE. The same binaries, zipped inside a folder, with a fresh
#     manifest so the hash still matches and the small zip is still the choice.
say "round C, a binary-only zip wrapped in a folder"
cp "$WORK/wrapped-bins.zip" "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip"
tools/launcher/make-manifest.sh v0.6.3 "$SITE/OpenCNC3D-v0.6.3-manifest.txt" \
    "$SITE"/*.zip > /dev/null
restart_site
printf 'version 0.6.1\ndata_id %s\n' "$MACDATA" > "$OLD/opencnc3d-install.txt"
printf 'OLD BINARY\n' > "$OLD/opencnc3d"
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/wrapped.log" 2>&1 || true
sed 's/^/   /' "$WORK/wrapped.log"
grep -q 'GET /files/OpenCNC3D-macos-v0.6.3-bins.zip' "$WORK/site.log" \
    && pass "the wrapped small zip was the one taken" \
    || fail "the wrapped small zip was not taken, so this round proves nothing"
grep -q "result:    installed v0.6.3" "$WORK/wrapped.log" \
    && pass "and the update succeeded" || fail "the wrapped update failed"
grep -q "NEW BINARY" "$OLD/opencnc3d" \
    && pass "the binaries landed at the top of the install" \
    || fail "the top of the install still holds the OLD binary"
[ -e "$OLD/OpenCNC3D-macos-v0.6.3" ] \
    && fail "the update made an OpenCNC3D-macos-v0.6.3 folder inside the install" \
    || pass "and no folder named after the zip was made inside it"

# ---------------------------------------------------------------- round D
# THE HARD CASES, from the hard zip: BUILD-ID.txt and opencnc3d-install.txt first,
# then opencnc3d, then ro/p, then 32 MB of zeros, then the engine.
say "round D, the hard cases"
cp "$WORK/hard-bins.zip" "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip"
tools/launcher/make-manifest.sh v0.6.3 "$SITE/OpenCNC3D-v0.6.3-manifest.txt" \
    "$SITE"/*.zip > /dev/null
restart_site
reset_old() {
    # Whatever the leg before left is cleared first, a failed leg's leftovers above all,
    # so no leg is judged on the one before it. Every leg asserts its own leftovers
    # before this runs. dosmenu.pack is put back too: round E moves it, and a launcher
    # that cannot load it stops at a message box.
    find "$OLD" \( -name '*.old*' -o -name 'opencnc3d-update.journal' -o -name 'opencnc3d-update.zip' \
        -o -name 'opencnc3d-update.done' -o -name '*.opencnc3d-new' \) -exec rm -rf {} + 2>/dev/null || true
    cp playable/dosmenu.pack "$OLD/dosmenu.pack"
    printf 'version 0.6.1\ndata_id %s\n' "$MACDATA" > "$OLD/opencnc3d-install.txt"
    printf 'v0.6.1  macos\n' > "$OLD/BUILD-ID.txt"
    printf 'OLD BINARY\n' > "$OLD/opencnc3d"
    printf 'OLD ENGINE\n' > "$OLD/TiberianDawn.dylib"
    rm -f "$OLD/big.bin"
    mkdir -p "$OLD/ro"
    chmod 0755 "$OLD/ro"
    printf 'OLD P\n' > "$OLD/ro/p"
}
no_leftovers() {
    [ -z "$(find "$OLD" -name '*.old*' -o -name 'opencnc3d-update.journal' -o -name 'opencnc3d-update.done' \
              -o -name 'opencnc3d-update.zip' -o -name '*.opencnc3d-new' -o -name big.bin | head -1)" ]
}
leftovers() {
    find "$OLD" -name '*.old*' -o -name 'opencnc3d-update.*' -o -name '*.opencnc3d-new' -o -name big.bin \
        | tr '\n' ' '
}
# EVERY LAUNCHER FROM HERE ON RUNS UNDER A TIME LIMIT. A launcher that cannot load its
# menu art stops at a message box and waits for a click, and a test that waits with it
# never ends. SIGXFSZ is exit status 153 and is the only kill a killed-part-way leg
# accepts; the time limit's SIGALRM is 142, and means the launcher stopped somewhere else.
start_bounded() {   # start_bounded <log>: one --check, stopped after 20 seconds
    perl -e 'alarm 20; exec { $ARGV[0] } @ARGV' launcher/opencnc3d-launcher --dir "$OLD" --check \
        > "$1" 2>&1 || true
}
kill_update() {   # kill_update <file size limit in 512-byte blocks> <log>: sets KILLRC
    KILLRC=0
    ( ulimit -f "$1"; exec perl -e 'alarm 60; exec { $ARGV[0] } @ARGV' \
        launcher/opencnc3d-launcher --dir "$OLD" --update ) > "$2" 2>&1 || KILLRC=$?
}

# 15. A FILE THAT CANNOT BE REPLACED WHERE IT STANDS. ro/p is writable, but its folder
#     is 0555, so it cannot be renamed and nothing new can be made beside it. opencnc3d is
#     written before it.
say "round D, a file that can be written but not moved aside"
reset_old
chmod 0555 "$OLD/ro"
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/hard-ro.log" 2>&1 || true
chmod 0755 "$OLD/ro"
sed 's/^/   /' "$WORK/hard-ro.log"
grep -q 'GET /files/OpenCNC3D-macos-v0.6.3-bins.zip' "$WORK/site.log" \
    && pass "the hard zip was the one taken" \
    || fail "the hard zip was not taken, so this round proves nothing"
grep -q "result:    FAILED" "$WORK/hard-ro.log" && grep -q "ro/p" "$WORK/hard-ro.log" \
    && pass "a file that can be written but not moved aside fails the update, by name" \
    || fail "a file that could not be moved aside was written over, or the failure does not name it"
grep -q "OLD P" "$OLD/ro/p" \
    && pass "and that file still holds its old contents" \
    || fail "the file was overwritten in place, with no copy to put back"
grep -q "OLD BINARY" "$OLD/opencnc3d" \
    && pass "opencnc3d, written before it, was put back" \
    || fail "opencnc3d was left new beside a file that was not replaced"
grep -q "v0.6.1" "$OLD/BUILD-ID.txt" && grep -q "version 0.6.1" "$OLD/opencnc3d-install.txt" \
    && pass "neither commit marker names the new build" \
    || fail "a commit marker names a build that did not install"
no_leftovers \
    && pass "and nothing was left behind" \
    || fail "the undone update left files behind"

# 16. KILLED PART WAY. The file size limit kills the launcher inside big.bin, after
#     opencnc3d and ro/p are written and, in zip order, after both commit markers.
say "round D, a launcher killed part way"
reset_old
kill_update 8192 "$WORK/hard-kill.log"
sed 's/^/   /' "$WORK/hard-kill.log"
echo "   exit status $KILLRC"
[ "$KILLRC" = 153 ] && ! grep -q "result:" "$WORK/hard-kill.log" \
    && pass "the launcher was killed before it could report, so no undo ran in it" \
    || fail "the launcher was not killed part way, so this round proves nothing"
grep -q "NEW BINARY" "$OLD/opencnc3d" && grep -q "NEW P" "$OLD/ro/p" \
    && [ -n "$(find "$OLD" -name 'big.bin*' | head -1)" ] \
    && pass "it died inside big.bin, with opencnc3d and ro/p already written" \
    || fail "the kill did not land after opencnc3d and ro/p, so this round proves nothing (opencnc3d: $(head -c 40 "$OLD/opencnc3d" 2>&1 | tr '\n' ' '), ro/p: $(head -c 40 "$OLD/ro/p" 2>&1 | tr '\n' ' '), found: $(cd "$OLD" && find . -name 'big.bin*' -o -name '*.opencnc3d-new' -o -name 'opencnc3d-update.*' | tr '\n' ' '))"
[ ! -e "$OLD/big.bin" ] \
    && pass "and nothing half written is on big.bin's own name: the new copy was still beside it" \
    || fail "a kill left a partial big.bin, without its executable bit, on its real name"
[ -x "$OLD/opencnc3d" ] \
    && pass "opencnc3d, renamed in before the kill, already had its executable bit" \
    || fail "a file on its real name was left without its executable bit"
grep -q "v0.6.1" "$OLD/BUILD-ID.txt" \
    && pass "BUILD-ID.txt still names v0.6.1, although the zip lists it first" \
    || fail "BUILD-ID.txt names the new build in a folder the update never finished"
grep -q "version 0.6.1" "$OLD/opencnc3d-install.txt" \
    && pass "and so does opencnc3d-install.txt, although the zip lists it second" \
    || fail "opencnc3d-install.txt claims the new build in a folder the update never finished"
[ -f "$OLD/opencnc3d-update.journal" ] \
    && pass "the journal of the unfinished update is on disk" \
    || fail "a killed update left no journal to undo it from"
start_bounded "$WORK/hard-recover.log"
sed 's/^/   /' "$WORK/hard-recover.log"
grep -q "OLD BINARY" "$OLD/opencnc3d" && grep -q "OLD P" "$OLD/ro/p" \
    && pass "the next start undid it: opencnc3d and ro/p are the old ones again" \
    || fail "the next start did not undo the killed update"
no_leftovers \
    && pass "and removed the partial big.bin, the .old copies, the journal and the download" \
    || fail "the next start left pieces of the killed update: $(leftovers)"
grep -q "result:    v0.6.3 is available" "$WORK/hard-recover.log" \
    && pass "and the update is offered again" \
    || fail "the undone update is not offered again"

# 18. THE WINDOWS INSTALLER RUN AGAIN OVER A KILLED UPDATE, which is the repair a
#     player on Windows reaches for. NSIS cannot run here, so what it does is read out
#     of tools/win/installer/opencnc3d.nsi and done by hand: the journal is renamed to
#     opencnc3d-update.done, and then removed, if and only if the script does both before
#     its File /r, then every packaged file is written over the folder in place, which
#     is all File /r does. NSIS Rename refuses a taken name, as the move here does.
say "round D, the Windows installer run again over a killed update"
reset_old
kill_update 8192 "$WORK/hard-kill2.log"
[ "$KILLRC" = 153 ] && [ -f "$OLD/opencnc3d-update.journal" ] \
    && pass "a second update was killed part way, and left its journal" \
    || fail "the second update was not killed part way, so this round proves nothing"
NSI=tools/win/installer/opencnc3d.nsi
if awk '/^[[:space:]]*Rename "\$INSTDIR\\opencnc3d-update\.journal" "\$INSTDIR\\opencnc3d-update\.done"/ { if (!r) r = NR }
        /^[[:space:]]*Delete "\$INSTDIR\\opencnc3d-update\.journal"/ { if (!d) d = NR }
        /^[[:space:]]*File \/r/ { if (!f) f = NR }
        END { exit !(r && d && f && r < d && d < f) }' "$NSI"; then
    pass "the installer renames the journal to opencnc3d-update.done, then removes it, before it writes any file"
    [ -e "$OLD/opencnc3d-update.done" ] || mv "$OLD/opencnc3d-update.journal" "$OLD/opencnc3d-update.done"
    rm -f "$OLD/opencnc3d-update.journal"
else
    fail "the installer writes its files and leaves the journal, so the next start undoes them"
fi
( cd "$HARD" && tar cf - . ) | ( cd "$OLD" && tar xf - )
start_bounded "$WORK/reinstall1.log"
start_bounded "$WORK/reinstall2.log"
sed 's/^/   /' "$WORK/reinstall2.log"
grep -q "NEW BINARY" "$OLD/opencnc3d" && grep -q "NEW ENGINE" "$OLD/TiberianDawn.dylib" \
    && grep -q "NEW P" "$OLD/ro/p" && grep -q "v0.6.3" "$OLD/BUILD-ID.txt" \
    && grep -q "version 0.6.3" "$OLD/opencnc3d-install.txt" \
    && [ "$(stat -f '%z' "$OLD/big.bin" 2>/dev/null)" = 33554432 ] \
    && pass "the reinstalled folder is the package, whole, after two starts" \
    || fail "the starts after a reinstall undid part of it"
[ -z "$(find "$OLD" -name '*.old*' -o -name 'opencnc3d-update.*' -o -name '*.opencnc3d-new' | head -1)" ] \
    && pass "and the killed update's .old copies and half-written copy are gone" \
    || fail "the reinstalled folder still holds pieces of the killed update: $(leftovers)"
grep -q "result:    up to date" "$WORK/reinstall2.log" \
    && pass "and it reports up to date, which is now the truth" \
    || fail "the reinstalled folder does not report up to date"
rm -f "$OLD/big.bin"

say "round D, the same zip with nothing in the way"
reset_old
launcher/opencnc3d-launcher --dir "$OLD" --update > "$WORK/hard-ok.log" 2>&1 || true
sed 's/^/   /' "$WORK/hard-ok.log"
grep -q "result:    installed v0.6.3" "$WORK/hard-ok.log" && grep -q "NEW P" "$OLD/ro/p" \
    && grep -q "v0.6.3" "$OLD/BUILD-ID.txt" \
    && [ "$(stat -f '%z' "$OLD/big.bin" 2>/dev/null)" = 33554432 ] \
    && pass "installs completely, so the two failures above came from the stand-ins, not the zip" \
    || fail "the hard zip does not install even with nothing in the way"
rm -f "$OLD/big.bin"

# ---------------------------------------------------------------- round E
# STATES BETWEEN TWO RENAMES, planted. Each is a real journal line with the rename it
# describes half taken. The failure both legs guard against ends in a message box,
# which is why start_bounded is not optional here.

# 19. Killed after the new install record was renamed in, before the commit line.
say "round E, a start after a kill that had just renamed the new record in"
reset_old
cp "$OLD/opencnc3d-install.txt" "$OLD/opencnc3d-install.txt.old"
cp "$HARD/opencnc3d-install.txt" "$OLD/opencnc3d-install.txt"
printf 'opencnc3d update journal 1\nA 1 opencnc3d-install.txt\n' > "$OLD/opencnc3d-update.journal"
start_bounded "$WORK/plant-record.log"
sed 's/^/   /' "$WORK/plant-record.log"
grep -q "installed: v0.6.1" "$WORK/plant-record.log" \
    && grep -q "result:    v0.6.3 is available" "$WORK/plant-record.log" \
    && pass "the version is read after the unfinished update is undone, so the update is offered" \
    || fail "the start read the killed update's record, and says the old folder is up to date"
grep -q "version 0.6.1" "$OLD/opencnc3d-install.txt" && no_leftovers \
    && pass "and the record on disk is the old one again, with nothing left over" \
    || fail "the planted update was not undone: $(leftovers)"

# 20. Killed between dosmenu.pack's two renames: the old pack stepped aside, the new
#     one not yet renamed in, so the folder has no dosmenu.pack at all.
say "round E, a start with dosmenu.pack between its two renames"
reset_old
mv "$OLD/dosmenu.pack" "$OLD/dosmenu.pack.old"
printf 'opencnc3d update journal 1\nA 1 dosmenu.pack\n' > "$OLD/opencnc3d-update.journal"
start_bounded "$WORK/plant-pack.log"
sed 's/^/   /' "$WORK/plant-pack.log"
cmp -s "$OLD/dosmenu.pack" playable/dosmenu.pack && no_leftovers \
    && pass "the start found its folder by the journal and put dosmenu.pack back" \
    || fail "the start never reached the recovery, so dosmenu.pack is still missing: $(leftovers)"
grep -q "result:    v0.6.3 is available" "$WORK/plant-pack.log" \
    && pass "and went on to check, instead of stopping at could not find its game files" \
    || fail "the start stopped before it checked"

# ---------------------------------------------------------------- round F
# 21. THE LAUNCHER'S OWN FILES. The flat binary-only zip from round A, which carries no
#     install record, and a changelog of about 2 MB from the site, under a 512 KB file
#     size limit: every entry fits, the download fits, and CHANGELOG.txt does not.
say "round F, a launcher killed while it writes the changelog"
cp "$WORK/good-bins.zip" "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip"
tools/launcher/make-manifest.sh v0.6.3 "$SITE/OpenCNC3D-v0.6.3-manifest.txt" \
    "$SITE"/*.zip > /dev/null
restart_site --changelog-pad 2000000
reset_old
printf 'the notes of v0.6.1\n' > "$OLD/CHANGELOG.txt"
unzip -Z1 "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip" | grep -q 'opencnc3d-install.txt' \
    && fail "the binary-only zip carries a record, so this round proves nothing" \
    || pass "the binary-only zip carries no install record, as the real ones do not"
kill_update 1024 "$WORK/notes-kill.log"
sed 's/^/   /' "$WORK/notes-kill.log"
echo "   exit status $KILLRC"
[ "$KILLRC" = 153 ] && ! grep -q "result:" "$WORK/notes-kill.log" \
    && grep -q 'GET /files/OpenCNC3D-macos-v0.6.3-bins.zip' "$WORK/site.log" \
    && grep -q "NEW BINARY" "$OLD/opencnc3d" \
    && pass "the launcher was killed after the binaries were in, writing the site's changelog" \
    || fail "the launcher was not killed where this round needs it, so it proves nothing"
grep -q "version 0.6.1" "$OLD/opencnc3d-install.txt" \
    && pass "the install record still names v0.6.1: it is the update's last step, not a rewrite after it" \
    || fail "the killed update left a record claiming it: $(tr '\n' ' ' < "$OLD/opencnc3d-install.txt")"
grep -qx "the notes of v0.6.1" "$OLD/CHANGELOG.txt" \
    && pass "and CHANGELOG.txt is the old notes, whole, not half of the new ones" \
    || fail "CHANGELOG.txt was left half written"
start_bounded "$WORK/notes-recover.log"
grep -q "OLD BINARY" "$OLD/opencnc3d" && grep -q "OLD ENGINE" "$OLD/TiberianDawn.dylib" \
    && grep -q "v0.6.1" "$OLD/BUILD-ID.txt" && no_leftovers \
    && pass "the next start undid the whole update" \
    || fail "the next start did not undo the update: $(leftovers)"
grep -q "result:    v0.6.3 is available" "$WORK/notes-recover.log" \
    && pass "and offers it again" \
    || fail "the undone update is not offered again"
restart_site

# ---------------------------------------------------------------- round G
# 22. THE APP OVER AN UNFINISHED UPDATE. A folder that passes the app's own "is this
#     the game" test, a launcher and a game that only write down that they ran, and
#     the app's executable built with its dialogs going to the same log.
say "round G, the macOS app over a folder with an unfinished update"
APPDIR="$WORK/appcase"
mkdir -p "$APPDIR/OpenCNC3D.app/Contents/MacOS"
for f in dosmenu.pack cameos.pack dossidebar.pack dosinfantry.pack TiberianDawn.dylib; do
    : > "$APPDIR/$f"
done
printf 'version 0.6.1\n' > "$APPDIR/opencnc3d-install.txt"
# An empty home folder, so that an app which does not recognise its own folder looks
# for the game where people unzip things and finds nothing, rather than a real one.
mkdir -p "$WORK/apphome"
printf '#!/bin/sh\necho game >> "$SELFTEST_LOG"\n' > "$APPDIR/opencnc3d"
printf '#!/bin/sh\necho "launcher $0" >> "$SELFTEST_LOG"\n' > "$WORK/fake-launcher"
chmod +x "$APPDIR/opencnc3d" "$WORK/fake-launcher"
cat > "$WORK/app-stub.h" <<'STUB'
#include <stdio.h>
#include <stdlib.h>
static int selftest_system(const char *cmd)
{
    FILE *f = fopen(getenv("SELFTEST_LOG"), "a");
    if (f) {
        fprintf(f, "dialog: %s\n", getenv("MSG") ? getenv("MSG") : cmd);
        fclose(f);
    }
    return 0;
}
/* The folder chooser the app offers when it cannot find the game: logged, and
 * answered as if the player pressed Quit, so nothing appears on screen. */
static FILE *selftest_popen(const char *cmd, const char *mode)
{
    FILE *f = fopen(getenv("SELFTEST_LOG"), "a");
    (void)mode;
    if (f) {
        fprintf(f, "dialog: %s\n", getenv("MSG") ? getenv("MSG") : cmd);
        fclose(f);
    }
    return NULL;
}
#define system selftest_system
#define popen selftest_popen
STUB
APP="$APPDIR/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
cc -O0 -w -include "$WORK/app-stub.h" -o "$APP" launcher/mac_launch.c 2> "$WORK/app-build.log" \
    || { cat "$WORK/app-build.log"; fail "launcher/mac_launch.c did not build"; }
run_app() {   # run_app <log>
    : > "$1"
    # exec { $ARGV[0] } @ARGV, not exec @ARGV: perl hands a lone argument to the shell
    # when it holds a shell character, and nothing promises a path here holds none.
    HOME="$WORK/apphome" SELFTEST_LOG="$1" perl -e 'alarm 20; exec { $ARGV[0] } @ARGV' "$APP" \
        > /dev/null 2>&1 || true
}
app_case() {   # app_case <launcher: exec|plain|none> <old launcher: exec|none> <journal: yes|no>
    rm -f "$APPDIR/opencnc3d-launcher" "$APPDIR/opencnc3d-launcher.old" "$APPDIR/opencnc3d-update.journal"
    case "$1" in
        exec) cp -p "$WORK/fake-launcher" "$APPDIR/opencnc3d-launcher" ;;
        plain) cp "$WORK/fake-launcher" "$APPDIR/opencnc3d-launcher" && chmod 0644 "$APPDIR/opencnc3d-launcher" ;;
    esac
    [ "$2" = exec ] && cp -p "$WORK/fake-launcher" "$APPDIR/opencnc3d-launcher.old"
    [ "$3" = yes ] && printf 'opencnc3d update journal 1\nA 1 opencnc3d-launcher\n' > "$APPDIR/opencnc3d-update.journal"
    run_app "$WORK/app.log"
}
app_case exec none no
grep -q "^launcher .*/opencnc3d-launcher$" "$WORK/app.log" && ! grep -q "^game" "$WORK/app.log" \
    && pass "the app starts the launcher" \
    || fail "the app did not start the launcher: $(tr '\n' ' ' < "$WORK/app.log")"
app_case plain none no
grep -q "^game" "$WORK/app.log" \
    && pass "with no launcher that can run and no update unfinished, it still starts the game" \
    || fail "a folder with an unusable launcher can no longer play: $(tr '\n' ' ' < "$WORK/app.log")"
app_case plain none yes
! grep -q "^game" "$WORK/app.log" && grep -q "did not finish" "$WORK/app.log" \
    && pass "while an update is unfinished and the launcher cannot run, it does not start the game, and says why" \
    || fail "the app started the game over an unfinished update: $(tr '\n' ' ' < "$WORK/app.log")"
app_case none exec yes
grep -q "^launcher .*/opencnc3d-launcher.old$" "$WORK/app.log" && ! grep -q "^game" "$WORK/app.log" \
    && pass "and between the launcher's two renames it starts the stepped-aside launcher, which finishes the update" \
    || fail "between the launcher's two renames the app did not start the launcher that can finish the update: $(tr '\n' ' ' < "$WORK/app.log")"
# Between the two renames of a file the app itself looks for, the launcher has to be
# started all the same: it is the only thing that finishes the update.
app_aside() {   # app_aside <file> <journal: yes|no>
    rm -f "$APPDIR/opencnc3d-launcher.old" "$APPDIR/opencnc3d-update.journal"
    cp -p "$WORK/fake-launcher" "$APPDIR/opencnc3d-launcher"
    mv "$APPDIR/$1" "$APPDIR/$1.old"
    [ "$2" = yes ] && printf 'opencnc3d update journal 1\nA 1 %s\n' "$1" > "$APPDIR/opencnc3d-update.journal"
    run_app "$WORK/app.log"
    mv "$APPDIR/$1.old" "$APPDIR/$1"
    rm -f "$APPDIR/opencnc3d-update.journal"
}
for f in opencnc3d TiberianDawn.dylib dosmenu.pack cameos.pack; do
    app_aside "$f" yes
    grep -q "^launcher .*/opencnc3d-launcher$" "$WORK/app.log" && ! grep -q "^game" "$WORK/app.log" \
        && pass "between the two renames of $f the app still recognises the folder and starts the launcher" \
        || fail "between the two renames of $f no launcher was started to finish the update: $(tr '\n' ' ' < "$WORK/app.log")"
done
app_aside opencnc3d no
! grep -q "^launcher" "$WORK/app.log" && ! grep -q "^game" "$WORK/app.log" \
    && pass "and with no journal, a folder missing opencnc3d is still not taken for the game" \
    || fail "the app took a folder with no opencnc3d and no unfinished update for the game: $(tr '\n' ' ' < "$WORK/app.log")"

# 31. AWAY FROM THE GAME. macOS runs a downloaded, unsigned app from a copy of the bundle
#     alone, so the app has to go and find the folder the zip expanded to. A home folder
#     holds that folder on the Desktop, under the zip's own name, and, in Downloads,
#     which is looked in first, a decoy with a whole game in it whose name only starts
#     with the same five letters. Each launcher in them writes down which one it is and
#     where it was started.
say "round G, the macOS app and its script started away from the game"
LHOME="$WORK/lost-home"
mkgame() {   # mkgame <folder> <the word its launcher writes down>
    mkdir -p "$1"
    for f in dosmenu.pack cameos.pack dossidebar.pack dosinfantry.pack TiberianDawn.dylib; do
        : > "$1/$f"
    done
    printf '#!/bin/sh\necho game >> "$SELFTEST_LOG"\n' > "$1/opencnc3d"
    printf '#!/bin/sh\necho "%s $(pwd -P)" >> "$SELFTEST_LOG"\n' "$2" > "$1/opencnc3d-launcher"
    chmod +x "$1/opencnc3d" "$1/opencnc3d-launcher"
}
mkgame "$LHOME/Downloads/Decoyapp" decoy
mkgame "$LHOME/Desktop/OpenCNC3D-macos-v0.8.0" found
lost_run() {   # lost_run <log> <the bundle's executable> [PATH prefix]
    : > "$1"
    ( cd "$WORK" && HOME="$LHOME" SELFTEST_LOG="$1" PATH="${3:+$3:}$PATH" \
        perl -e 'alarm 20; exec { $ARGV[0] } @ARGV' "$2" ) > /dev/null 2>&1 || true
}
mkdir -p "$WORK/lost-app/OpenCNC3D.app/Contents/MacOS"
cp -p "$APP" "$WORK/lost-app/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
lost_run "$WORK/lost-app.log" "$WORK/lost-app/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
grep -q "^found .*/Desktop/OpenCNC3D-macos-v0\.8\.0$" "$WORK/lost-app.log" \
    && ! grep -q -e "^decoy" -e "^game" -e "Could not find" "$WORK/lost-app.log" \
    && pass "the app on its own finds OpenCNC3D-macos-v0.8.0 by name, passes over Decoyapp, and starts the launcher there" \
    || fail "the app on its own did not find the folder the zip expanded to: $(tr '\n' ' ' < "$WORK/lost-app.log")"
# The bundle's tracked executable is a script, which the packagers replace with the
# compiled app and which stays the fallback when that does not build. Its dialogs go
# through osascript and its quarantine sweep through xattr, so both are stand-ins on
# PATH that write down what they were asked and show nothing.
mkdir -p "$WORK/stub-bin" "$WORK/lost-script/OpenCNC3D.app/Contents/MacOS"
printf '#!/bin/sh\necho "dialog: $*" >> "$SELFTEST_LOG"\n' > "$WORK/stub-bin/osascript"
printf '#!/bin/sh\nexit 0\n' > "$WORK/stub-bin/xattr"
chmod +x "$WORK/stub-bin/osascript" "$WORK/stub-bin/xattr"
cp -p tools/launchers/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch \
    "$WORK/lost-script/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
lost_run "$WORK/lost-script.log" "$WORK/lost-script/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch" \
    "$WORK/stub-bin"
grep -q "^found .*/Desktop/OpenCNC3D-macos-v0\.8\.0$" "$WORK/lost-script.log" \
    && ! grep -q -e "^decoy" -e "^game" -e "^dialog" "$WORK/lost-script.log" \
    && pass "and so does the bundle's script, with no dialog" \
    || fail "the bundle's script on its own did not find the folder the zip expanded to: $(tr '\n' ' ' < "$WORK/lost-script.log")"

# ---------------------------------------------------------------- round H
# 23-26. HOSTILE FOLDERS. The launcher is started over folders it has no business
# changing, and over installs arranged to lead it outside. Every entry it must not
# touch is compared before and after: its type, permissions, size, mtime to the
# nanosecond, inode, link target and the hash of its bytes. Inside an install the
# only differences allowed are the update's own files, named per leg.
snap() {   # snap <folder> [a top-level name the launcher may change]...
    _root=$1
    shift
    ( cd "$_root" && find . -print | LC_ALL=C sort | while IFS= read -r _p; do
          _skip=0
          for _x in "$@"; do
              if [ "$_p" = "./$_x" ]; then _skip=1; fi
          done
          if [ "$_skip" = 1 ]; then continue; fi
          if [ "$_p" = . ] && [ $# -gt 0 ]; then
              # Its own mtime moves when the update's own files come and go in it.
              stat -f '%N|%HT|%Lp|%i' "$_p"
              continue
          fi
          stat -f '%N|%HT|%Lp|%z|%Fm|%i|%Y' "$_p"
          if [ -f "$_p" ] && [ ! -L "$_p" ]; then shasum < "$_p" | cut -d' ' -f1; fi
      done )
}
same() {   # same <before> <after> <what it proves>
    if cmp -s "$1" "$2"; then
        pass "$3"
    else
        fail "$3 -- it did not: $(diff "$1" "$2" | grep '^[<>]' | head -6 | tr '\n' ' ')"
    fi
}
h_run() {   # h_run <log> <folder to start in> <launcher> <arguments>...: bounded
    _log=$1
    _cwd=$2
    shift 2
    ( cd "$_cwd" && perl -e 'alarm 20; exec { $ARGV[0] } @ARGV' "$@" ) > "$_log" 2>&1 || true
    sed 's/^/   /' "$_log"
}
decoys() {   # decoys <folder>: a person's own files, named the way an update's leftovers are
    mkdir -p "$1/Documents/taxes" "$1/Pictures" "$1/.config"
    printf 'the current report\n' > "$1/Documents/report.txt"
    printf 'my own backup of the report\n' > "$1/Documents/report.txt.old"
    printf 'the only copy of this sheet\n' > "$1/Documents/taxes/2025.xls.old"
    printf 'current notes\n' > "$1/notes.md"
    printf 'first backup of the notes\n' > "$1/notes.md.old1"
    printf 'second backup of the notes\n' > "$1/notes.md.old2"
    printf 'a photo\n' > "$1/Pictures/beach.jpg"
    printf 'the photo before an edit\n' > "$1/Pictures/beach.jpg.old"
    printf 'a draft some other program left\n' > "$1/Documents/draft.opencnc3d-new"
    printf 'nothing to do with the game\n' > "$1/opencnc3d-update.zip"
    printf 'nor is this\n' > "$1/opencnc3d-update.zip.part"
    printf 'hidden settings\n' > "$1/.config/settings.old"
}
mkinstall() {   # mkinstall <folder>: an install at v0.6.1, as a release package leaves one
    mkdir -p "$1/missions/user_maps"
    cp playable/dosmenu.pack "$1/"
    printf 'version 0.6.1\ndata_id %s\n' "$MACDATA" > "$1/opencnc3d-install.txt"
    printf 'v0.6.1  macos\n' > "$1/BUILD-ID.txt"
    printf 'OLD BINARY\n' > "$1/opencnc3d"
    chmod +x "$1/opencnc3d"
    printf 'OLD ENGINE\n' > "$1/TiberianDawn.dylib"
    printf 'a data file that does not change between the two builds\n' > "$1/missions/data.bin"
}
outside_lines() {   # outside_lines <absolute outside folder>: journal lines that leave the folder
    printf 'N ../outside/victim1.txt\n'
    printf 'A 1 ../outside/victim2.txt\n'
    printf 'N %s/victim3.txt\n' "$1"
    printf 'A 1 sub/../../outside/victim2.txt\n'
    printf 'N missions/../../outside/victim3.txt\n'
    printf 'N ..\\outside\\victim1.txt\n'
    printf 'A 1 ..\n'
}
HOST="$WORK/hostile"
OUTSIDE="$HOST/outside"
mkdir -p "$OUTSIDE"
printf 'victim one\n' > "$OUTSIDE/victim1.txt"
printf 'victim two\n' > "$OUTSIDE/victim2.txt"
printf 'its backup\n' > "$OUTSIDE/victim2.txt.old"
printf 'victim three\n' > "$OUTSIDE/victim3.txt"
printf 'its draft\n' > "$OUTSIDE/victim3.txt.opencnc3d-new"
LAUNCHER="$ROOT/launcher/opencnc3d-launcher"

# 23. Named with --dir: a folder holding the game's menu pack, so the launcher gets
#     as far as a check, and no install record.
say "round H, a folder that is not an install, full of decoys, named with --dir"
H1="$HOST/not-an-install"
mkdir -p "$H1"
decoys "$H1"
cp playable/dosmenu.pack "$H1/"
snap "$H1" > "$WORK/h1.before"
h_run "$WORK/h1.log" "$ROOT" "$LAUNCHER" --dir "$H1" --check
snap "$H1" > "$WORK/h1.after"
grep -q "^install:   $H1\$" "$WORK/h1.log" && grep -q "^result:" "$WORK/h1.log" \
    && pass "the launcher started on that folder and got as far as a check" \
    || fail "the launcher did not get as far as a check there, so this leg proves less than it says"
same "$WORK/h1.before" "$WORK/h1.after" \
    "a folder with the menu pack and no install record is left as it was, byte for byte and mtime for mtime"

# 23. Copied by hand five folders below a home folder, with nothing of the game beside
#     it, and started with --play, which goes through the same start-up and shows no
#     dialog when it finds no game.
say "round H, a launcher copied into a folder five below a home folder"
H1B="$HOST/home"
H1BL="$H1B/g1/g2/g3/g4/OpenCNC3D"
mkdir -p "$H1BL"
decoys "$H1B"
cp -p launcher/opencnc3d-launcher "$H1BL/"
for f in launcher/*.dylib; do cp -p "$f" "$H1BL/"; done
snap "$H1B" > "$WORK/h1b.before"
h_run "$WORK/h1b.log" "$H1BL" ./opencnc3d-launcher --play
snap "$H1B" > "$WORK/h1b.after"
grep -q "could not start" "$WORK/h1b.log" \
    && pass "it started, found no game, and said so" \
    || fail "the copied launcher did not start and give up as expected, so this leg proves less"
same "$WORK/h1b.before" "$WORK/h1b.after" \
    "neither the folder it was copied into nor the home folder above it changed"

# 24. A journal planted in a folder that is not an install, naming the folder's own
#     files and files outside it.
say "round H, a journal planted in a folder that is not an install"
H2="$HOST/planted"
mkdir -p "$H2/sub"
printf 'my thesis\n' > "$H2/thesis.docx"
printf 'a photo\n' > "$H2/photo.jpg"
printf 'the photo before an edit\n' > "$H2/photo.jpg.old"
{ printf 'opencnc3d update journal 1\nN thesis.docx\nA 1 photo.jpg\n'; outside_lines "$OUTSIDE"; } \
    > "$H2/opencnc3d-update.journal"
snap "$H2" > "$WORK/h2.before"
snap "$OUTSIDE" > "$WORK/out.before"
h_run "$WORK/h2.log" "$ROOT" "$LAUNCHER" --dir "$H2" --play
snap "$H2" > "$WORK/h2.after"
snap "$OUTSIDE" > "$WORK/out.after"
same "$WORK/h2.before" "$WORK/h2.after" \
    "a planted journal with no install record changes nothing: nothing it names is removed or put back"
same "$WORK/out.before" "$WORK/out.after" "and nothing outside the folder that it names changed"

# 24. The same outside lines planted in an install, as a journal and as a done journal,
#     beside a player's own backups no journal names.
say "round H, an install with planted journals naming files outside it"
H2B="$HOST/planted-install"
mkinstall "$H2B"
printf 'a map I made\n' > "$H2B/missions/user_maps/mine.map"
printf 'my backup of it\n' > "$H2B/missions/user_maps/mine.map.old"
printf 'a save whose original I deleted\n' > "$H2B/savegame.sav.old"
{ printf 'opencnc3d update journal 1\n'; outside_lines "$OUTSIDE"; } > "$H2B/opencnc3d-update.journal"
{ printf 'opencnc3d update journal 1\n'; outside_lines "$OUTSIDE"; printf 'C\n'; } > "$H2B/opencnc3d-update.done"
snap "$H2B" opencnc3d-update.journal opencnc3d-update.done > "$WORK/h2b.before"
snap "$OUTSIDE" > "$WORK/out.before"
h_run "$WORK/h2b.log" "$ROOT" "$LAUNCHER" --dir "$H2B" --check
snap "$H2B" opencnc3d-update.journal opencnc3d-update.done > "$WORK/h2b.after"
snap "$OUTSIDE" > "$WORK/out.after"
[ ! -e "$H2B/opencnc3d-update.journal" ] && [ ! -e "$H2B/opencnc3d-update.done" ] \
    && pass "the install was recognised, and both planted journals were read and finished" \
    || fail "the planted journals were not finished, so the recovery never ran on them"
same "$WORK/out.before" "$WORK/out.after" \
    "no file outside the install that their climbing or absolute lines name changed"
same "$WORK/h2b.before" "$WORK/h2b.after" \
    "and inside, nothing but the two journals changed: a player's own .old files are as they were"

# 25. An install whose missions folder is a link to somewhere else, with a journal
#     naming files through it.
say "round H, an install whose missions folder is a link to somewhere else"
H3="$HOST/linked-install"
ELSE="$HOST/elsewhere"
mkinstall "$H3"
mkdir -p "$ELSE/missions"
printf 'data that belongs somewhere else\n' > "$ELSE/missions/data.bin"
printf 'its backup\n' > "$ELSE/missions/data.bin.old"
printf 'a file that belongs somewhere else\n' > "$ELSE/missions/new.bin"
rm -rf "$H3/missions"
ln -s "$ELSE/missions" "$H3/missions"
printf 'opencnc3d update journal 1\nA 1 missions/data.bin\nN missions/new.bin\n' > "$H3/opencnc3d-update.journal"
snap "$H3" opencnc3d-update.journal > "$WORK/h3.before"
snap "$ELSE" > "$WORK/else.before"
h_run "$WORK/h3.log" "$ROOT" "$LAUNCHER" --dir "$H3" --check
snap "$H3" opencnc3d-update.journal > "$WORK/h3.after"
snap "$ELSE" > "$WORK/else.after"
[ ! -e "$H3/opencnc3d-update.journal" ] \
    && pass "the install was recognised, and the planted journal was read and finished" \
    || fail "the planted journal was not finished, so the recovery never ran on it"
same "$WORK/else.before" "$WORK/else.after" \
    "nothing behind the link changed: the journal's names were not followed through it"
same "$WORK/h3.before" "$WORK/h3.after" "and the install, the link included, is as it was"

# 25. An update over an install where opencnc3d is a link to a copy of the new opencnc3d that
#     is not executable (an extractor that followed it would change that file's mode),
#     and ro, where the hard zip writes ro/p, is a link to another folder.
say "round H, an update over an install where a shipped file and a folder are links"
cp "$WORK/hard-bins.zip" "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip"
tools/launcher/make-manifest.sh v0.6.3 "$SITE/OpenCNC3D-v0.6.3-manifest.txt" \
    "$SITE"/*.zip > /dev/null
restart_site
H3B="$HOST/linked-update"
ELSE2="$HOST/elsewhere2"
mkinstall "$H3B"
mkdir -p "$ELSE2/ro"
cp "$HARD/opencnc3d" "$ELSE2/engine"
chmod 0644 "$ELSE2/engine"
printf 'a file that belongs somewhere else\n' > "$ELSE2/ro/p"
rm -f "$H3B/opencnc3d"
ln -s "$ELSE2/engine" "$H3B/opencnc3d"
ln -s "$ELSE2/ro" "$H3B/ro"
snap "$H3B" opencnc3d-update.zip > "$WORK/h3b.before"
snap "$ELSE2" > "$WORK/else2.before"
h_run "$WORK/h3b.log" "$ROOT" "$LAUNCHER" --dir "$H3B" --update
snap "$H3B" opencnc3d-update.zip > "$WORK/h3b.after"
snap "$ELSE2" > "$WORK/else2.after"
grep -q 'GET /files/OpenCNC3D-macos-v0.6.3-bins.zip' "$WORK/site.log" \
    && pass "the hard zip, which writes opencnc3d and ro/p, was the one taken" \
    || fail "the hard zip was not taken, so this leg proves nothing"
grep -q "result:    FAILED" "$WORK/h3b.log" && grep -q "ro/p" "$WORK/h3b.log" \
    && pass "an update whose way to ro/p passes a link fails, naming it" \
    || fail "an update wrote through a linked folder, or its failure does not say where"
grep -q "ro in the game folder is a link or a file, not a real folder. Put a real folder in its place" \
    "$WORK/h3b.log" \
    && pass "and the refusal names the linked folder, ro, and says to put a real folder in its place" \
    || fail "the refusal does not say which folder is a link, or what to do about it"
same "$WORK/else2.before" "$WORK/else2.after" \
    "nothing behind either link changed: no file written through ro, no mode changed through opencnc3d"
same "$WORK/h3b.before" "$WORK/h3b.after" \
    "and the undo put the install back as it was, the link on opencnc3d included"

# 26. --dir names a link to a folder that is not an install.
say "round H, --dir names a link to a folder that is not an install"
H3C="$HOST/real-folder"
mkdir -p "$H3C"
decoys "$H3C"
cp playable/dosmenu.pack "$H3C/"
ln -s "$H3C" "$HOST/OpenCNC3D"
snap "$H3C" > "$WORK/h3c.before"
h_run "$WORK/h3c.log" "$ROOT" "$LAUNCHER" --dir "$HOST/OpenCNC3D" --check
snap "$H3C" > "$WORK/h3c.after"
same "$WORK/h3c.before" "$WORK/h3c.after" \
    "the folder behind a --dir that is a link, with no install record, is left as it was"

# ---------------------------------------------------------------- round I
# 27-30. THE NAMES AN UPDATE KEEPS FOR ITS COPIES ARE NAMES A PLAYER CAN USE TOO. A
# fresh install per leg, with a player's own files on those names beside files the
# update carries, each compared before and after like round H's. Then installs whose
# record an older launcher can leave empty or cut short.
cp "$WORK/good-bins.zip" "$SITE/OpenCNC3D-macos-v0.6.3-bins.zip"
tools/launcher/make-manifest.sh v0.6.3 "$SITE/OpenCNC3D-v0.6.3-manifest.txt" \
    "$SITE"/*.zip > /dev/null
restart_site
PLAYER="opencnc3d.old opencnc3d.old2 TiberianDawn.dylib.old9 libSDL2-2.0.0.dylib.old
        libSDL2-2.0.0.dylib.opencnc3d-new BUILD-ID.txt.old opencnc3d-install.txt.old3 opencnc3d-launcher.old"
mkplayer() {   # mkplayer <install>: an install, round A's SDL, and a player's own copies
    mkinstall "$1"
    printf 'the same SDL in both builds\n' > "$1/libSDL2-2.0.0.dylib"
    for _f in $PLAYER; do
        printf 'a player file named %s\n' "$_f" > "$1/$_f"
    done
}
psnap() {   # psnap <install> <name>...: only the files named, compared as snap compares
    _root=$1
    shift
    ( cd "$_root" && for _f in "$@"; do
          if [ -e "$_f" ]; then
              stat -f '%N|%HT|%Lp|%z|%Fm|%i' "$_f"
              shasum < "$_f" | cut -d' ' -f1
          else
              echo "$_f is gone"
          fi
      done )
}
copies() {   # copies <install>: every file on a name an update keeps for itself, sorted
    ( cd "$1" && find . \( -name '*.old*' -o -name '*.opencnc3d-new' -o -name 'opencnc3d-update.*' \) -print \
        | sed 's|^\./||' | LC_ALL=C sort | tr '\n' ' ' )
}
sorted() { printf '%s\n' "$@" | LC_ALL=C sort | tr '\n' ' '; }

# 27. Beside a file the update finds identical, files it replaces, and the record it
#     rewrites, and a previous launcher's copy the zip does not carry.
say "round I, an update beside a player's own files on the names it keeps for its copies"
I1="$HOST/player-ok"
mkplayer "$I1"
psnap "$I1" $PLAYER > "$WORK/i1.before"
h_run "$WORK/i1.log" "$ROOT" "$LAUNCHER" --dir "$I1" --update
psnap "$I1" $PLAYER > "$WORK/i1.after"
grep -q "result:    installed v0.6.3" "$WORK/i1.log" && grep -q "NEW BINARY" "$I1/opencnc3d" \
    && grep -q "NEW ENGINE" "$I1/TiberianDawn.dylib" \
    && pass "the update installed beside them" \
    || fail "the update beside a player's own copies did not install, so this leg proves less"
same "$WORK/i1.before" "$WORK/i1.after" \
    "a player's .old, .old2, .old9 and .opencnc3d-new files beside identical, replaced and rewritten files are as they were"
[ "$(copies "$I1")" = "$(sorted $PLAYER)" ] \
    && pass "and the update's own copies, on the names it took around them, are gone" \
    || fail "the files on copy names are not exactly the player's: $(copies "$I1")"

# 27. A player's file on the name the engine's new copy is written to: the update fails
#     there, after opencnc3d was replaced.
say "round I, an update that fails on a player's file in its way"
I2="$HOST/player-fail"
mkplayer "$I2"
printf 'a player file named TiberianDawn.dylib.opencnc3d-new\n' > "$I2/TiberianDawn.dylib.opencnc3d-new"
psnap "$I2" $PLAYER TiberianDawn.dylib.opencnc3d-new > "$WORK/i2.before"
h_run "$WORK/i2.log" "$ROOT" "$LAUNCHER" --dir "$I2" --update
psnap "$I2" $PLAYER TiberianDawn.dylib.opencnc3d-new > "$WORK/i2.after"
grep -q "result:    FAILED" "$WORK/i2.log" \
    && grep -q "TiberianDawn.dylib.opencnc3d-new is in the way" "$WORK/i2.log" \
    && pass "an update whose new copy's name is taken fails, naming the file in its way" \
    || fail "an update went ahead over a file on the name of its new copy"
grep -q "OLD BINARY" "$I2/opencnc3d" && grep -q "OLD ENGINE" "$I2/TiberianDawn.dylib" \
    && grep -q "version 0.6.1" "$I2/opencnc3d-install.txt" \
    && pass "and opencnc3d, replaced before it, was put back" \
    || fail "the failed update was not undone"
same "$WORK/i2.before" "$WORK/i2.after" \
    "a player's files beside a file replaced and put back, and beside the file that failed, are as they were"
[ "$(copies "$I2")" = "$(sorted $PLAYER TiberianDawn.dylib.opencnc3d-new)" ] \
    && pass "and nothing of the update's own is left on a copy name" \
    || fail "the files on copy names are not exactly the player's: $(copies "$I2")"

# 28. A done journal, as a finished update or the Windows installer leaves one, whose
#     lines record some copies, beside a player's files on other copy names.
say "round I, a start over opencnc3d-update.done beside a player's own files"
I3="$HOST/player-done"
mkinstall "$I3"
for f in opencnc3d.old opencnc3d.old3 opencnc3d.opencnc3d-new missions/data.bin.opencnc3d-new TiberianDawn.dylib.old2; do
    printf 'a copy the update made: %s\n' "$f" > "$I3/$f"
done
for f in opencnc3d.old2 missions/data.bin.old TiberianDawn.dylib.old; do
    printf 'a player file named %s\n' "$f" > "$I3/$f"
done
printf 'opencnc3d update journal 1\nA 1 opencnc3d\nS 3 opencnc3d\nN missions/data.bin\nA 2 TiberianDawn.dylib\nC\n' \
    > "$I3/opencnc3d-update.done"
psnap "$I3" opencnc3d.old2 missions/data.bin.old TiberianDawn.dylib.old > "$WORK/i3.before"
h_run "$WORK/i3.log" "$ROOT" "$LAUNCHER" --dir "$I3" --check
psnap "$I3" opencnc3d.old2 missions/data.bin.old TiberianDawn.dylib.old > "$WORK/i3.after"
[ "$(copies "$I3")" = "$(sorted TiberianDawn.dylib.old opencnc3d.old2 missions/data.bin.old)" ] \
    && pass "the start removed exactly the copies the done journal's lines record, and the journal" \
    || fail "the start did not remove exactly what opencnc3d-update.done records: $(copies "$I3")"
same "$WORK/i3.before" "$WORK/i3.after" \
    "a player's .old files beside the same files, on names no line records, are as they were"

# 29. An unfinished update whose new cnc_eyes is still held (a non-empty folder, as in
#     round A), with a player's cnc_eyes.old2 on the first spare name.
say "round I, a start whose undo leaves a spare copy something still holds"
I4="$HOST/player-spare"
mkinstall "$I4"
printf 'OLD EDITOR\n' > "$I4/cnc_eyes.old"
mkdir "$I4/cnc_eyes"
printf 'held\n' > "$I4/cnc_eyes/held"
printf 'a player file named cnc_eyes.old2\n' > "$I4/cnc_eyes.old2"
printf 'opencnc3d update journal 1\nA 1 cnc_eyes\n' > "$I4/opencnc3d-update.journal"
psnap "$I4" cnc_eyes.old2 > "$WORK/i4.before"
h_run "$WORK/i4.log" "$ROOT" "$LAUNCHER" --dir "$I4" --check
grep -qx "OLD EDITOR" "$I4/cnc_eyes" 2>/dev/null && [ -d "$I4/cnc_eyes.old3" ] \
    && [ ! -e "$I4/opencnc3d-update.journal" ] \
    && pass "the undo put the old file back and moved the held copy to the first free spare name" \
    || fail "the undo did not put the old file back beside a held copy: $(copies "$I4")"
grep -qx "S 3 cnc_eyes" "$I4/opencnc3d-update.done" 2>/dev/null \
    && pass "and the spare was journalled, so its name is kept in opencnc3d-update.done" \
    || fail "the spare's name was not kept, so no start can remove it by name"
rm -f "$I4/cnc_eyes.old3/held"
h_run "$WORK/i4b.log" "$ROOT" "$LAUNCHER" --dir "$I4" --check
psnap "$I4" cnc_eyes.old2 > "$WORK/i4.after"
[ "$(copies "$I4")" = "cnc_eyes.old2 " ] \
    && pass "once nothing holds it, the next start removes the spare by that name, and the done journal" \
    || fail "the spare, or the done journal, was not removed: $(copies "$I4")"
same "$WORK/i4.before" "$WORK/i4.after" "and a player's cnc_eyes.old2, on no recorded name, is as it was"

# 30. Records a launcher up to v0.6.11 can leave when it is killed while it rewrites one
#     in place, and a record that is not a file at all. Served as v0.9.9, which is newer
#     than this launcher's own build number, the version an empty record reads as.
say "round I, installs whose record is empty, cut short, or not a file"
restart_site --tag v0.9.9
I5="$HOST/record-empty"
mkinstall "$I5"
: > "$I5/opencnc3d-install.txt"
h_run "$WORK/i5.log" "$ROOT" "$LAUNCHER" --dir "$I5" --update
grep -q "result:    installed v0.9.9" "$WORK/i5.log" && grep -q "version 0.9.9" "$I5/opencnc3d-install.txt" \
    && grep -q "NEW BINARY" "$I5/opencnc3d" \
    && pass "an install with an empty record is updated, and gets a whole record" \
    || fail "an install with an empty record was not updated"
I5B="$HOST/record-cut"
mkinstall "$I5B"
printf '# written by the OpenCNC3D launcher after an update\n' > "$I5B/opencnc3d-install.txt"
mv "$I5B/opencnc3d" "$I5B/opencnc3d.old"
printf 'HALF WAY\n' > "$I5B/opencnc3d"
printf 'opencnc3d update journal 1\nA 1 opencnc3d\n' > "$I5B/opencnc3d-update.journal"
h_run "$WORK/i5b.log" "$ROOT" "$LAUNCHER" --dir "$I5B" --check
grep -qx "OLD BINARY" "$I5B/opencnc3d" && [ -z "$(copies "$I5B")" ] \
    && pass "one whose record was cut to its first line has its unfinished update undone at start" \
    || fail "an install whose record was cut short was not recovered: $(copies "$I5B")"
I5C="$HOST/record-folder"
mkinstall "$I5C"
rm -f "$I5C/opencnc3d-install.txt"
mkdir "$I5C/opencnc3d-install.txt"
h_run "$WORK/i5c.log" "$ROOT" "$LAUNCHER" --dir "$I5C" --update
grep -q "result:    FAILED" "$WORK/i5c.log" \
    && grep -q "opencnc3d-install.txt in this folder is not a plain file" "$WORK/i5c.log" \
    && grep -q "OLD BINARY" "$I5C/opencnc3d" \
    && pass "and one whose record is a folder is refused, saying so rather than that it is missing" \
    || fail "a record that is not a plain file was not refused with a message that says so"

# ---------------------------------------------------------------- round J
# 32. THE CUTOFF. The live site shows a launcher of a release below 0.8.0 only the
#     releases below 0.8.0, by the prefix of the User-Agent it sends, and the fake site
#     is told the same rule. Each launcher's agent is read out of its own lnet.c: this
#     one's from the tree, the older one's from the newest release tag below 0.8.0. So
#     neither is written down a second time here, and a change to either is a change
#     this round sees.
say "round J, the cutoff for launchers below 0.8.0"
agent_of() { sed -n 's/^#define LN_AGENT "\(.*\)"$/\1/p'; }
NOW_AGENT=$(agent_of < launcher/lnet.c)
PRIOR_TAG=$(git tag --list 'v*' --sort=-version:refname \
    | awk '{ v = $0; sub(/^v/, "", v); n = split(v, p, ".");
             if (n >= 3 && p[1] + 0 == 0 && p[2] + 0 < 8) { print $0; exit } }')
PRIOR_AGENT=""
[ -n "$PRIOR_TAG" ] && PRIOR_AGENT=$(git show "$PRIOR_TAG:launcher/lnet.c" 2>/dev/null | agent_of)
# The prefix the site keys on is the agent's product name, up to and including the slash.
PRIOR_PREFIX="${PRIOR_AGENT%%/*}/"
if [ -z "$NOW_AGENT" ] || [ -z "$PRIOR_AGENT" ]; then
    fail "the agents could not be read (this tree: '$NOW_AGENT', ${PRIOR_TAG:-no release tag below 0.8.0}: '$PRIOR_AGENT'); fetch the tags and run it again"
else
    echo "   this launcher: $NOW_AGENT    $PRIOR_TAG's: $PRIOR_AGENT"
    case "$NOW_AGENT" in
        "$PRIOR_PREFIX"*) fail "this launcher's agent starts with $PRIOR_PREFIX, so the site would cut it off too" ;;
        *) pass "this launcher's agent does not start with $PRIOR_PREFIX, $PRIOR_TAG's" ;;
    esac
    restart_site --tag v0.8.0 --prior "$PRIOR_TAG" --cutoff 0.8.0 --cutoff-agent "$PRIOR_PREFIX"
    builds_seen() {   # builds_seen <agent>: the newest tag, then every tag listed
        curl -s -A "$1" "http://127.0.0.1:$PORT/api/builds" | python3 -I -c '
import json, sys
d = json.load(sys.stdin)
print((d.get("latest") or {}).get("tag", "none"), " ".join(r["tag"] for r in d.get("releases", [])))'
    }
    SEEN=$(builds_seen "$PRIOR_AGENT")
    [ "$SEEN" = "$PRIOR_TAG $PRIOR_TAG" ] \
        && pass "a launcher of $PRIOR_TAG is shown $PRIOR_TAG as the newest, and nothing at or above 0.8.0" \
        || fail "a launcher of $PRIOR_TAG was shown: $SEEN"
    SEEN=$(builds_seen "$NOW_AGENT")
    [ "$SEEN" = "v0.8.0 v0.8.0 $PRIOR_TAG" ] \
        && pass "this launcher's agent is shown v0.8.0 and everything below it" \
        || fail "this launcher's agent was shown: $SEEN"
    J1="$HOST/cutoff"
    mkinstall "$J1"
    printf 'version %s\ndata_id %s\n' "${PRIOR_TAG#v}" "$MACDATA" > "$J1/opencnc3d-install.txt"
    h_run "$WORK/j1.log" "$ROOT" "$LAUNCHER" --dir "$J1" --check
    grep -q "latest:    v0.8.0" "$WORK/j1.log" && grep -q "result:    v0.8.0 is available" "$WORK/j1.log" \
        && pass "and this launcher, over an install at $PRIOR_TAG, is offered v0.8.0" \
        || fail "this launcher was not offered v0.8.0 over an install at $PRIOR_TAG"
    grep -F -q "GET /api/builds HTTP/1.1\" 200 - ua=$NOW_AGENT" "$WORK/site.log" \
        && pass "the launcher sent the agent its lnet.c names" \
        || fail "the launcher did not send '$NOW_AGENT': $(grep 'GET /api/builds' "$WORK/site.log" | tail -1)"
fi

# ---------------------------------------------------------------- round K
# 33-40. THE MOVIES AND MUSIC, AND THE FIRST INSTALL. Three synthetic disc images, made
# here out of random bytes with small fake movies and music files at chosen offsets,
# stand in for the freeware discs; the fake site serves them as every mirror a test
# manifest names, each address going wrong in its own way (fake-site.py's docstring).
# Every run below is --media-install or --install-game: neither opens a window.
#
# THE MODES MUST EXIST BEFORE THEY ARE ASKED FOR. A launcher whose main() does not hand
# them to lsetup_cli_main reads "--media-install" as nothing and opens its window, so
# the round checks the source first and stops rather than raise one.
say "round K, movies and music"
if ! grep -q 'lsetup_cli_main(' launcher/launcher.c; then
    fail "launcher/launcher.c does not call lsetup_cli_main, so --media-install and --install-game would open the launcher's window; round K was not run"
else

KM="$WORK/k-media"
mkdir -p "$KM"
cat > "$WORK/k-mkdisc.py" <<'PY'
import hashlib, os, random, struct, sys, zipfile
out, files_txt = sys.argv[1], sys.argv[2]
rnd = random.Random(1995)
def noise(n):
    return bytearray(rnd.getrandbits(8) for _ in range(n))
# Big enough that a mirror streaming a whole disc cannot hide inside the socket buffers.
discs = {"GDI": noise(6000000), "NOD": noise(2000000), "COVERT": noise(1000000)}
spec = [("movies", "dosdata/movies/GDI1.VQA", "GDI", 300000),
        ("movies", "dosdata/movies/LOGO.VQA", "GDI", 120000),
        ("movies", "dosdata/movies/INTRO2.VQA", "GDI", 200000),
        ("movies", "dosdata/movies/NOD1.VQA", "NOD", 250000),
        ("movies", "dosdata/movies/NOD1PRE.VQA", "NOD", 90000),
        ("music", "dosdata/SCORES.MIX", "GDI", 400000),
        ("music", "dosdata/transit/TRANSIT.MIX", "GDI", 60000),
        ("music", "dosdata/music/FIST226M.AUD", "COVERT", 150000),
        ("music", "dosdata/music/RECN226M.AUD", "COVERT", 110000)]
at = {"GDI": 501234, "NOD": 77777, "COVERT": 33333}
lines = []
for comp, rel, disc, n in spec:
    body = noise(n)
    body[:4] = b"FORM" if rel.endswith(".VQA") else b"MIX!"
    off = at[disc]
    at[disc] += n + 4321
    discs[disc][off:off + n] = body
    lines.append("file %s %s %d %s %s %d" % (comp, rel, n, hashlib.sha256(body).hexdigest(), disc, off))
for d, b in discs.items():
    with open(os.path.join(out, d + ".ISO"), "wb") as f:
        f.write(b)
# The Nod disc again, STORED inside a zip, the way one fallback host serves the discs.
zpath = os.path.join(out, "NOD.ZIP")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_STORED) as z:
    z.writestr("README.TXT", "a file in front of the disc image\n")
    z.write(os.path.join(out, "NOD.ISO"), "NOD95.ISO")
with zipfile.ZipFile(zpath) as z:
    info = z.getinfo("NOD95.ISO")
with open(zpath, "rb") as f:
    f.seek(info.header_offset)
    head = f.read(30)
    base = info.header_offset + 30 + struct.unpack("<H", head[26:28])[0] + struct.unpack("<H", head[28:30])[0]
with open(files_txt, "w") as f:
    f.write("\n".join(lines) + "\n")
print(base)
PY
NODZIP_BASE=$(python3 -I "$WORK/k-mkdisc.py" "$KM" "$WORK/k-files.txt")
KTOTAL=$(awk '{ t += $4 } END { print t }' "$WORK/k-files.txt")
printf 'legacy movies movies/LOGO.VQA\nlegacy movies movies/INTRO2.VQA\nlegacy music dosdata/music/AOI.AUD\n' \
    > "$WORK/k-legacy.txt"
KB="http://127.0.0.1:$PORT"
echo "   discs in $KM; the Nod disc starts at byte $NODZIP_BASE of NOD.ZIP; $KTOTAL bytes of media"

kman() {   # kman <out> <egrep over the file lines> <mirror lines>...
    _out=$1
    _filter=$2
    shift 2
    { echo "OCMEDIA 1"
      echo "# a test manifest"
      for _m in "$@"; do echo "mirror $_m"; done
      grep -E "$_filter" "$WORK/k-files.txt" || true
      cat "$WORK/k-legacy.txt"; } > "$_out"
}
kgood() {   # kgood <install> <egrep over the file lines>: every file there, byte for byte
    _bad=""
    while read -r _k _comp _rel _size _sha _disc _off; do
        _got=$(shasum -a 256 "$1/$_rel" 2>/dev/null | cut -d' ' -f1)
        [ "$_got" = "$_sha" ] || _bad="$_bad $_rel"
    done <<EOF
$(grep -E "$2" "$WORK/k-files.txt")
EOF
    [ -z "$_bad" ]
}
krecord() {   # krecord <install>: the record's four lines on one line
    grep -E '^(movies|music|movies_state|music_state) ' "$1/opencnc3d-media.txt" 2>/dev/null | tr '\n' ' '
}
kfield() {   # kfield <progress file> <key>
    sed -n "s/^$2=//p" "$1" 2>/dev/null | head -1
}
krun() {   # krun <log> <seconds> <launcher arguments>...: bounded, sets KRC
    _log=$1
    _secs=$2
    shift 2
    KRC=0
    perl -e 'alarm shift; exec { $ARGV[0] } @ARGV' "$_secs" "$LAUNCHER" "$@" > "$_log" 2>&1 || KRC=$?
    sed 's/^/   /' "$_log"
}
ksampled() {   # ksampled <log> <progress file> <samples> <launcher arguments>...: sets KRC
    _log=$1
    _prog=$2
    _samples=$3
    shift 3
    : > "$_samples"
    KRC=0
    perl -e 'alarm 180; exec { $ARGV[0] } @ARGV' "$LAUNCHER" "$@" > "$_log" 2>&1 &
    _pid=$!
    while kill -0 "$_pid" 2>/dev/null; do
        [ -f "$_prog" ] && { cat "$_prog"; printf -- '---\n'; } >> "$_samples"
        nap
    done
    wait "$_pid" || KRC=$?
    [ -f "$_prog" ] && { cat "$_prog"; printf -- '---\n'; } >> "$_samples"
    sed 's/^/   /' "$_log"
}
cat > "$WORK/k-contract.py" <<'PY'
import re, sys
samples, want_state, want_total = sys.argv[1], sys.argv[2], sys.argv[3]
keys = ["state", "pct", "text", "item", "beat", "reason"]
states = {"fetching", "checking", "removing", "done", "failed", "cancelled"}
def size(n):
    n = float(n); mib = 1024.0 * 1024.0
    return "%.1f GB" % (n / mib / 1024.0) if n >= 1023.95 * mib else "%.1f MB" % (n / mib)
bad, last, prev_pct, prev_beat, beats, n = [], None, -1, -1, set(), 0
for block in open(samples).read().split("---\n"):
    lines = block.rstrip("\n").split("\n")
    if lines == [""]:
        continue
    n += 1
    if lines[-1] != "end":
        bad.append("a sample does not end with 'end': %r" % lines[-1]); continue
    if [l.split("=", 1)[0] for l in lines[:-1]] != keys:
        bad.append("fields out of order: %r" % lines); continue
    d = dict(l.split("=", 1) for l in lines[:-1])
    if d["state"] not in states:
        bad.append("unknown state %r" % d["state"])
    pct, beat = int(d["pct"]), int(d["beat"])
    if not 0 <= pct <= 100 or pct < prev_pct:
        bad.append("pct went from %d to %d" % (prev_pct, pct))
    if beat < prev_beat:
        bad.append("beat went from %d to %d" % (prev_beat, beat))
    if d["text"] and not re.match(r"^(\d+\.\d (MB|GB) of \d+\.\d (MB|GB) \(\d+%\)|\d+%)$", d["text"]):
        bad.append("text %r is not 'X of Y (P%%)'" % d["text"])
    m = re.match(r"^(\d+\.\d) (MB|GB) of (\d+\.\d) (MB|GB) \((\d+)%\)$", d["text"])
    if m:
        # The two sizes say the same percentage as the number: each is rounded to 0.1 of
        # its unit, so they may be that far apart, and no further.
        unit = {"MB": 1.0, "GB": 1024.0}
        x, y = float(m.group(1)) * unit[m.group(2)], float(m.group(3)) * unit[m.group(4)]
        p = int(m.group(5))
        slack = (100.0 * 0.05 * (unit[m.group(2)] + unit[m.group(4)]) / y + 0.001) if y else 0
        if y and p != pct:
            bad.append("text %r says %d%%, pct says %d" % (d["text"], p, pct))
        elif y and not (p - slack <= x * 100.0 / y < p + 1 + slack):
            bad.append("text %r: %.2f%% of the way is not %d%%" % (d["text"], x * 100.0 / y, p))
    prev_pct, prev_beat, last = pct, beat, d
    beats.add(beat)
if not last:
    bad.append("no sample was read")
else:
    if last["state"] != want_state:
        bad.append("the last state is %r, not %r" % (last["state"], want_state))
    if want_state == "done" and last["pct"] != "100":
        bad.append("done at %s%%" % last["pct"])
    if want_total != "-" and not last["text"].endswith(" of %s (%s%%)" % (size(want_total), last["pct"])) :
        bad.append("the last text %r does not say %s" % (last["text"], size(want_total)))
print("%d samples, %d beats, last %s" % (n, len(beats), last and (last["state"], last["pct"], last["text"])))
for b in bad[:8]:
    print("BAD " + b)
sys.exit(1 if bad else 0)
PY
kcontract() {   # kcontract <samples> <final state> <total bytes or -> <what it proves>
    if python3 -I "$WORK/k-contract.py" "$1" "$2" "$3" > "$WORK/k-contract.out" 2>&1; then
        pass "$4 ($(head -1 "$WORK/k-contract.out"))"
    else
        fail "$4: $(tr '\n' ' ' < "$WORK/k-contract.out")"
    fi
}

# The movies and music mirrors are this same fake site. It is restarted with them, on
# the same port, serving the round's own builds when there are some.
KSITE="$WORK/k-site"
mkdir -p "$KSITE"
SITE_DIR="$KSITE" restart_site --tag v0.8.0 --media "$KM"

# 33. The default install, every mirror of the first choice going wrong.
say "round K, --media-install movies,music over mirrors that go wrong"
K1="$HOST/k-default"
mkinstall "$K1"
kman "$WORK/k-m1.txt" . \
    "GDI $KB/media-fault/ignore-range/GDI.ISO 0" \
    "GDI $KB/media-302/GDI.ISO 0" \
    "NOD $KB/media-fault/html/NOD.ISO 0" \
    "NOD $KB/media/NOD.ZIP $NODZIP_BASE" \
    "COVERT $KB/media-fault/500x2/COVERT.ISO 0"
ksampled "$WORK/k1.log" "$WORK/k1.progress" "$WORK/k1.samples" \
    --media-install movies,music --dir "$K1" --media-manifest "$WORK/k-m1.txt" \
    --progress-file "$WORK/k1.progress"
[ "$KRC" = 0 ] && kgood "$K1" . \
    && pass "every movie and every piece of music arrived, each matching its SHA-256 (exit 0)" \
    || fail "the default install did not fetch every file intact (exit $KRC)"
[ "$(krecord "$K1")" = "movies yes music yes movies_state complete music_state complete " ] \
    && pass "and opencnc3d-media.txt records both ticked and both complete" \
    || fail "the record reads: $(krecord "$K1")"
[ -z "$(find "$K1" -name '*.ocpart')" ] \
    && pass "and no part file is left" || fail "part files are left: $(find "$K1" -name '*.ocpart')"
IGN=$(grep -c 'media: /media-fault/ignore-range/GDI.ISO .*status=200' "$WORK/site.log" || true)
IGNSENT=$(sed -n 's|.*media: /media-fault/ignore-range/GDI.ISO .*sent=\([0-9]*\) of.*|\1|p' "$WORK/site.log" | head -1)
[ "$IGN" = 1 ] && [ -n "$IGNSENT" ] && [ "$IGNSENT" -lt 3000000 ] \
    && pass "the mirror that ignores Range was asked once, and refused after $IGNSENT of its 6000000 bytes" \
    || fail "the mirror that ignores Range was asked $IGN times and sent ${IGNSENT:-?} bytes"
grep -q '"GET /media-302/GDI.ISO HTTP/1\.[01]" 302' "$WORK/site.log" \
    && grep -q 'media: /media/GDI.ISO range=bytes=[0-9]*-[0-9]* status=206' "$WORK/site.log" \
    && pass "the 302 was followed, and the Range went with it" \
    || fail "the redirect to the Gold disc was not followed with its Range"
[ "$(grep -c 'media: /media-fault/html/NOD.ISO .*status=206' "$WORK/site.log")" = 2 ] \
    && [ "$(grep -c 'media: /media/NOD.ZIP .*status=206' "$WORK/site.log")" = 2 ] \
    && pass "a mirror whose bytes were a web page failed the hash for each file, and the zip's copy was used, from its base offset" \
    || fail "the web-page mirror or the zipped disc was not used as it should have been"
[ "$(grep -c '"GET /media-fault/500x2/COVERT.ISO HTTP/1\.[01]" 500' "$WORK/site.log")" = 2 ] \
    && grep -q 'media: /media-fault/500x2/COVERT.ISO .*status=206' "$WORK/site.log" \
    && pass "two answers of 500 were retried, and the third attempt got the file" \
    || fail "the 500s were not retried"
kcontract "$WORK/k1.samples" done "$KTOTAL" "the progress file kept its contract all the way to done"

# 34. One fault at a time, on one file.
say "round K, a transfer cut part way, one that stalls, and a file nobody has"
CUTREL=dosdata/movies/GDI1.VQA
CUTOFF=$(awk -v r="$CUTREL" '$3 == r { print $7 }' "$WORK/k-files.txt")
K2="$HOST/k-cut"
mkinstall "$K2"
kman "$WORK/k-m2.txt" "GDI1\.VQA" "GDI $KB/media-fault/cut=100000/GDI.ISO 0"
krun "$WORK/k2.log" 60 --media-install movies --dir "$K2" --media-manifest "$WORK/k-m2.txt"
[ "$KRC" = 0 ] && kgood "$K2" "GDI1\.VQA" \
    && grep -q "media: /media-fault/cut=100000/GDI.ISO range=bytes=$((CUTOFF + 100000))-" "$WORK/site.log" \
    && pass "a transfer cut after 100000 bytes was resumed from byte $((CUTOFF + 100000)) and finished intact" \
    || fail "the cut transfer was not resumed where it stopped (exit $KRC)"

K3="$HOST/k-stall"
mkinstall "$K3"
kman "$WORK/k-m3.txt" "LOGO\.VQA" "GDI $KB/media-fault/stall=40/GDI.ISO 0" "GDI $KB/media/GDI.ISO 0"
T0=$(date +%s)
krun "$WORK/k3.log" 90 --media-install movies --dir "$K3" --media-manifest "$WORK/k-m3.txt" \
    --stall-seconds 2
T1=$(date +%s)
[ "$KRC" = 0 ] && kgood "$K3" "LOGO\.VQA" && [ $((T1 - T0)) -lt 35 ] \
    && [ "$(grep -c '"GET /media-fault/stall=40/GDI.ISO' "$WORK/site.log")" = 3 ] \
    && pass "a mirror that stalls was given up on three times within the stall bound and the next one used ($((T1 - T0)) s)" \
    || fail "the stalled mirror was not abandoned in time (exit $KRC, $((T1 - T0)) s)"

K4="$HOST/k-nobody"
mkinstall "$K4"
kman "$WORK/k-m4.txt" "LOGO\.VQA" "GDI $KB/media-fault/404/GDI.ISO 0"
krun "$WORK/k4.log" 60 --media-install movies --dir "$K4" --media-manifest "$WORK/k-m4.txt" \
    --progress-file "$WORK/k4.progress"
[ "$KRC" = 1 ] && [ "$(krecord "$K4")" = "movies yes music no movies_state pending music_state none " ] \
    && kfield "$WORK/k4.progress" reason | grep -q "404" \
    && [ "$(kfield "$WORK/k4.progress" state)" = failed ] \
    && pass "a file no mirror has fails the job (exit 1), says why, and the record says pending" \
    || fail "a file nobody serves gave exit $KRC, record '$(krecord "$K4")', reason '$(kfield "$WORK/k4.progress" reason)'"

# 35. Cancel, part way through a slow transfer, and the run after it.
say "round K, cancelled part way, then resumed"
K5="$HOST/k-cancel"
mkinstall "$K5"
kman "$WORK/k-m5.txt" "GDI1\.VQA" "GDI $KB/media-fault/slow=60000/GDI.ISO 0"
rm -f "$WORK/k5.cancel"
: > "$WORK/k5.samples"
KRC=0
perl -e 'alarm 60; exec { $ARGV[0] } @ARGV' "$LAUNCHER" --media-install movies --dir "$K5" \
    --media-manifest "$WORK/k-m5.txt" --progress-file "$WORK/k5.progress" \
    --cancel-file "$WORK/k5.cancel" > "$WORK/k5.log" 2>&1 &
KPID=$!
i=0
while [ $i -lt 400 ]; do
    [ "$(stat -f %z "$K5/$CUTREL.ocpart" 2>/dev/null || echo 0)" -gt 50000 ] && break
    [ -f "$WORK/k5.progress" ] && { cat "$WORK/k5.progress"; printf -- '---\n'; } >> "$WORK/k5.samples"
    nap
    i=$((i + 1))
done
: > "$WORK/k5.cancel"
wait "$KPID" || KRC=$?
cat "$WORK/k5.progress" >> "$WORK/k5.samples"; printf -- '---\n' >> "$WORK/k5.samples"
sed 's/^/   /' "$WORK/k5.log"
KPART=$(stat -f %z "$K5/$CUTREL.ocpart" 2>/dev/null || echo 0)
[ "$KRC" = 2 ] && [ "$KPART" -gt 0 ] && [ "$KPART" -lt 300000 ] && [ ! -e "$K5/$CUTREL" ] \
    && [ "$(krecord "$K5")" = "movies yes music no movies_state pending music_state none " ] \
    && pass "a cancelled download stops (exit 2), keeps its $KPART bytes in the part file, and records pending" \
    || fail "the cancel gave exit $KRC, a part of $KPART bytes, record '$(krecord "$K5")'"
kcontract "$WORK/k5.samples" cancelled - "and the progress file ends cancelled, in contract"
kman "$WORK/k-m5b.txt" "GDI1\.VQA" "GDI $KB/media/GDI.ISO 0"
krun "$WORK/k5b.log" 60 --media-install movies --dir "$K5" --media-manifest "$WORK/k-m5b.txt"
[ "$KRC" = 0 ] && kgood "$K5" "GDI1\.VQA" && [ ! -e "$K5/$CUTREL.ocpart" ] \
    && grep -q "media: /media/GDI.ISO range=bytes=$((CUTOFF + KPART))-" "$WORK/site.log" \
    && [ "$(krecord "$K5")" = "movies yes music no movies_state complete music_state none " ] \
    && pass "the next run resumed from byte $((CUTOFF + KPART)) and finished it" \
    || fail "the run after the cancel did not resume (exit $KRC)"

# 36. Nothing to reach.
say "round K, offline"
DEADPORT=$(python3 -I -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
K6="$HOST/k-offline"
mkinstall "$K6"
kman "$WORK/k-m6.txt" . "GDI http://127.0.0.1:$DEADPORT/GDI.ISO 0" \
    "NOD http://127.0.0.1:$DEADPORT/NOD.ISO 0" "COVERT http://127.0.0.1:$DEADPORT/COVERT.ISO 0"
T0=$(date +%s)
krun "$WORK/k6.log" 60 --media-install movies,music --dir "$K6" --media-manifest "$WORK/k-m6.txt" \
    --progress-file "$WORK/k6.progress"
T1=$(date +%s)
[ "$KRC" = 3 ] && [ $((T1 - T0)) -lt 35 ] && kfield "$WORK/k6.progress" reason | grep -q "could not reach the download servers" \
    && ! kfield "$WORK/k6.progress" reason | grep -q "127.0.0.1" \
    && pass "with no mirror reachable the job fails as offline (exit 3) in $((T1 - T0)) s, naming no host" \
    || fail "offline gave exit $KRC after $((T1 - T0)) s: $(kfield "$WORK/k6.progress" reason)"

# 37. Unticking.
say "round K, --media-install none"
K7="$HOST/k-none"
cp -R "$K1" "$K7"
mkdir -p "$K7/movies"
printf 'the old top-level copy\n' > "$K7/movies/LOGO.VQA"
printf 'a duplicate theme\n' > "$K7/dosdata/music/AOI.AUD"
printf 'half a download\n' > "$K7/dosdata/movies/NOD1.VQA.ocpart"
printf 'a movie the player made\n' > "$K7/dosdata/movies/MYOWN.VQA"
printf 'and a theme\n' > "$K7/dosdata/music/MYOWN.AUD"
KGONE="$(awk '{ print $3 }' "$WORK/k-files.txt") movies/LOGO.VQA movies/INTRO2.VQA dosdata/music/AOI.AUD dosdata/movies/NOD1.VQA.ocpart"
( cd "$K7" && find . -type f ! -name opencnc3d-media.txt | LC_ALL=C sort ) > "$WORK/k7.all"
for f in $KGONE; do printf './%s\n' "$f"; done | LC_ALL=C sort > "$WORK/k7.gone"
LC_ALL=C comm -23 "$WORK/k7.all" "$WORK/k7.gone" > "$WORK/k7.kept"
( cd "$K7" && while read -r f; do shasum "$f"; done < "$WORK/k7.kept" ) > "$WORK/k7.before"
krun "$WORK/k7.log" 60 --media-install none --dir "$K7" --media-manifest "$WORK/k-m1.txt"
LEFT=""
for f in $KGONE; do [ -e "$K7/$f" ] && LEFT="$LEFT $f"; done
( cd "$K7" && while read -r f; do shasum "$f" 2>&1; done < "$WORK/k7.kept" ) > "$WORK/k7.after"
[ "$KRC" = 0 ] && [ -z "$LEFT" ] \
    && pass "every manifest name, legacy name and part file is gone (exit 0)" \
    || fail "after --media-install none (exit $KRC) these are still there:$LEFT"
same "$WORK/k7.before" "$WORK/k7.after" \
    "and everything else is as it was, the player's own MYOWN.VQA and MYOWN.AUD included"
[ "$(krecord "$K7")" = "movies no music no movies_state none music_state none " ] \
    && pass "and the record says neither, and none of either" \
    || fail "the record reads: $(krecord "$K7")"

K8="$HOST/k-linked"
mkinstall "$K8"
mkdir -p "$HOST/k-linked-data"
cp -R "$K1/dosdata/" "$HOST/k-linked-data/"
ln -s "$HOST/k-linked-data" "$K8/dosdata"
snap "$HOST/k-linked-data" > "$WORK/k8.before"
krun "$WORK/k8.log" 60 --media-install none --dir "$K8" --media-manifest "$WORK/k-m1.txt" \
    --progress-file "$WORK/k8.progress"
KRC_NONE=$KRC
krun "$WORK/k8b.log" 60 --media-install movies,music --dir "$K8" --media-manifest "$WORK/k-m1.txt"
snap "$HOST/k-linked-data" > "$WORK/k8.after"
[ "$KRC_NONE" = 1 ] && [ "$KRC" = 1 ] && kfield "$WORK/k8.progress" reason | grep -q "dosdata in the game folder is a link" \
    && pass "a dosdata that is a link is refused both ways (exit 1), saying so" \
    || fail "a linked dosdata gave exit $KRC_NONE and $KRC: $(kfield "$WORK/k8.progress" reason)"
same "$WORK/k8.before" "$WORK/k8.after" "and nothing behind the link was touched"

K9="$HOST/k-notinstall"
mkdir -p "$K9"
cp playable/dosmenu.pack "$K9/"
snap "$K9" > "$WORK/k9.before"
krun "$WORK/k9.log" 60 --media-install movies,music --dir "$K9" --media-manifest "$WORK/k-m1.txt"
snap "$K9" > "$WORK/k9.after"
[ "$KRC" = 1 ] && grep -q "not an OpenCNC3D install" "$WORK/k9.log" \
    && pass "a folder that is not an install is refused (exit 1), saying so" \
    || fail "a folder that is not an install gave exit $KRC"
same "$WORK/k9.before" "$WORK/k9.after" "and gets no record and no files"

# 41. SETTINGS NEVER REMOVES WHAT THE PLAYER DID NOT UNTICK. A score only partly there
#     (the first 1000 bytes of SCORES.MIX, TRANSIT.MIX whole) is what a stopped download
#     or an older package leaves. Settings is pictured with --shot, and each picture
#     prints the ticks and what Apply would hand to lmedia, so the job a press would
#     start is then run for real with --media-install.
say "round K, Settings over a half-downloaded score, then Apply for the movies"
lchk() {   # lchk <shot log> <label>: that checkbox's checked= field
    sed -n "s/^LCHK|label=$2|checked=\([01]\)|.*/\1/p" "$1" | head -1
}
lapply() { sed -n 's/^LAPPLY|//p' "$1" | head -1; }
lstatus() { sed -n 's/^LSTATUS|text=//p' "$1" | head -1; }
kman "$WORK/k-m13.txt" . "GDI $KB/media/GDI.ISO 0" "NOD $KB/media/NOD.ISO 0" \
    "COVERT $KB/media/COVERT.ISO 0"
K13="$HOST/k-halfscore"
mkinstall "$K13"
mkdir -p "$K13/dosdata/transit"
cp "$K1/dosdata/transit/TRANSIT.MIX" "$K13/dosdata/transit/"
head -c 1000 "$K1/dosdata/SCORES.MIX" > "$K13/dosdata/SCORES.MIX"
h_run "$WORK/k13.log" "$ROOT" "$LAUNCHER" --dir "$K13" --media-manifest "$WORK/k-m13.txt" \
    --view settings --media-shot-tick movies --shot "$WORK/k13.png" --scale 1
[ "$(lchk "$WORK/k13.log" MUSIC)" = 1 ] && [ "$(lchk "$WORK/k13.log" MOVIES)" = 1 ] \
    && [ "$(lapply "$WORK/k13.log")" = "movies=1|music=1|asks=0|game=0|enabled=1" ] \
    && pass "Settings ticks the music while any of it is there, so Apply pressed for the movies sends movies,music and asks nothing" \
    || fail "Settings over a partial score: MOVIES $(lchk "$WORK/k13.log" MOVIES), MUSIC $(lchk "$WORK/k13.log" MUSIC), Apply would send '$(lapply "$WORK/k13.log")'"
h_run "$WORK/k13q.log" "$ROOT" "$LAUNCHER" --dir "$K13" --media-manifest "$WORK/k-m13.txt" \
    --view settings --media-shot-tick music --media-shot-confirm --shot "$WORK/k13q.png" --scale 1
[ "$(lapply "$WORK/k13q.log")" = "movies=0|music=0|asks=1|game=0|enabled=1" ] \
    && lstatus "$WORK/k13q.log" | grep -q "^Remove the music, " \
    && pass "unticking that partial music asks first, naming it: '$(lstatus "$WORK/k13q.log")'" \
    || fail "unticking the partial music: Apply '$(lapply "$WORK/k13q.log")', status '$(lstatus "$WORK/k13q.log")'"
KSPEC=$(lapply "$WORK/k13.log" | awk -F'|' '{ m = ($1 == "movies=1"); u = ($2 == "music=1");
    print (m && u) ? "movies,music" : m ? "movies" : u ? "music" : "none" }')
krun "$WORK/k13b.log" 60 --media-install "$KSPEC" --dir "$K13" --media-manifest "$WORK/k-m13.txt"
[ "$KRC" = 0 ] && kgood "$K13" . \
    && [ "$(krecord "$K13")" = "movies yes music yes movies_state complete music_state complete " ] \
    && pass "and the job Apply would start ($KSPEC) keeps the music: every music file is there and whole, the short score replaced rather than removed" \
    || fail "the job Apply would start ($KSPEC) gave exit $KRC, record '$(krecord "$K13")'"
K13P="$HOST/k-pending"
mkinstall "$K13P"
printf 'movies yes\nmusic no\nmovies_state pending\nmusic_state none\n' > "$K13P/opencnc3d-media.txt"
h_run "$WORK/k13p.log" "$ROOT" "$LAUNCHER" --dir "$K13P" --media-manifest "$WORK/k-m13.txt" \
    --shot "$WORK/k13p.png" --scale 1
h_run "$WORK/k13ps.log" "$ROOT" "$LAUNCHER" --dir "$K13P" --media-manifest "$WORK/k-m13.txt" \
    --view settings --shot "$WORK/k13ps.png" --scale 1
[ "$(lstatus "$WORK/k13p.log")" = "The movies did not finish downloading. The gear finishes it." ] \
    && [ "$(lapply "$WORK/k13ps.log")" = "movies=1|music=0|asks=0|game=0|enabled=1" ] \
    && pass "a record left pending is said on the module grid, and Settings ticks what it chose, ready to finish" \
    || fail "a pending record: grid '$(lstatus "$WORK/k13p.log")', Settings would send '$(lapply "$WORK/k13ps.log")'"

# 39. The first install into a folder holding nothing but the launcher.
say "round K, --install-game into a folder holding only the launcher"
mkrelease() {   # mkrelease <version> <site folder> [extra file for the bins zip]
    _v=$1
    _site=$2
    _pkg="$WORK/k-pkg-$_v"
    rm -rf "$_pkg"
    cp -R "$NEWMAC" "$_pkg"
    printf 'v%s  macos\n' "$_v" > "$_pkg/BUILD-ID.txt"
    printf 'NEW BINARY %s\n' "$_v" > "$_pkg/opencnc3d"
    chmod +x "$_pkg/opencnc3d"
    printf '# selftest\nversion %s\ndata_id %s\n' "$_v" "$(tools/launcher/data-id.sh "$_pkg")" \
        > "$_pkg/opencnc3d-install.txt"
    rm -rf "$WORK/k-wrap" && mkdir -p "$WORK/k-wrap" && cp -R "$_pkg" "$WORK/k-wrap/OpenCNC3D-macos-v$_v"
    ( cd "$WORK/k-wrap" && zip -qrX "$_site/OpenCNC3D-macos-v$_v.zip" "OpenCNC3D-macos-v$_v" )
    [ -n "$3" ] && printf 'movies yes\nmusic no\nmovies_state none\nmusic_state none\n' > "$_pkg/$3"
    ( cd "$_pkg" && zip -qX "$_site/OpenCNC3D-macos-v$_v-bins.zip" $BINS ${3:+"$3"} )
    # The Windows package too, because a release manifest names both or is refused.
    rm -rf "$WORK/k-wrap" && mkdir -p "$WORK/k-wrap" \
        && cp -R "$NEWWIN" "$WORK/k-wrap/OpenCNC3D-windows-v$_v"
    printf '# selftest\nversion %s\ndata_id %s\n' "$_v" \
        "$(tools/launcher/data-id.sh "$WORK/k-wrap/OpenCNC3D-windows-v$_v")" \
        > "$WORK/k-wrap/OpenCNC3D-windows-v$_v/opencnc3d-install.txt"
    ( cd "$WORK/k-wrap" && zip -qrX "$_site/OpenCNC3D-windows-v$_v.zip" "OpenCNC3D-windows-v$_v" )
    tools/launcher/make-manifest.sh "v$_v" "$_site/OpenCNC3D-v$_v-manifest.txt" \
        "$_site/OpenCNC3D-macos-v$_v.zip" "$_site/OpenCNC3D-macos-v$_v-bins.zip" \
        "$_site/OpenCNC3D-windows-v$_v.zip" > /dev/null
}
mkrelease 0.8.0 "$KSITE"
K10="$HOST/k-fresh"
mkdir -p "$K10"
printf 'the launcher, which is all the web installer puts here\n' > "$K10/OpenCNC3D.exe"

kman "$WORK/k-m10.txt" . "GDI $KB/media/GDI.ISO 0" "NOD $KB/media-302/NOD.ISO 0" \
    "COVERT $KB/media/COVERT.ISO 0"
ZIPSIZE=$(stat -f %z "$KSITE/OpenCNC3D-macos-v0.8.0.zip")
# The same folder with the menu pack in it as well, so that --update gets as far as
# refusing it rather than stopping at "could not find its game files", which is a
# message box: an ordinary update must still refuse a folder with no record.
#
# SERVED A RELEASE NEWER THAN ANY BUILD THIS TREE CAN STAMP, on a site of its own. A
# folder with no record reads as the launcher's own build number, and only an update
# on offer reaches the refusal: against a release at or below that number the check
# answers "up to date" and returns first. Served v0.8.0, as the install below is,
# this case proved the refusal only while the tree's VERSION was under 0.8.0, and
# failed from the first build of the 0.8 line on.
KSITEU="$WORK/k-site-update"
mkdir -p "$KSITEU"
mkrelease 99.0.0 "$KSITEU"
SITE_DIR="$KSITEU" restart_site --tag v99.0.0
K10U="$HOST/k-fresh-update"
mkdir -p "$K10U"
printf 'the launcher, which is all the web installer puts here\n' > "$K10U/OpenCNC3D.exe"
cp playable/dosmenu.pack "$K10U/"
snap "$K10U" > "$WORK/k10u.before"
h_run "$WORK/k10u.log" "$ROOT" "$LAUNCHER" --dir "$K10U" --update
snap "$K10U" > "$WORK/k10u.after"
grep -q "has no opencnc3d-install.txt" "$WORK/k10u.log" \
    && pass "an ordinary --update still refuses a folder that is not an install yet" \
    || fail "an ordinary --update did not refuse a folder with no install record: $(grep '^result:' "$WORK/k10u.log")"
same "$WORK/k10u.before" "$WORK/k10u.after" "and changes nothing in it"
SITE_DIR="$KSITE" restart_site --tag v0.8.0 --media "$KM" --short OpenCNC3D-macos-v0.8.0.zip
krun "$WORK/k10a.log" 90 --install-game --dir "$K10" --media movies,music \
    --media-manifest "$WORK/k-m10.txt" --progress-file "$WORK/k10a.progress"
KHALF=$(stat -f %z "$K10/opencnc3d-update.zip.ocpart" 2>/dev/null || echo 0)
[ "$KRC" = 1 ] && [ "$KHALF" -gt 0 ] && [ "$KHALF" -lt "$ZIPSIZE" ] && [ ! -e "$K10/opencnc3d-install.txt" ] \
    && pass "a package that keeps arriving short fails the install (exit 1), keeping its $KHALF of $ZIPSIZE bytes and writing no record" \
    || fail "a short package gave exit $KRC, a part of $KHALF bytes"
SITE_DIR="$KSITE" restart_site --tag v0.8.0 --media "$KM" --cutoff 0.8.0 --cutoff-agent "$PRIOR_PREFIX"
ksampled "$WORK/k10.log" "$WORK/k10.progress" "$WORK/k10.samples" \
    --install-game --dir "$K10" --media movies,music --media-manifest "$WORK/k-m10.txt" \
    --progress-file "$WORK/k10.progress"
KDATA=$(awk '$1=="macos_data_id"{print $2}' "$KSITE/OpenCNC3D-v0.8.0-manifest.txt")
[ "$KRC" = 0 ] && grep -qx "version 0.8.0" "$K10/opencnc3d-install.txt" \
    && grep -qx "data_id $KDATA" "$K10/opencnc3d-install.txt" && grep -qx "NEW BINARY 0.8.0" "$K10/opencnc3d" \
    && [ -x "$K10/opencnc3d" ] && cmp -s playable/dosmenu.pack "$K10/dosmenu.pack" \
    && pass "the game was installed (exit 0): v0.8.0's files, and an install record with its version and data id" \
    || fail "--install-game did not leave an install (exit $KRC): $(tr '\n' ' ' < "$K10/opencnc3d-install.txt" 2>/dev/null)"
grep -q "\"GET /files/OpenCNC3D-macos-v0.8.0.zip HTTP/1\.[01]\" 206 .*range=bytes=$KHALF-" "$WORK/site.log" \
    && pass "and its package was resumed from byte $KHALF, where the cut-short attempt stopped" \
    || fail "the package was not resumed from byte $KHALF: $(grep 'GET /files/' "$WORK/site.log" | tr '\n' ' ')"
kgood "$K10" . && [ "$(krecord "$K10")" = "movies yes music yes movies_state complete music_state complete " ] \
    && pass "and then the movies and music, both complete" \
    || fail "the movies and music after the install are not complete: $(krecord "$K10")"
[ ! -e "$K10/opencnc3d-update.zip" ] && [ ! -e "$K10/opencnc3d-update.zip.ocpart" ] \
    && pass "and the package is gone from the folder" || fail "the package was left in the folder"
kcontract "$WORK/k10.samples" done - "the install's progress file kept its contract, game and media as one bar"

# 42. A STOP AS THE PACKAGE FINISHES UNPACKING. The cancel file is made the moment the
#     install record appears, which the extractor writes last: too late for the
#     unpacking to hear it. The mirrors are slow, so a movies and music job that did
#     start would write its record within a second and still be running.
say "round K, a Stop that comes as the game finishes unpacking"
K14="$HOST/k-stop-unpack"
mkdir -p "$K14"
printf 'the launcher, which is all the web installer puts here\n' > "$K14/OpenCNC3D.exe"
kman "$WORK/k-m14.txt" . "GDI $KB/media-fault/slow=60000/GDI.ISO 0" \
    "NOD $KB/media-fault/slow=60000/NOD.ISO 0" "COVERT $KB/media-fault/slow=60000/COVERT.ISO 0"
rm -f "$WORK/k14.cancel"
KRC=0
perl -e 'alarm 120; exec { $ARGV[0] } @ARGV' "$LAUNCHER" --install-game --dir "$K14" \
    --media movies,music --media-manifest "$WORK/k-m14.txt" --progress-file "$WORK/k14.progress" \
    --cancel-file "$WORK/k14.cancel" > "$WORK/k14.log" 2>&1 &
KPID=$!
i=0
while [ $i -lt 1200 ] && kill -0 "$KPID" 2>/dev/null; do
    [ -f "$K14/opencnc3d-install.txt" ] && break
    nap
    i=$((i + 1))
done
: > "$WORK/k14.cancel"
wait "$KPID" || KRC=$?
sed 's/^/   /' "$WORK/k14.log"
KGAME="not installed"
grep -qx "version 0.8.0" "$K14/opencnc3d-install.txt" 2>/dev/null && KGAME="installed"
[ "$KRC" = 2 ] && [ ! -e "$K14/opencnc3d-media.txt" ] && [ -z "$(find "$K14" -name '*.ocpart')" ] \
    && [ "$(kfield "$WORK/k14.progress" state)" = cancelled ] \
    && pass "it ends cancelled (exit 2) with no movies or music job started and no choice recorded (the game: $KGAME)" \
    || fail "a Stop as the package finished unpacking gave exit $KRC, state '$(kfield "$WORK/k14.progress" state)', record '$(krecord "$K14")', parts: $(find "$K14" -name '*.ocpart' | tr '\n' ' ')"

# 43. A FIRST INSTALL'S PART BELONGS TO ONE PACKAGE.
say "round K, a part of one build is not resumed into another"
K15="$HOST/k-part-other"
mkdir -p "$K15"
printf 'the launcher, which is all the web installer puts here\n' > "$K15/OpenCNC3D.exe"
SITE_DIR="$KSITE" restart_site --tag v0.8.0 --media "$KM" --short OpenCNC3D-macos-v0.8.0.zip
krun "$WORK/k15a.log" 90 --install-game --dir "$K15"
K15HALF=$(stat -f %z "$K15/opencnc3d-update.zip.ocpart" 2>/dev/null || echo 0)
[ "$KRC" = 1 ] && [ "$K15HALF" -gt 0 ] \
    && grep -qx "name OpenCNC3D-macos-v0.8.0.zip" "$K15/opencnc3d-update.id.ocpart" 2>/dev/null \
    && pass "a first install cut short keeps its part ($K15HALF bytes) and a tag naming the package it is a piece of" \
    || fail "the cut-short first install gave exit $KRC, a part of $K15HALF bytes, tag: $(tr '\n' ' ' < "$K15/opencnc3d-update.id.ocpart" 2>/dev/null)"
KSITE5="$WORK/k-site5"
mkdir -p "$KSITE5"
mkrelease 0.8.4 "$KSITE5"
SITE_DIR="$KSITE5" restart_site --tag v0.8.4
krun "$WORK/k15b.log" 90 --install-game --dir "$K15"
[ "$KRC" = 0 ] && grep -qx "version 0.8.4" "$K15/opencnc3d-install.txt" \
    && grep -q "\"GET /files/OpenCNC3D-macos-v0.8.4.zip HTTP/1\.[01]\" 206 .*range=bytes=0-" "$WORK/site.log" \
    && [ ! -e "$K15/opencnc3d-update.zip.ocpart" ] && [ ! -e "$K15/opencnc3d-update.id.ocpart" ] \
    && pass "a newer build published in between is downloaded from its first byte, the old part thrown away, and installed (exit 0)" \
    || fail "the next build over an old part gave exit $KRC: $(grep 'GET /files/' "$WORK/site.log" | tr '\n' ' ')"

say "round K, an ordinary start keeps a first install's part"
K16="$HOST/k-part-kept"
mkinstall "$K16"
SITE_DIR="$KSITE" restart_site --tag v0.8.0 --media "$KM" --short OpenCNC3D-macos-v0.8.0.zip
krun "$WORK/k16a.log" 90 --install-game --dir "$K16"
K16HALF=$(stat -f %z "$K16/opencnc3d-update.zip.ocpart" 2>/dev/null || echo 0)
psnap "$K16" opencnc3d-update.zip.ocpart opencnc3d-update.id.ocpart > "$WORK/k16.before"
h_run "$WORK/k16b.log" "$ROOT" "$LAUNCHER" --dir "$K16" --check
psnap "$K16" opencnc3d-update.zip.ocpart opencnc3d-update.id.ocpart > "$WORK/k16.after"
if [ "$KRC" = 1 ] && [ "$K16HALF" -gt 0 ] && [ -f "$K16/opencnc3d-update.id.ocpart" ]; then
    same "$WORK/k16.before" "$WORK/k16.after" "an ordinary start over an older install keeps a Setup's part ($K16HALF bytes) and its tag"
else
    fail "the cut-short Setup over an older install gave exit $KRC and a part of $K16HALF bytes, so this proves nothing"
fi
SITE_DIR="$KSITE" restart_site --tag v0.8.0 --media "$KM"
krun "$WORK/k16c.log" 90 --install-game --dir "$K16"
[ "$KRC" = 0 ] && grep -qx "version 0.8.0" "$K16/opencnc3d-install.txt" \
    && grep -q "\"GET /files/OpenCNC3D-macos-v0.8.0.zip HTTP/1\.[01]\" 206 .*range=bytes=$K16HALF-" "$WORK/site.log" \
    && pass "and the next Setup resumes it from byte $K16HALF and installs (exit 0)" \
    || fail "the Setup after an ordinary start gave exit $KRC: $(grep 'GET /files/' "$WORK/site.log" | tr '\n' ' ')"

# 44. A FOLDER HOLDING ONLY THE LAUNCHER: the menu art alone, then the offer to install.
say "round K, a folder holding only the launcher: its menu art, then Install"
K17N="$HOST/k-art-none"
mkdir -p "$K17N"
printf 'the launcher, which is all the web installer puts here\n' > "$K17N/OpenCNC3D.exe"
# The small package is downloaded into the folder and removed again, so the folder's own
# time moves; everything in it is compared as before.
snap "$K17N" opencnc3d-update.zip > "$WORK/k17n.before"
krun "$WORK/k17n.log" 60 --fetch-menu-art --dir "$K17N"
snap "$K17N" opencnc3d-update.zip > "$WORK/k17n.after"
[ "$KRC" = 1 ] && grep -q "does not carry dosmenu.pack" "$WORK/k17n.log" \
    && pass "a build whose small package carries no menu art is refused (exit 1), saying so" \
    || fail "--fetch-menu-art from a build with no pack in its small package gave exit $KRC"
same "$WORK/k17n.before" "$WORK/k17n.after" "and everything in the folder is as it was, the small package it looked in removed again"
KSITE6="$WORK/k-site6"
mkdir -p "$KSITE6"
mkrelease 0.8.5 "$KSITE6"
( cd "$WORK/k-pkg-0.8.5" && zip -qX "$KSITE6/OpenCNC3D-macos-v0.8.5-bins.zip" dosmenu.pack )
tools/launcher/make-manifest.sh v0.8.5 "$KSITE6/OpenCNC3D-v0.8.5-manifest.txt" \
    "$KSITE6/OpenCNC3D-macos-v0.8.5.zip" "$KSITE6/OpenCNC3D-macos-v0.8.5-bins.zip" \
    "$KSITE6/OpenCNC3D-windows-v0.8.5.zip" > /dev/null
SITE_DIR="$KSITE6" restart_site --tag v0.8.5
K17="$HOST/k-art"
mkdir -p "$K17"
printf 'the launcher, which is all the web installer puts here\n' > "$K17/OpenCNC3D.exe"
krun "$WORK/k17.log" 60 --fetch-menu-art --dir "$K17"
[ "$KRC" = 0 ] && cmp -s playable/dosmenu.pack "$K17/dosmenu.pack" \
    && [ "$(cd "$K17" && ls -A | LC_ALL=C sort | tr '\n' ' ')" = "OpenCNC3D.exe dosmenu.pack " ] \
    && grep -q '"GET /files/OpenCNC3D-macos-v0.8.5-bins.zip' "$WORK/site.log" \
    && pass "--fetch-menu-art puts the newest build's dosmenu.pack, from its small package, and nothing else into the folder (exit 0)" \
    || fail "--fetch-menu-art gave exit $KRC, the folder holds: $(cd "$K17" && ls -A | tr '\n' ' ')"
h_run "$WORK/k17s.log" "$ROOT" "$LAUNCHER" --dir "$K17" --media-manifest "$WORK/k-m13.txt" \
    --shot "$WORK/k17.png" --scale 1
grep -q '^LBTN|i=0|label=Install|' "$WORK/k17s.log" \
    && [ "$(lapply "$WORK/k17s.log")" = "movies=1|music=1|asks=0|game=1|enabled=1" ] \
    && pass "and the launcher opens there on Install, both boxes ticked, the game included" \
    || fail "the launcher in a folder holding only itself and its art: $(grep '^LBTN|i=0' "$WORK/k17s.log"), '$(lapply "$WORK/k17s.log")'"
K18="$HOST/k-game-no-record"
mkdir -p "$K18"
cp playable/dosmenu.pack "$K18/"
printf 'the game\n' > "$K18/opencnc3d"
h_run "$WORK/k18.log" "$ROOT" "$LAUNCHER" --dir "$K18" --media-manifest "$WORK/k-m13.txt" \
    --shot "$WORK/k18.png" --scale 1
grep -q '^LBTN|i=0|' "$WORK/k18.log" && ! grep -q '^LBTN|i=0|label=Install|' "$WORK/k18.log" \
    && ! grep -q '^LAPPLY|' "$WORK/k18.log" \
    && pass "a folder holding the game and no record, the development play folder's shape, is not offered an install" \
    || fail "a folder with the game and no record: $(grep '^LBTN|i=0' "$WORK/k18.log")"

# 40. An ordinary update afterwards, through the site, with this launcher's agent.
say "round K, v0.8.0 to v0.8.1 over the movies and music"
KSITE2="$WORK/k-site2"
mkdir -p "$KSITE2"
mkrelease 0.8.1 "$KSITE2"
KMEDIAFILES="opencnc3d-media.txt $(awk '{ print $3 }' "$WORK/k-files.txt")"
psnap "$K10" $KMEDIAFILES > "$WORK/k11.before"
SITE_DIR="$KSITE2" restart_site --tag v0.8.1 --prior v0.8.0 --cutoff 0.8.0 --cutoff-agent "$PRIOR_PREFIX"
h_run "$WORK/k11.log" "$ROOT" "$LAUNCHER" --dir "$K10" --update
psnap "$K10" $KMEDIAFILES > "$WORK/k11.after"
grep -q "result:    installed v0.8.1" "$WORK/k11.log" && grep -qx "version 0.8.1" "$K10/opencnc3d-install.txt" \
    && grep -qx "NEW BINARY 0.8.1" "$K10/opencnc3d" \
    && pass "the update to v0.8.1 installed" || fail "the update to v0.8.1 did not install"
grep -F -q "GET /api/builds HTTP/1.1\" 200 - ua=$NOW_AGENT" "$WORK/site.log" \
    && pass "through the site, with this launcher's agent ($NOW_AGENT)" \
    || fail "the update did not ask the site with '$NOW_AGENT'"
same "$WORK/k11.before" "$WORK/k11.after" "and the movies, the music and their record are exactly as they were"
KSITE3="$WORK/k-site3"
mkdir -p "$KSITE3"
mkrelease 0.8.2 "$KSITE3" opencnc3d-media.txt
SITE_DIR="$KSITE3" restart_site --tag v0.8.2
h_run "$WORK/k12.log" "$ROOT" "$LAUNCHER" --dir "$K10" --update
psnap "$K10" $KMEDIAFILES > "$WORK/k12.after"
grep -q "keeps for itself (opencnc3d-media.txt)" "$WORK/k12.log" && grep -qx "version 0.8.1" "$K10/opencnc3d-install.txt" \
    && pass "an update zip carrying opencnc3d-media.txt is refused, and the install stays at v0.8.1" \
    || fail "an update zip carrying the record's name was not refused"
same "$WORK/k11.before" "$WORK/k12.after" "and the record it carried did not replace the player's"

SITE_DIR="" restart_site
fi

echo
if [ "$FAILED" = "0" ]; then
    echo "LAUNCHER SELFTEST: all assertions passed"
else
    echo "LAUNCHER SELFTEST: $FAILED FAILED"
fi
exit $FAILED
