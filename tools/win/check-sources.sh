#!/bin/sh
# Does the Windows build compile the same set of sources the Mac build does?
#
# This is the anti-drift guard described at the top of sources.sh. It reads the two Mac
# build scripts, extracts every .c and .cpp filename they hand to a compiler, and
# compares that set against the Windows lists. Any difference is a hard failure with the
# offending filenames printed, because the failure mode being defended against is a
# Windows build that quietly stops matching the Mac one.
#
# Deliberately excluded from the comparison, with reasons:
#   audio_null.c        never built by either; it is the silent-device harness
#   playvqa.c           a standalone video test tool, not part of either binary
#   gate_optlayout.c    a standalone GATE binary (the pause dialog's geometry), not part
#                       of either shipped binary. Same category as playvqa.c. It sits in
#                       game/build.sh only because it links dosopt.o and dosbar.o and must
#                       never go stale against them. CONSEQUENCE, written down rather than
#                       left to be found as a red gate on the other machine: the Windows
#                       box has no gate_optlayout, so gates.sh's G57 is a macOS-only leg
#                       until a cross-compile rule is added. A known gap.
#   brain/host/*        host-side diagnostic tools, built by hand when needed
set -e
cd "$(dirname "$0")/../.."
. tools/win/sources.sh

win_list=$(for f in $WIN_GAME_C $WIN_MENU_C $WIN_VIDEO_C $WIN_AUDIO_C $WIN_APP_C \
                    $WIN_EYES_CPP $WIN_APP_CPP; do basename "$f"; done | sort -u)

# What the Mac scripts actually compile. Both scripts name their sources as paths with a
# .c or .cpp on the end, so pulling those out of the text is exact rather than a guess.
# The AUDIO list in game/build.sh is bare stems in a variable, so that one is expanded
# from its own loop line.
mac_list=$( { grep -oE '\.\./[a-z]+/[a-zA-Z0-9_]+\.(c|cpp)|(^|[ 	])[a-zA-Z0-9_]+\.(c|cpp)' \
                  app/build.sh game/build.sh | sed 's/^[^:]*://' | tr -d ' \t'
              sed -n '/^AUDIO="/,/"$/p' game/build.sh \
                  | tr ' \\\n' '\n\n\n' | sed 's/AUDIO="//; s/"//' \
                  | grep -E '^[a-z0-9]+$' | sed 's/$/.c/'
              # audio_sdl.c is compiled on its own line in game/build.sh, not via AUDIO.
            } | xargs -n1 basename 2>/dev/null | sort -u \
                | grep -vE '^(audio_null|playvqa|gate_optlayout)\.c$' )

missing=$(comm -13 "$(echo "$win_list" > /tmp/.w$$; echo /tmp/.w$$)" \
                   "$(echo "$mac_list" > /tmp/.m$$; echo /tmp/.m$$)")
extra=$(comm -23 /tmp/.w$$ /tmp/.m$$)
rm -f /tmp/.w$$ /tmp/.m$$

rc=0
if [ -n "$missing" ]; then
    echo "SOURCE DRIFT: the Mac build compiles these and the Windows build does not:" >&2
    echo "$missing" | sed 's/^/  /' >&2
    echo "Add them to tools/win/sources.sh." >&2
    rc=1
fi
if [ -n "$extra" ]; then
    echo "SOURCE DRIFT: the Windows build compiles these and the Mac build does not:" >&2
    echo "$extra" | sed 's/^/  /' >&2
    echo "Remove them from tools/win/sources.sh, or add the exclusion and say why." >&2
    rc=1
fi
[ $rc -eq 0 ] && echo "source lists agree ($(echo "$win_list" | wc -l | tr -d ' ') files)"
exit $rc
