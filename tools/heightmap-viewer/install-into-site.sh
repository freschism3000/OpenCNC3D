#!/usr/bin/env bash
# Drop the heightmap viewer into the cnc3dgame.com Next.js app so it serves at /maps.
#
#   ./install-into-site.sh /path/to/cnc3d-web
#
# It copies public/ into the site's public/maps/ and prints the one config change
# next.config.mjs needs. It never edits the site's source; the copy is the only
# write, so it is safe to run against a dirty tree and trivial to undo
# (rm -rf <site>/public/maps).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SITE="${1:-}"

if [ -z "$SITE" ] || [ ! -f "$SITE/next.config.mjs" ]; then
    echo "usage: $0 /path/to/cnc3d-web   (the directory holding next.config.mjs)" >&2
    exit 2
fi
if [ ! -f "$HERE/public/data/index.json" ]; then
    echo "public/data is empty. Run 'python3 export.py' first." >&2
    exit 2
fi

DEST="$SITE/public/maps"
rm -rf "$DEST"
mkdir -p "$DEST"
cp -R "$HERE/public/." "$DEST/"
echo "copied $(find "$DEST" -type f | wc -l | tr -d ' ') files -> $DEST"
du -sh "$DEST"

cat <<'NOTE'

Now the one config change. next.config.mjs needs /maps to land on the static page
WITH a trailing slash, because every asset the viewer loads is relative to the
document. Add to the config object:

  async redirects() {
    return [{ source: '/maps', destination: '/maps/', permanent: false }]
  },
  async rewrites() {
    return [{ source: '/maps/', destination: '/maps/index.html' }]
  },

(If the file already has redirects()/rewrites(), append these entries to the
arrays it returns rather than adding a second function.)

The page carries its own belt-and-braces redirect too, so /maps works even if the
rewrite is missing, at the cost of one extra hop.

Then deploy the site the way it is normally deployed. Check BOTH / and /maps on the
preview URL before promoting: this adds ~4 MB of static assets to the deployment.
NOTE
