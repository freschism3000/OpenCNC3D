#!/bin/sh
# A HOUSE BIT IS NEVER SHIFTED OUT OF AN INT.
#
# brain/xl exists to take the engine to 64 multiplayer houses. Every house mask in it is
# uint64_t, and the one thing a wider FIELD does not fix is the VALUE being shifted into
# it: `1 << house` is an int shift, so at house 32 and above it is undefined behaviour
# rather than a lost bit. The result is whatever the optimiser chose that day, on that
# compiler, which is the worst possible shape for a bug in a simulation that every peer
# has to agree on.
#
# So the rule is spelled 1ULL everywhere and this script is what keeps it spelled that
# way, because the failure it prevents is invisible at today's roster of twelve houses and
# stays invisible until the roster is grown.
#
#   tools/xl-house-mask-guard.sh          check brain/xl
#   tools/xl-house-mask-guard.sh --self-test   prove the pattern still matches
#
# NO \b IN THE PATTERN. grep -E accepts it and then matches nothing, silently, which is
# how a guard comes to report a clean tree forever. Boundaries are explicit character
# classes, and --self-test exists so a pattern that has stopped matching says so.
set -e
cd "$(dirname "$0")/.."

XLDIR=brain/xl/tiberiandawn

# A bare `1` shifted by anything whose name ends in house/House, with no U or L suffix.
# The leading class rejects the 1 in `1ULL`, `21`, `0x1` and the like.
#
# THE RUN BETWEEN `<<` AND THE NAME IS DELIBERATELY PERMISSIVE, and the first version of
# this pattern was not, which is how it came to report a clean tree over two live shifts.
# It allowed an optional open paren and then only name characters, so it saw
# `1 << house` and missed `1 << (int)PlayerPtr->Class->House`: after the paren it matched
# `int` and then wanted the name immediately, but met `)`. A cast, a member access or an
# array index between the shift and the name all defeated it in the same way. Now anything
# up to a `;` or a `/` is allowed, so the whole expression is spanned and only a statement
# end or a comment stops it. A false positive here is a line somebody has to look at; a
# false negative is the entire point of the guard, silently.
PATTERN='(^|[^0-9A-Za-z_UL])1[[:space:]]*<<[^;/]*[Hh]ouse'

if [ "$1" = "--self-test" ]; then
    # EVERY SHAPE THE TREE HAS ACTUALLY CONTAINED, not just the textbook one. The two cast
    # forms below are the lines this guard missed while reporting clean, so they are the
    # cases most worth keeping: a self-test that only covers what the pattern was written
    # against proves nothing about what the tree will grow next.
    #
    # ONE SHAPE IS DELIBERATELY ABSENT: a shift on a short name with the word house only in
    # a trailing comment, as in `(1 << (int)h)  /* per house */`. The pattern stops at the
    # comment and does not flag it, which is correct: the shift there is on `h` and this
    # guard is about names that end in house. It was tried as a probe and the self-test
    # rejected it, which is the self-test doing its job on the test rather than the code.
    bad_count=0
    for probe in \
        'mask |= (1 << house);' \
        'selected = (obj->IsSelectedMask & (1 << (int)PlayerPtr->Class->House)) ? 1 : 0;' \
        'IsDiscoveredByPlayerMask &= ~(1 << (int)PlayerPtr->Class->House);' \
        'flags = 1 << HousesType(i);' \
        'm |= 1 << Houses.Raw_Ptr(i)->Class->House;'
    do
        if printf '%s\n' "$probe" | grep -Eq "$PATTERN"; then
            bad_count=$((bad_count + 1))
        else
            echo "xl house mask guard: SELF-TEST FAILED -- the pattern no longer matches" >&2
            echo "  this known-bad line, so it is reporting a clean tree by accident:" >&2
            echo "    $probe" >&2
            exit 1
        fi
    done
    echo "xl house mask guard: self-test ok ($bad_count known-bad shapes still match)"

    # And it must NOT match the correct spelling, in any of the same shapes, or every
    # clean file trips it and the guard gets switched off.
    good_count=0
    for probe in \
        'mask |= (1ULL << house);' \
        'selected = (obj->IsSelectedMask & (1ULL << (int)PlayerPtr->Class->House)) ? 1 : 0;' \
        'IsDiscoveredByPlayerMask &= ~(1ULL << (int)PlayerPtr->Class->House);'
    do
        if printf '%s\n' "$probe" | grep -Eq "$PATTERN"; then
            echo "xl house mask guard: SELF-TEST FAILED -- the pattern matches 1ULL too," >&2
            echo "  so it would refuse the correct spelling:" >&2
            echo "    $probe" >&2
            exit 1
        fi
        good_count=$((good_count + 1))
    done
    echo "xl house mask guard: self-test ok ($good_count correct spellings are not flagged)"
    exit 0
fi

[ -d "$XLDIR" ] || { echo "xl house mask guard: no $XLDIR; nothing to check"; exit 0; }

# COMMENT LINES ARE NOT SHIFTS. The rule is documented inside the fork itself, in a block
# comment that quotes the bad spelling in order to forbid it, and the guard flagged its own
# documentation. Dropping lines whose first non-space characters open or continue a comment
# is the whole filter; it cannot hide real code, because a shift that executes never begins
# its line with * or //. Proven by re-injecting a bare shift into a code line and watching
# it still be caught.
HITS=$(grep -REn "$PATTERN" "$XLDIR" --include='*.cpp' --include='*.h' 2>/dev/null \
       | grep -vE '^[^:]*:[0-9]+:[[:space:]]*(\*|//|/\*)' || true)

if [ -n "$HITS" ]; then
    echo ""
    echo "XL HOUSE MASK: a house bit is being shifted out of an int."
    echo ""
    printf '%s\n' "$HITS" | sed 's/^/  /'
    echo ""
    echo "  Spell it 1ULL. At house 32 and above a bare 1 << is undefined behaviour, not"
    echo "  a truncation, so this cannot be left to be found by a player at slot 32."
    echo ""
    exit 1
fi

echo "xl house mask guard: clean"
