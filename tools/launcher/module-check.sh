#!/bin/sh
# Prove that an install shaped like a package, with no movies and no music in it, shows
# the Red Alert module on the launcher's grid and starts it.
#
#   tools/launcher/module-check.sh [launcher] [play-folder]
#
# No package carries the 1995 movies or the score, Red Alert's included: they are on
# tools/media/media-paths.txt, they are downloaded later or never, and the game plays
# without them by file presence alone. The launcher's test for an installed Red Alert
# once asked for ra3d/movies/REDINTRO.VQA, ra3d/movies/PROLOG.VQA and
# ra3d/audio/SCORES.MIX as well, so a folder without them drew Red Alert's slot as
# "Coming soon" and `--module ra3d --play` refused it, while the game itself would have
# run. Four legs, cheapest first:
#
#   1. THE TABLE, from the source and always: every file launcher/lmodule.c's
#      lmod_ra_needs asks for, on both platforms, is one a package carries. None may be on
#      the media list or under a folder on it, and none may be a movie or a score by
#      name (.VQA, SCORES.MIX, TRANSIT.MIX), which the packagers refuse wherever it is.
#
#   With a play folder that has a Red Alert module staged (game/gates.sh gives it its
#   own), a SCRATCH FOLDER is made from it: a link to every entry except the paths on the
#   media list, a folder with one of those beneath it being a real folder of links built
#   the same way. It is then asked rather than trusted: no movie and no score may be
#   reachable anywhere under its ra3d/.
#
#   2. THE PICTURE: the launcher's module grid drawn from that folder with --shot. Its
#      LMOD line for Red Alert must read installed=1 and drawn=1.
#   3. THE GAME: `--module ra3d --play` there starts the real Red Alert game, hidden,
#      which must run a script to its end, find no movie and no theme (MEDIA|movies=0,
#      music=0, the bank holding no theme but its sound effects), and meet the missing
#      title movie and carry on past it.
#   4. THE CONTROL, so the check can go red: the same folder with the Red Alert brain
#      taken out. The grid must draw the slot empty and name the brain as what is
#      missing, and --play must refuse, naming it, without starting the game.
#
# NOT COVERED: the Windows launcher, which cannot run here. It is the same source, and
# leg 1 reads the Windows half of the table as well.
#
# No window is ever raised: --shot draws into memory with no window and no GL, and the
# game runs hidden under OPENCNC3D_BACKGROUND=1, as every other gate does. A --script run
# is silent, so the run makes no sound either.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
launcher=${1:-"$root/launcher/opencnc3d-launcher"}
case "$launcher" in /*) ;; *) launcher="$PWD/$launcher" ;; esac
play=${2:-}
media_list="$root/tools/media/media-paths.txt"
[ -x "$launcher" ] || { echo "MOD|FAIL|no launcher at $launcher (launcher/build.sh builds it)"; exit 1; }
[ -f "$media_list" ] || { echo "MOD|FAIL|no media list at $media_list"; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-module.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
fail=0
bad() { echo "MOD|FAIL|$*"; fail=1; }

# The media list as every other reader takes it: '#' starts a comment, the leading "/"
# and a trailing "/" are optional, and paths are compared without regard to case.
sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' -e 's|^\./||' -e 's|^/||' \
    -e 's|/$||' "$media_list" | grep -v '^$' | tr 'A-Z' 'a-z' > "$work/media.txt" || true
[ -s "$work/media.txt" ] || { echo "MOD|FAIL|the media list $media_list names no path"; exit 1; }

# -------------------------------------------------------------------------- 1
sed -n '/^const char \*const lmod_ra_needs\[\] = {/,/^};/p' "$root/launcher/lmodule.c" \
    | grep -o '"[^"]*"' | tr -d '"' > "$work/needs.txt" || true
needs=$(wc -l < "$work/needs.txt" | tr -d ' ')
if [ "$needs" -lt 8 ]; then
    bad "launcher/lmodule.c has no lmod_ra_needs table this check can read ($needs entries)"
else
    shipped=1
    while IFS= read -r need; do
        low=$(printf '%s' "$need" | tr 'A-Z' 'a-z')
        while IFS= read -r m; do
            case "$low" in "$m"|"$m"/*) bad "Red Alert's test asks for $need, which is on the media list as $m"; shipped=0 ;; esac
        done < "$work/media.txt"
        case "$low" in *.vqa|*/scores.mix|*/transit.mix) bad "Red Alert's test asks for $need, a movie or a score"; shipped=0 ;; esac
    done < "$work/needs.txt"
    [ "$shipped" -eq 0 ] || echo "MOD|ok|Red Alert's test asks for $needs files over both platforms, none of them a movie or a score or on the media list"
