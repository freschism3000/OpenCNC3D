#!/bin/sh
# Prove that Red Alert's movies and score are downloaded exactly where the Red Alert module
# is installed, and that its score arrives as the header the manifest pins followed by its
# range of the disc.
#
#   tools/launcher/media-module-check.sh [launcher]
#
# The launcher's movies and music boxes cover both games. Red Alert's own, about 724 MB,
# sit under the manifest's `module ra3d` line, so they are fetched, counted as missing and
# summed into the sizes only where launcher/lmodule.c says the module is installed, which
# is also the test the module grid draws Red Alert's tile by. A Tiberian Dawn install
# never fetches them. Five legs, cheapest first:
#
#   1. THE SHIPPED MANIFEST, from the source and always: launcher/lmedia_manifest.c has a
#      module line for ra3d, every file under ra3d/ is beneath it, Red Alert's score
#      carries a head, and the sizes beside Setup's boxes (media-sizes.py) count Red
#      Alert's files only when Setup is told the package carries the module.
#
#   Then, against a stand-in mirror (tools/launcher/fake-site.py, on this machine) serving
#   two disc images made here of random bytes, one standing in for a Tiberian Dawn disc
#   and one for the Allied disc, with a test manifest of the shipped one's shape:
#
#   2. NO MODULE: --media-install movies,music fetches Tiberian Dawn's files and nothing
#      under ra3d/, and records both boxes complete.
#   3. THE MODULE INSTALLED (the files lmodule.c asks for, as stand-ins): the same run now
#      fetches Red Alert's movie and its score, the score byte for byte the pinned head and
#      then the disc's range, and leaves no part file.
#   4. UNTICKED: --media-install none removes Red Alert's movie and score too, and leaves
#      the module's own files where they were.
#   5. THE CONTROL: a manifest with a file under ra3d/ and no module line is refused,
#      naming the file, before anything is fetched.
#
# No window is ever raised: --media-install is the launcher's command-line mode. The only
# network it uses is the stand-in mirror on 127.0.0.1.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
launcher=${1:-"$root/launcher/opencnc3d-launcher"}
case "$launcher" in /*) ;; *) launcher="$PWD/$launcher" ;; esac
[ -x "$launcher" ] || { echo "MMOD|FAIL|no launcher at $launcher (launcher/build.sh builds it)"; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-media-module.XXXXXX")
site_pid=
cleanup() {
    [ -z "$site_pid" ] || kill "$site_pid" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
fail=0
bad() { echo "MMOD|FAIL|$*"; fail=1; }
ok() { echo "MMOD|ok|$*"; }

# -------------------------------------------------------------------------- 1
# The manifest's text, which tools/media/probe-sources.py holds launcher/lmedia_manifest.c
# to byte for byte, read by the media tools' own reader.
sizes=$root/tools/win/installer/media-sizes.py
manifest=$root/launcher/lmedia_manifest.c
if ! python3 -I - "$root" <<'EOF' > "$work/static.txt" 2>&1; then
import os, sys
sys.path.insert(0, os.path.join(sys.argv[1], "tools", "media"))
import medialib as ml
man = ml.Manifest.load(os.path.join(sys.argv[1], "tools", "media", "media-manifest.txt"))
ra = [f for f in man.files if man.module_of(f["relpath"]) == "ra3d"]
loose = [f for f in man.files if f["relpath"].lower().startswith("ra3d/") and f not in ra]
heads = [f for f in ra if f["head"]]
if man.modules != ["ra3d"] or loose:
    sys.exit("the module lines are %r and %d ra3d/ files are outside them" % (man.modules, len(loose)))
if not any(f["relpath"] == "ra3d/audio/SCORES.MIX" for f in heads):
    sys.exit("the manifest has %d Red Alert files and its score has no head" % len(ra))
if any(f["head"] for f in man.files if f not in ra):
    sys.exit("a Tiberian Dawn file carries a head")
print("%d Red Alert files under the module line, %d of them with a head" % (len(ra), len(heads)))
print(sum(f["size"] for f in man.files if f["component"] == "movies" and f not in ra))
EOF
    bad "the shipped manifest: $(tr '\n' ' ' < "$work/static.txt")"
else
    td=$(sed -n 2p "$work/static.txt")
    without=$(python3 -I "$sizes" "$manifest" | sed -n 's/^MOVIES_BYTES=//p')
    with=$(python3 -I "$sizes" "$manifest" --module ra3d | sed -n 's/^MOVIES_BYTES=//p')
    if [ -n "$without" ] && [ "$without" = "$td" ] && [ "${with:-0}" -gt "$without" ]; then
        ok "the shipped manifest: $(sed -n 1p "$work/static.txt"); Setup's movies figure is $without bytes without the module and $with with it"
    else
        bad "Setup's movies figure: without=$without with=$with, Tiberian Dawn's own=$td"
    fi
fi

# -------------------------------------------------------------------------- the mirror
mkdir -p "$work/discs" "$work/site"
python3 -I - "$work/discs" "$work/files.txt" <<'EOF'
import hashlib, os, random, sys
out, files_txt = sys.argv[1], sys.argv[2]
rnd = random.Random(1996)
def noise(n):
    return bytearray(rnd.getrandbits(8) for _ in range(n))
discs = {"GDI": noise(1500000), "RA_ALLIED": noise(2500000)}
lines = []
def put(comp, rel, disc, off, n, head=b""):
    body = noise(n)
    body[:4] = b"FORM"
    discs[disc][off:off + n] = body
    sha = hashlib.sha256(bytes(head) + bytes(body)).hexdigest()
    lines.append("file %s %s %d %s %s %d%s" % (comp, rel, len(head) + n, sha, disc, off,
                                               " " + bytes(head).hex() if head else ""))
put("movies", "dosdata/movies/GDI1.VQA", "GDI", 400001, 150000)
put("movies", "ra3d/movies/ALLY1.VQA", "RA_ALLIED", 300007, 220000)
# The score: a plain header the manifest pins, then the disc's data block.
put("music", "ra3d/audio/SCORES.MIX", "RA_ALLIED", 1200003, 400000, head=noise(258))
for d, b in discs.items():
    with open(os.path.join(out, d + ".ISO"), "wb") as f:
        f.write(b)
with open(files_txt, "w") as f:
    f.write("\n".join(lines) + "\n")
EOF
python3 -I "$root/tools/launcher/fake-site.py" "$work/site" --media "$work/discs" --port 0 \
    > "$work/site.log" 2>&1 &
site_pid=$!
port=
i=0
while [ $i -lt 100 ]; do
    port=$(sed -n 's/^PORT //p' "$work/site.log" | head -1)
    [ -n "$port" ] && break
    kill -0 "$site_pid" 2>/dev/null || break
    i=$((i + 1))
    sleep 0.1
done
[ -n "$port" ] || { echo "MMOD|FAIL|the stand-in mirror did not start: $(head -3 "$work/site.log" | tr '\n' ' ')"; exit 1; }
base="http://127.0.0.1:$port/media"
{
    echo "OCMEDIA 1"
    echo "mirror GDI $base/GDI.ISO 0"
    echo "mirror RA_ALLIED $base/RA_ALLIED.ISO 0"
    echo "module ra3d"
    cat "$work/files.txt"
} > "$work/manifest.txt"
{
    echo "OCMEDIA 1"
    echo "mirror GDI $base/GDI.ISO 0"
    echo "mirror RA_ALLIED $base/RA_ALLIED.ISO 0"
    cat "$work/files.txt"
} > "$work/no-module.txt"

install() {   # install <folder>: an install the media job will lock and write into
    mkdir -p "$1"
    printf 'version 0.8.7\ndata_id media-module-check\n' > "$1/opencnc3d-install.txt"
}
run() {   # run <log> <launcher arguments>...: bounded, sets RC
    _log=$1
    shift
    RC=0
    perl -e 'alarm 60; exec { $ARGV[0] } @ARGV' "$launcher" "$@" > "$_log" 2>&1 || RC=$?
}
intact() {   # intact <install> <relpath>: the file there matches its SHA-256 line
    _sha=$(awk -v r="$2" '$3 == r { print $5 }' "$work/files.txt")
    [ -n "$_sha" ] && [ "$(shasum -a 256 "$1/$2" 2>/dev/null | cut -d' ' -f1)" = "$_sha" ]
}
record() {
    grep -E '^(movies|music|movies_state|music_state) ' "$1/opencnc3d-media.txt" 2>/dev/null | tr '\n' ' '
}

# -------------------------------------------------------------------------- 2
td="$work/td"
install "$td"
run "$work/td.log" --media-install movies,music --dir "$td" --media-manifest "$work/manifest.txt"
if [ "$RC" = 0 ] && intact "$td" dosdata/movies/GDI1.VQA && [ ! -e "$td/ra3d" ] \
   && [ "$(record "$td")" = "movies yes music yes movies_state complete music_state complete " ] \
   && ! grep -q 'RA_ALLIED.ISO' "$work/site.log"; then
    ok "without the Red Alert module the download fetched Tiberian Dawn's movie and nothing of Red Alert's (no ra3d/ folder, no request to its disc), and recorded both boxes complete"
else
    bad "without the module: exit=$RC record=[$(record "$td")] ra3d=$([ -e "$td/ra3d" ] && echo present || echo absent) $(tail -2 "$work/td.log" | tr '\n' ' ')"
fi

# -------------------------------------------------------------------------- 3
ra="$work/ra"
install "$ra"
for need in $(sed -n '/^const char \*const lmod_ra_needs\[\] = {/,/^};/p' "$root/launcher/lmodule.c" \
              | sed -n '/^#ifdef _WIN32/,/^#else/!p' | grep -o '"[^"]*"' | tr -d '"'); do
    mkdir -p "$ra/$(dirname "$need")"
    printf 'a stand-in for %s\n' "$need" > "$ra/$need"
done
( cd "$ra" && find ra3d -type f | LC_ALL=C sort | while read -r f; do shasum "$f"; done ) > "$work/module.before"
run "$work/ra.log" --media-install movies,music --dir "$ra" --media-manifest "$work/manifest.txt"
head_ok=0
if intact "$ra" ra3d/audio/SCORES.MIX; then
    head_hex=$(awk '$3 == "ra3d/audio/SCORES.MIX" { print $8 }' "$work/files.txt")
    [ "$(head -c 258 "$ra/ra3d/audio/SCORES.MIX" | od -An -v -tx1 | tr -d ' \n')" = "$head_hex" ] && head_ok=1
fi
parts=$(find "$ra" -name '*.ocpart' | sed "s|^$ra/||" | tr '\n' ' ')
if [ "$RC" = 0 ] && intact "$ra" ra3d/movies/ALLY1.VQA && [ "$head_ok" = 1 ] && [ -z "$parts" ] \
   && intact "$ra" dosdata/movies/GDI1.VQA \
   && [ "$(record "$ra")" = "movies yes music yes movies_state complete music_state complete " ]; then
    ok "with the module installed the same run fetched Red Alert's movie and its score, the score the pinned 258-byte head and then the disc's range, every file matching its SHA-256, and left no part file"
else
    bad "with the module: exit=$RC head=$head_ok parts=[$parts] record=[$(record "$ra")] $(tail -2 "$work/ra.log" | tr '\n' ' ')"
fi

# -------------------------------------------------------------------------- 4
run "$work/none.log" --media-install none --dir "$ra" --media-manifest "$work/manifest.txt"
( cd "$ra" && find ra3d -type f | LC_ALL=C sort | while read -r f; do shasum "$f"; done ) > "$work/module.after"
left=$(cd "$ra" && for f in ra3d/movies/ALLY1.VQA ra3d/audio/SCORES.MIX dosdata/movies/GDI1.VQA; do [ ! -e "$f" ] || printf '%s ' "$f"; done)
if [ "$RC" = 0 ] && [ -z "$left" ] && cmp -s "$work/module.before" "$work/module.after"; then
    ok "unticked, Red Alert's movie and score went with Tiberian Dawn's, and the module's own files are as they were"
else
    bad "unticked: exit=$RC still there=[$left] module files changed=$(cmp -s "$work/module.before" "$work/module.after" && echo no || echo yes)"
fi

# -------------------------------------------------------------------------- 5
ctl="$work/control"
install "$ctl"
before=$(grep -c 'media: ' "$work/site.log" || true)
run "$work/control.log" --media-install movies,music --dir "$ctl" --media-manifest "$work/no-module.txt"
after=$(grep -c 'media: ' "$work/site.log" || true)
if [ "$RC" != 0 ] && grep -q 'ra3d/movies/ALLY1.VQA is in the ra3d folder' "$work/control.log" \
   && [ "$before" = "$after" ]; then
    ok "the control: a manifest with Red Alert's files and no module line is refused (exit $RC), naming the first, and nothing was fetched"
else
    bad "the control: exit=$RC requests $before -> $after: $(head -2 "$work/control.log" | tr '\n' ' ')"
fi

[ "$fail" -eq 0 ] && echo "MMOD|end|0 failures" || echo "MMOD|end|FAILED"
exit $fail
