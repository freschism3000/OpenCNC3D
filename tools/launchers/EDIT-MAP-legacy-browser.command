#!/bin/sh
# ============================================================================
#  THE BROWSER MAP EDITOR -- the older one, and NOT the one to reach for.
#
#  THE EDITOR IS Editor.app, beside this file. Double-click that. It is native,
#  it edits and plays in one window, and it is what is being worked on.
#
#  This is kept for exactly one reason: it can still paint terrain and raise
#  ground, and the native editor cannot yet. When that lands this file goes.
#
#  It opens a page in your browser and needs Python; it saves into
#  game/authored/ and bakes through tools/stage-skirmish-maps.sh.
# ============================================================================
set -e
cd "$(dirname "$0")/.."

SERVE="tools/heightmap-viewer/serve.py"
[ -f "$SERVE" ] || { echo "Missing $SERVE" >&2; exit 1; }

# The data the page draws from is generated, not committed, so say something useful
# rather than serving a page that comes up empty.
[ -f "tools/heightmap-viewer/public/data/index.json" ] || {
    echo "The editor's map data has not been generated yet. Run these once:" >&2
    echo "    python3 tools/heightmap-viewer/export.py" >&2
    echo "    python3 tools/heightmap-viewer/export_editor.py" >&2
    echo "    python3 tools/heightmap-viewer/export_dosinf.py" >&2
    exit 1
}

PORT="${PORT:-8099}"
export PORT

# Open the browser once the port is actually accepting connections, so the first load
# is not a connection-refused page the user has to reload by hand.
( for i in $(seq 1 40); do
      if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then open "http://127.0.0.1:$PORT/"; exit 0; fi
      sleep 0.25
  done
  echo "server did not come up on port $PORT" >&2 ) &

echo "CNC3D map editor -> http://127.0.0.1:$PORT/"
echo "Ctrl-C to stop."
exec python3 "$SERVE"