fi

# -------------------------------------------------------------------------- 2, 3, 4
if [ -n "$play" ]; then
    play=$(CDPATH= cd -- "$play" && pwd)
    if [ ! -x "$play/ra3d/opencnc3d" ] || [ ! -f "$play/ra3d/RedAlert.dylib" ]; then
        bad "no Red Alert module is staged in $play"
    else
        pkg="$work/package"
        python3 -I - "$play" "$pkg" "$work/media.txt" <<'EOF'
import os, sys
play, out, media_file = sys.argv[1:4]
media = [m for m in open(media_file, encoding='utf-8').read().split('\n') if m]
def walk(rel):
    src = os.path.join(play, rel) if rel else play
    for name in sorted(os.listdir(src)):
        r = f'{rel}/{name}' if rel else name
        low = r.lower()
        if low in media:
            continue
        if any(m.startswith(low + '/') for m in media):
            os.makedirs(os.path.join(out, r), exist_ok=True)
            walk(r)
        else:
            os.symlink(os.path.join(src, name), os.path.join(out, r))
os.makedirs(out, exist_ok=True)
walk('')
EOF
        had=$(cd "$play/ra3d" && find -L . \( -iname '*.vqa' -o -iname 'scores.mix' \) 2>/dev/null | wc -l | tr -d ' ')
        left=$(cd "$pkg/ra3d" && find -L . \( -iname '*.vqa' -o -iname 'scores.mix' -o -iname 'transit.mix' \) 2>/dev/null | head -3 | tr '\n' ' ')
        if [ -n "$left" ]; then
            bad "the scratch folder still holds a movie or a score: $left"
        else
            echo "MOD|ok|a folder shaped like a package: no movie and no score under ra3d/ (the play folder has $had)"

            # ------------------------------------------------------------------ 2
            OPENCNC3D_BACKGROUND=1 "$launcher" --dir "$pkg" --view modules --module ra3d \
                --shot "$work/modules.png" > "$work/shot.log" 2>&1 || true
            ra=$(grep '^LMOD|i=1|' "$work/shot.log" | head -1)
            case "$ra" in
                *"|name=RA3D|installed=1|"*"|drawn=1"*)
                    if [ -s "$work/modules.png" ]; then
                        echo "MOD|ok|the module grid draws Red Alert as installed: $ra"
                    else
                        bad "the grid reads Red Alert as installed and wrote no picture: $(head -2 "$work/shot.log" | tr '\n' ' ')"
                    fi ;;
                "") bad "the launcher printed no LMOD line for Red Alert: $(head -2 "$work/shot.log" | tr '\n' ' ')" ;;
                *) bad "the module grid does not draw Red Alert as installed: $ra" ;;
            esac

            # ------------------------------------------------------------------ 3
            printf 'tick 30\nquit\n' > "$work/run.script"
            mkdir -p "$work/save"
            # A TIME LIMIT, because a hidden game whose script never ends plays for ever.
            # perl's alarm because macOS has no timeout(1); it outlives the exec, so it
            # reaches the game the launcher becomes. A good run takes a few seconds.
            rc=0
            (cd "$pkg" && OPENCNC3D_BACKGROUND=1 perl -e 'alarm 120; exec @ARGV' \
                "$launcher" --dir . --module ra3d --play -- --hidden --raskirmish SCM10EA \
                --side soviet --ai 3 --seed 4242 --savedir "$work/save" \
                --script "$work/run.script") > "$work/game.log" 2>&1 || rc=$?
            [ "$rc" -ne 142 ] || echo "(stopped after 120 seconds: the script never ended the game)" >> "$work/game.log"
            media=$(grep '^MEDIA|' "$work/game.log" | head -1)
            bank=$(grep '^AUDIO|bank|' "$work/game.log" | head -1)
            sfx=$(echo "$bank" | sed -n 's/.*|sfx=\([0-9]*\)\/.*/\1/p')
            themes=$(echo "$bank" | sed -n 's/.*|themes=\([0-9]*\)\/.*/\1/p')
            logo=$(grep -c '^movie: cannot open ra3d/movies/' "$work/game.log" || true)
            if [ "$rc" -ne 0 ] || ! grep -q '^SCRIPT|end .* 0 failures' "$work/game.log"; then
                bad "--module ra3d --play there: exit=$rc script-end=$(grep -c '^SCRIPT|end ' "$work/game.log") first lines: $(head -3 "$work/game.log" | tr '\n' ' ')$(grep '^(stopped after' "$work/game.log" || true)"
            elif ! echo "$media" | grep -q '^MEDIA|movies=0|music=0/'; then
                bad "the game ran there and did not find the folder empty of movies and music: [$media]"
            elif [ "${themes:-x}" != "0" ] || [ "${sfx:-0}" -le 0 ]; then
                bad "the game's audio bank there is not the score's absence alone: [$bank]"
            elif [ "$logo" -lt 1 ]; then
                bad "the game ran there and never met a missing Red Alert movie, so the movie path went unexercised"
            else
                echo "MOD|ok|--module ra3d --play there runs the Red Alert game to the end of its script: $media, $sfx sound effects and no theme, past a missing title movie"
            fi

            # ------------------------------------------------------------------ 4
            # The brain is taken out by removing its LINK. Only when ra3d/ is a real
            # folder of links, which it is while the media list names a path inside
            # it: were ra3d/ itself a link, this would delete the play folder's brain.
            if [ -L "$pkg/ra3d" ] || [ ! -L "$pkg/ra3d/RedAlert.dylib" ]; then
                bad "the scratch folder's ra3d/ is not a folder of links, so the control cannot take the brain out safely"
            else
            rm -f "$pkg/ra3d/RedAlert.dylib"
            # Not --module ra3d here: asked for a module that is not installed, the
            # launcher refuses before it draws, which is the --play leg just below.
            OPENCNC3D_BACKGROUND=1 "$launcher" --dir "$pkg" --view modules \
                --shot "$work/control.png" > "$work/control.log" 2>&1 || true
            ra=$(grep '^LMOD|i=1|' "$work/control.log" | head -1)
            rc=0
            (cd "$pkg" && OPENCNC3D_BACKGROUND=1 perl -e 'alarm 60; exec @ARGV' \
                "$launcher" --dir . --module ra3d --play -- --hidden \
                --script "$work/run.script") > "$work/refused.log" 2>&1 || rc=$?
            case "$ra" in
                *"|installed=0|"*"|missing=ra3d/RedAlert.dylib is missing"*)
                    if [ "$rc" -ne 0 ] && grep -q 'ra3d/RedAlert.dylib' "$work/refused.log" \
                       && ! grep -q '^SCRIPT|' "$work/refused.log"; then
                        echo "MOD|ok|without its brain the slot is drawn empty, naming ra3d/RedAlert.dylib, and --play refuses (exit $rc) before the game starts"
                    else
                        bad "without its brain, --play: exit=$rc message=[$(head -2 "$work/refused.log" | tr '\n' ' ')]"
                    fi ;;
                *) bad "without its brain the grid still reads Red Alert as: [$ra]" ;;
            esac
            fi
        fi
    fi
fi

[ "$fail" -eq 0 ] && echo "MOD|end|0 failures" || echo "MOD|end|FAILED"
exit $fail
