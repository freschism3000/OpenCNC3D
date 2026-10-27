#!/bin/sh
# Prove that what follows "--" on the launcher's command line reaches the game, all
# of it, in order.
#
#   tools/launcher/forward-check.sh [launcher] [play-folder]
#
# The launcher once kept the game's arguments in a list of 16 that simply stopped
# copying when it was full. A Red Alert skirmish run with 17 of them handed the game
# "--script" without its file, the game printed its usage and exited 2, and nothing
# said an argument had been left behind. Three legs, cheapest first:
#
#   1. A STAND-IN GAME, a shell script in a scratch folder that writes down every
#      argument it is given. The launcher forwards twenty, the last "--script FILE",
#      one with a space in it, and the stand-in must have received exactly those
#      twenty. Then the longest list the launcher carries, which must arrive whole,
#      and one argument more, which must be REFUSED, naming the argument it would
#      have dropped, with the stand-in never started.
#   2. THE WINDOWS COMMAND LINE, which cannot be run here: forward-check.c builds it
#      with the launcher's own code and checks it against strings worked from the C
#      runtime's parsing rules.
#   3. THE REAL GAME, only when a play folder is given (game/gates.sh gives it one):
#      the launcher starts the Red Alert module headless with twenty forwarded
#      arguments ending "--script FILE", and the game must run that script to its end
#      with the starting credits the middle of the list asked for: 10000, where a
#      skirmish with no --credits starts on 5000.
#
# No window is ever raised: legs 1 and 2 start nothing graphical, and leg 3 runs the
# game hidden under OPENCNC3D_BACKGROUND=1, as every other gate does.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
launcher=${1:-"$root/launcher/opencnc3d-launcher"}
case "$launcher" in /*) ;; *) launcher="$PWD/$launcher" ;; esac
play=${2:-}
[ -x "$launcher" ] || { echo "FWD|FAIL|no launcher at $launcher (launcher/build.sh builds it)"; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-forward.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
fail=0
bad() { echo "FWD|FAIL|$*"; fail=1; }
# Every launcher run against the stand-in has a TIME LIMIT. A stand-in answers at once;
# anything still running after 30 seconds is a real game the launcher started instead,
# hidden, and it is stopped rather than left to play for ever. perl's alarm because
# macOS has no timeout(1); it outlives the exec, so it reaches whatever the launcher
# becomes.
runl() { OPENCNC3D_BACKGROUND=1 perl -e 'alarm 30; exec @ARGV' "$launcher" "$@"; }

# -------------------------------------------------------------------------- 1
game="$work/game"
mkdir -p "$game"
cat > "$game/opencnc3d" <<'EOF'
#!/bin/sh
: > "$(dirname "$0")/argv.txt"
for a in "$@"; do printf '%s\n' "$a" >> "$(dirname "$0")/argv.txt"; done
EOF
chmod +x "$game/opencnc3d"
# AND IT LOOKS LIKE AN INSTALL, or the launcher does not take --dir at all. A --dir
# holding no dosmenu.pack (and no install record) is passed over for the folder the
# launcher itself sits in, found by walking up from its own path. Run from a play
# folder, as the gate suite runs it, that is the play folder, so this leg started the
# REAL game with the stand-in's arguments, and a hidden game with a script that did
# not exist yet played on with nothing to end it. An empty file is all the test asks
# for, and --play starts the game before anything reads it.
: > "$game/dosmenu.pack"
printf 'version 0.0.0-fixture\nmodule_install 1\n' > "$game/opencnc3d-install.txt"
# The installed-module predicate requires both the program and its brain. First
# prove an incomplete fixture is refused, without ever starting the stand-in.
rc=0
runl --dir "$game" --play > "$work/missing-brain.log" 2>&1 || rc=$?
if [ "$rc" -eq 0 ] || [ -f "$game/argv.txt" ] \
   || ! grep -q 'The selected module is not installed.' "$work/missing-brain.log"; then
    bad "without its TD brain the stand-in was not refused before launch: exit=$rc"
fi
# This presence-only brain is intentionally inert: the shell stand-in above
# receives arguments and never loads a game library. It is not a playable game.
: > "$game/TiberianDawn.dylib"

# Twenty, the last two the script and its file. "--savedir" carries a space, which
# has to arrive as one argument.
set -- --hidden --no-movies --nosound --raskirmish SCM10EA --side soviet --ai 3 \
       --seed 4242 --credits 10000 --savedir "$work/save dir" --nosound --nosound \
       --nosound --script "$work/run.script"
[ $# -eq 20 ] || bad "the twenty-argument list holds $# (the check itself is wrong)"
printf '%s\n' "$@" > "$work/want.txt"
rm -f "$game/argv.txt"
if runl --dir "$game" --play -- "$@" > "$work/l1.log" 2>&1; then
    if [ -f "$game/argv.txt" ] && cmp -s "$game/argv.txt" "$work/want.txt"; then
        echo "FWD|ok|20 forwarded arguments reached the game in order, the last --script $(tail -1 "$game/argv.txt")"
    else
        bad "20 forwarded, the game received $( [ -f "$game/argv.txt" ] && wc -l < "$game/argv.txt" | tr -d ' ' || echo none), last [$( [ -f "$game/argv.txt" ] && tail -1 "$game/argv.txt")]"
    fi
else
    bad "the launcher exited non-zero forwarding 20: $(head -2 "$work/l1.log" | tr '\n' ' ')"
fi

# The longest list it carries, then one more. The limit is read off the header the
# launcher is compiled from, so this check follows it if it ever moves.
max=$(sed -n 's/^#define LC_GAME_ARGS \([0-9]*\).*/\1/p' "$root/launcher/lcmdline.h")
ed=$(sed -n 's/^#define LC_EDITOR_ARGS \([0-9]*\).*/\1/p' "$root/launcher/lcmdline.h")
keep=$((max - ed))
set --
k=1
while [ $k -lt $keep ]; do set -- "$@" "--a$k"; k=$((k + 1)); done
set -- "$@" "--last"
rm -f "$game/argv.txt"
if runl --dir "$game" --play -- "$@" > "$work/l2.log" 2>&1 \
   && [ -f "$game/argv.txt" ] && [ "$(wc -l < "$game/argv.txt" | tr -d ' ')" = "$keep" ] \
   && [ "$(tail -1 "$game/argv.txt")" = "--last" ]; then
    echo "FWD|ok|the longest list the launcher carries, $keep, arrived whole"
