#!/bin/sh
# Does every build have BOTH halves: a changelog entry AND a GitHub release with builds?
#
#   tools/changelog-check.sh          report, exit 1 on drift
#   tools/changelog-check.sh --quiet  same, but silent when clean
#
# A project rule: a build updates the changelog AND the builds on GitHub, and
# neither half goes without the other. He spotted this the obvious way, by reading the
# changelog, seeing v0.5.8 at the top, and finding v0.5.7 as the newest thing he could
# actually download.
#
# THIS IS A DRIFT DETECTOR, NOT A CI GATE, and the difference matters. There is one
# legitimate window where the two halves disagree: release.sh REQUIRES the changelog entry
# to be written before it cuts the build (its rule 3), so between writing the entry and
# running the script the changelog names a version GitHub has never heard of. That window
# is meant to be minutes. Wired into CI it would be red for the length of every release
# round, and a check that is red on purpose is a check nobody reads. So the pending entry
# is reported as PENDING, and only an entry OLDER than the current VERSION counts as drift.
#
# THE CUTOFF. Releases before v0.5.1 predate release.sh, which is why v0.1.0, v0.0.2,
# v0.4.0 and v0.5.0 have entries and no release, and why v0.3.1 is a release with no entry.
# That history is not fixable and not worth failing on forever, so the rule starts where
# the script does.
set -e
cd "$(dirname "$0")/.."

QUIET=0
[ "$1" = "--quiet" ] && QUIET=1

FIRST_SCRIPTED=0.5.1   # the first release tools/release.sh cut; see THE CUTOFF above
VERSION=$(cat VERSION | tr -d '[:space:]')
NEXT=$(tools/version.sh --next)

# Sort key so 0.5.10 lands above 0.5.9 rather than below it, which plain sort does not do.
key() { echo "$1" | awk -F. '{ printf "%05d%05d%05d\n", $1, $2, $3 }'; }
older_than() { [ "$(key "$1")" -lt "$(key "$2")" ]; }

if ! command -v gh >/dev/null 2>&1; then
    echo "changelog-check: gh is not installed, so the GitHub half cannot be read." >&2
    exit 2
fi
# ONE api call for tag AND asset count, as "0.5.7 4" per line. Not `gh release list`,
# whose last column is the PUBLISHED timestamp, not the tag: parsing it with $NF made the
# first run of this script report every release as "vT10:22:53Z" and declare
# fourteen drifts that did not exist. And not one `gh release view` per version either,
# which is a round trip apiece.
RELINFO=$(gh api "repos/{owner}/{repo}/releases" --paginate \
              --jq '.[] | "\(.tag_name) \(.assets|length)"' 2>/dev/null) || {
    echo "changelog-check: gh could not list releases (not authenticated?)." >&2
    exit 2
}
RELEASES=$(echo "$RELINFO" | awk '{print $1}' | sed 's/^v//')

# Every "## C&C 3D vX.Y.Z" heading in the changelog, bare number.
ENTRIES=$(grep '^## ' docs/CHANGELOG.md | sed -n 's/.*[[:space:]]v\([0-9][0-9.]*\).*/\1/p')

FAIL=0
PENDING=""
for v in $ENTRIES; do
    older_than "$v" "$FIRST_SCRIPTED" && continue
    if echo "$RELEASES" | grep -qx "$v"; then continue; fi
    # Anything above the number in VERSION has not been cut yet, so it is written-ahead,
    # not drift. Keyed off VERSION rather than off --next so that an entry drafted two
    # numbers ahead is still reported as pending instead of as a build that went missing.
    if ! older_than "$v" "$VERSION" && [ "$v" != "$VERSION" ]; then
        PENDING="$PENDING $v"
        continue
    fi
    echo "DRIFT: docs/CHANGELOG.md has an entry for v$v and GitHub has no release for it."
    echo "       VERSION says $VERSION, so this build was supposed to be out already. Either"
    echo "       cut it (tools/release.sh) or the publish step failed silently."
    FAIL=1
done

# The other direction: a release nobody wrote down.
for v in $RELEASES; do
    older_than "$v" "$FIRST_SCRIPTED" && continue
    grep -q "^## .*v$v[^0-9.]" docs/CHANGELOG.md && continue
    echo "DRIFT: GitHub has a release for v$v and docs/CHANGELOG.md has no entry for it."
    FAIL=1
done

# A release with notes and nothing to download is the same rule seen from the other side.
for v in $RELEASES; do
    older_than "$v" "$FIRST_SCRIPTED" && continue
    n=$(echo "$RELINFO" | awk -v t="v$v" '$1 == t { print $2; exit }')
    [ -n "$n" ] && [ "$n" -gt 0 ] && continue
    echo "DRIFT: the v$v release carries no build. Notes never go up on their own."
    FAIL=1
done

if [ -n "$PENDING" ]; then
    echo "PENDING:$PENDING has a changelog entry and no build yet."
    echo "        That is the legitimate window: the entry is written first, on purpose."
    echo "        It stops being legitimate the moment the round ends without a release,"
    echo "        which is exactly the state found. Run tools/release.sh."
fi

if [ "$FAIL" = "0" ] && [ -z "$PENDING" ] && [ "$QUIET" = "0" ]; then
    echo "changelog-check: clean. Every build from v$FIRST_SCRIPTED on has an entry and a"
    echo "release carrying its builds."
fi
exit $FAIL
