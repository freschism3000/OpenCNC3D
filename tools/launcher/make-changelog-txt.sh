#!/bin/sh
# docs/CHANGELOG.md as the launcher wants to read it: newest entry first.
#
#   tools/launcher/make-changelog-txt.sh <out.txt>
#
# NEWEST FIRST BY VERSION NUMBER, not by position in the file. docs/CHANGELOG.md
# carries three early entries out of order at the top (v0.1.0, v0.0.2, v0.4.0
# before v0.6.2), so a straight copy opens the launcher's panel on a build from
# two weeks ago and tells the player about the wrong one.
#
# Both the shipped CHANGELOG.txt and the copy on the update host come through
# here, so the offline panel and the online one cannot be ordered differently.
set -e
cd "$(dirname "$0")/../.."
OUT="$1"
[ -n "$OUT" ] || { echo "usage: make-changelog-txt.sh <out.txt>" >&2; exit 1; }

python3 - docs/CHANGELOG.md "$OUT" <<'PY'
import re, sys

src = open(sys.argv[1]).read()
i = src.find("\n## ")
if i < 0:
    sys.exit("docs/CHANGELOG.md has no version headings")
parts = [p for p in re.split(r"(?m)^(?=## )", src[i + 1:]) if p.strip()]

def key(entry):
    m = re.search(r"v(\d+)\.(\d+)\.(\d+)", entry.split("\n", 1)[0])
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)

parts.sort(key=key, reverse=True)
open(sys.argv[2], "w").write("\n".join(p.rstrip() + "\n" for p in parts))
print("%s: %d entries, newest first (%s)"
      % (sys.argv[2], len(parts), parts[0].split("\n")[0][:56]))
PY