else
    bad "$keep forwarded (the limit), the game received $( [ -f "$game/argv.txt" ] && wc -l < "$game/argv.txt" | tr -d ' ' || echo none): $(head -2 "$work/l2.log" | tr '\n' ' ')"
fi
set -- "$@" "--dropped-one"
rm -f "$game/argv.txt"
rc=0
runl --dir "$game" --play -- "$@" > "$work/l3.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ] && [ ! -f "$game/argv.txt" ] && grep -q -- '--dropped-one' "$work/l3.log"; then
    echo "FWD|ok|$((keep + 1)) forwarded is refused (exit $rc) before the game starts, naming --dropped-one"
else
    bad "$((keep + 1)) forwarded: exit=$rc game-started=$( [ -f "$game/argv.txt" ] && echo yes || echo no) message=[$(head -2 "$work/l3.log" | tr '\n' ' ')]"
fi

# -------------------------------------------------------------------------- 2
if cc -std=gnu89 -Wall -Wextra -I"$root/launcher" -o "$work/fwdwin" \
      "$root/tools/launcher/forward-check.c" "$root/launcher/lcmdline.c" 2> "$work/cc.log"; then
    if "$work/fwdwin" > "$work/win.log" 2>&1; then
        echo "FWD|ok|Windows command line: $(grep -c '^FWDWIN|ok' "$work/win.log") cases quoted as the C runtime reads them back, twenty forwarded included"
    else
        bad "Windows command line: $(grep '^FWDWIN|FAIL' "$work/win.log" | head -3 | tr '\n' ' ')"
    fi
else
    bad "forward-check.c did not compile: $(head -3 "$work/cc.log" | tr '\n' ' ')"
fi

# -------------------------------------------------------------------------- 3
if [ -n "$play" ]; then
    play=$(CDPATH= cd -- "$play" && pwd)
    cat > "$work/run.script" <<'EOF'
tick 30
creditsdump
quit
EOF
    mkdir -p "$work/save dir"
    set -- --hidden --no-movies --nosound --raskirmish SCM10EA --side soviet --ai 3 \
           --seed 4242 --credits 10000 --savedir "$work/save dir" --nosound --nosound \
           --nosound --script "$work/run.script"
    # A TIME LIMIT, because a launcher that drops the script's file AND its flag hands
    # the game a skirmish with nothing to end it, and a hidden game plays that for
    # ever. perl's alarm because macOS has no timeout(1); it outlives the exec, so
    # it reaches the game the launcher becomes. A good run takes a few seconds.
    rc=0
    (cd "$play" && OPENCNC3D_BACKGROUND=1 perl -e 'alarm 90; exec @ARGV' \
        "$launcher" --dir . --module ra3d --play -- "$@") > "$work/real.log" 2>&1 || rc=$?
    [ "$rc" -ne 142 ] || echo "(stopped after 90 seconds: the script never ended the game)" >> "$work/real.log"
    if [ "$rc" -eq 0 ] && grep -q '^SCRIPT|end .* 0 failures' "$work/real.log" \
       && grep -q '^CREDITS|credits=10000|' "$work/real.log"; then
        echo "FWD|ok|the Red Alert game ran --script to its end from 20 forwarded arguments, with the 10000 credits asked for"
    else
        bad "the real game, 20 forwarded: exit=$rc script-end=$(grep -c '^SCRIPT|end ' "$work/real.log") credits=[$(grep '^CREDITS|' "$work/real.log" | head -1)] first lines: $(head -3 "$work/real.log" | tr '\n' ' ')$(grep '^(stopped after' "$work/real.log")"
    fi
fi

[ "$fail" -eq 0 ] && echo "FWD|end|0 failures" || echo "FWD|end|FAILED"
exit $fail
