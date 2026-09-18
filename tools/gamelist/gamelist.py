#!/usr/bin/env python3
"""THE INTERNET GAME LIST: the whole service, and a stand-in to develop against.

WHAT IT IS. One URL that answers two methods. A host POSTs a row to say its game is open,
POSTs the same row again every few seconds to say it still is, and POSTs it once more with
"open": false when it closes. A browsing player GETs the list. There is no third route, no
path built from an id, and no DELETE, because a service that is one handler with an
if-statement can be reimplemented anywhere in an afternoon, and being tied to one provider
is the only real risk here.

WHY THE ROW CARRIES ITS OWN KEYS. The host mints both the public id and the secret token
itself, so announcing is a single request with nothing to allocate first and no state to
lose between two calls. The token never appears in a GET and never goes in a URL. A row can
only be changed or withdrawn by whoever knows its token, which is a lock with no accounts
behind it.

WHY THE SERVER FILLS IN THE ADDRESS. A direct row's address is taken from the connection,
never from the body, so a host cannot list somebody else's machine. A RELAYED row has no
address at all: it carries a six-character room code, which is a random id on a relay and
says nothing about where its host lives. That difference is the whole privacy story, and it
is enforced here rather than trusted to the client.

EXPIRY IS THE ONLY CLEANUP THAT WORKS. A game that closes cleanly withdraws itself, but a
game whose machine crashes or loses its network cannot, so every row dies on its own unless
something keeps it alive. Nothing here has to be swept on a timer: a row is simply not shown
once its last heartbeat is older than the timeout.

A ROW COMES FROM THE GAME, NEVER FROM A WEB PAGE. Any page on any site can make its
visitors' browsers send a POST here, and a direct row sent that way would list the visitor's
own address. The game's HTTP clients label a row application/json and send no Origin; a
browser adds Origin to every POST, and cannot label a cross-site body application/json
without asking the server first, which nothing here answers. So both are required.

RUN IT:  python3 gamelist.py [--port N] [--ttl SECONDS] [--per-source N]
"""

import argparse
import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROW_VERSION = 1

# A row is dropped when its last heartbeat is older than this. Three missed beats at the
# client's fifteen second interval, so one lost packet does not empty the list.
DEFAULT_TTL = 45.0

# CAPS, AND WHY EACH ONE IS HERE. A public endpoint with no accounts behind it can be filled
# with junk, and every one of these is cheap to enforce and costs an honest host nothing.
MAX_ROWS_PER_SOURCE = 8   # one address cannot crowd the list out on its own
MAX_BODY = 4096           # a row is a few hundred bytes; anything larger is not one

# THE WHOLE LIST IS AS LONG AS THE GAME CAN SHOW. The game keeps the first 32 rows of a
# listing and no more (MB_MAX_ROWS in menu/mpbrowse.h), so a thirty-third row would be
# listed, answered with success, and seen by nobody. A full list refuses the host instead,
# which is a failure the host can read. Raise this only together with the game's own limit.
MAX_ROWS = 32

# AN ID STAYS WITH ITS TOKEN FOR A WHILE AFTER ITS ROW EXPIRES. Every listing shows every
# id, so without this anybody watching could take the id of a game whose host lost its
# connection for a minute, and every heartbeat that host sent afterwards would be refused as
# not its row. For this many seconds after expiry only the token that announced the row can
# bring it back. The game draws a new id for every game it lists, so no honest host is ever
# refused by this.
ID_HOLD = 600.0

HEX32 = re.compile(r"\A[0-9a-f]{32}\Z")
ROOM_CODE = re.compile(r"\A#[0-9A-Z]{3}-[0-9A-Z]{3}\Z")
SCENARIO = re.compile(r"\A[A-Za-z0-9_.-]{1,15}\Z")

# field -> (type, maximum length or value). The lengths match the game's own buffers, so a
# row that fits here fits on the screen that draws it.
STR_FIELDS = {"name": 31, "map": 31, "scenario": 15, "build": 15, "room": 9}
INT_FIELDS = {"players": (0, 8), "max": (1, 8), "port": (1, 65535)}


def _clean_text(value, limit):
    """Printable ASCII only, trimmed to the buffer the game will read it into.

    A game name is typed by a player and goes on other players' screens, so control
    characters and anything outside the 1995 font's range are dropped rather than passed on.
    """
    if not isinstance(value, str):
        return None
    out = "".join(ch for ch in value if 32 <= ord(ch) < 127).strip()
    return out[:limit]


def request_refusal(origin, fetch_site, content_type):
    """The answer to a POST that did not come from the game, or None to go on reading it.

    Judged from the headers alone, before a byte of the body is read. A browser sends
    Origin on every POST and Sec-Fetch-Site on every request it makes; the game sends
    neither. A body that is not labelled application/json is what a web page's form or
    plain fetch sends without asking the server first.
    """
    site = (fetch_site or "").strip().lower()
    if origin is not None or (fetch_site is not None and site not in ("none", "same-origin")):
        return 403, {"ok": False, "error": "a row cannot be sent from a web page"}
    if (content_type or "").split(";", 1)[0].strip().lower() != "application/json":
        return 415, {"ok": False, "error": "a row is sent as application/json"}
    return None


class GameList:
    """The rows, and the lock that lets several connections touch them at once."""

    def __init__(self, ttl, per_source=MAX_ROWS_PER_SOURCE):
        self._ttl = ttl
        self._per_source = per_source
        self._lock = threading.Lock()
        self._rows = {}     # id -> row dict, carrying its token and last-heard time
        self._held = {}     # id -> {"token", "heard"} of a row that expired within ID_HOLD

    def _expire(self, now):
        """Rows past the timeout leave the list but keep their id for ID_HOLD seconds."""
        for rid, row in list(self._rows.items()):
            if now - row["heard"] > self._ttl:
                del self._rows[rid]
                self._held[rid] = {"token": row["token"], "heard": row["heard"]}
        for rid, held in list(self._held.items()):
            if now - held["heard"] > self._ttl + ID_HOLD:
                del self._held[rid]

    def publish(self, row, source_ip, now):
        """Announce, heartbeat or withdraw. Returns (status, body dict)."""
        rid = row.get("id")
        token = row.get("token")
        if not isinstance(rid, str) or not HEX32.match(rid):
            return 400, {"ok": False, "error": "id must be 32 lowercase hex characters"}
        if not isinstance(token, str) or not HEX32.match(token):
            return 400, {"ok": False, "error": "token must be 32 lowercase hex characters"}

        with self._lock:
            self._expire(now)
            existing = self._rows.get(rid)
            # THE TOKEN IS THE ONLY LOCK. A row that exists, or expired recently enough that
            # its id is still held, can only be touched by whoever announced it, and the answer
            # to a wrong token is the same whatever else the body says, so guessing tells an
            # attacker nothing.
            holder = existing or self._held.get(rid)
            if holder and holder["token"] != token:
                return 403, {"ok": False, "error": "not your row"}

            if row.get("open") is False:
                self._rows.pop(rid, None)
                self._held.pop(rid, None)
                return 200, {"ok": True, "listed": False}

            clean = self._validate(row, source_ip)
            if isinstance(clean, str):
                return 400, {"ok": False, "error": clean}

            mine = sum(1 for v in self._rows.values() if v["source"] == source_ip)
            if not existing:
                if len(self._rows) >= MAX_ROWS:
                    return 503, {"ok": False, "error": "the list is full"}
                if mine >= self._per_source:
                    return 429, {"ok": False, "error": "too many games from one address"}
            elif existing["source"] != source_ip and mine >= self._per_source:
                # A HEARTBEAT FROM ANOTHER ADDRESS MOVES THE ROW THERE, and a move is counted
                # like a new row. Without this, rows announced from one address and kept alive
                # from another would count against neither, and two addresses could fill the
                # list. The row stays where it was.
                return 429, {"ok": False, "error": "too many games from one address"}

            clean["token"] = token
            clean["source"] = source_ip
            clean["heard"] = now
            self._held.pop(rid, None)
            self._rows[rid] = clean
            return 200, {"ok": True, "listed": True, "ttl": int(self._ttl)}

    def _validate(self, row, source_ip):
        """Returns a clean row, or a sentence saying what was wrong with this one."""
        if row.get("v") != ROW_VERSION:
            return "unknown row version"

        out = {"id": row["id"]}
        for field, limit in STR_FIELDS.items():
            if field in row and row[field] is not None:
                text = _clean_text(row[field], limit)
                if text is None:
                    return "%s must be text" % field
                out[field] = text
        for field, (lo, hi) in INT_FIELDS.items():
            if field in row and row[field] is not None:
                value = row[field]
                if not isinstance(value, int) or isinstance(value, bool):
                    return "%s must be a whole number" % field
                if not lo <= value <= hi:
                    return "%s is out of range" % field
                out[field] = value

        if not out.get("name"):
            return "a game needs a name"
        if not SCENARIO.match(out.get("scenario", "")):
            return "scenario is not a scenario name"
        if "players" not in out or "max" not in out:
            return "a game needs a player count"
        if out["players"] > out["max"]:
            return "more players than seats"

        for field in ("abi", "scen"):
            value = row.get(field)
            if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xFFFFFFFF:
                return "%s must be a 32 bit number" % field
            out[field] = value

        out["locked"] = bool(row.get("locked"))
        out["relay"] = bool(row.get("relay"))

        # THE TWO KINDS OF ROW, AND THE ONE FIELD THAT SEPARATES THEM.
        if out["relay"]:
            code = out.get("room", "")
            if not ROOM_CODE.match(code):
                return "a relayed game needs a room code"
            # No address is stored at all, so none can be served.
            out.pop("port", None)
        else:
            if "port" not in out:
                return "a direct game needs a port"
            out.pop("room", None)
            # TAKEN FROM THE CONNECTION, NEVER FROM THE BODY, so a host can only ever list
            # itself. This is also the field the host was warned would become public.
            out["addr"] = source_ip
        return out

    def listing(self, now):
        with self._lock:
            self._expire(now)
            rows = []
            for row in self._rows.values():
                public = {k: v for k, v in row.items()
                          if k not in ("token", "source", "heard")}
                public["age"] = int(now - row["heard"])
                rows.append(public)
        rows.sort(key=lambda r: (not r.get("relay"), r.get("name", "")))
        return {"v": ROW_VERSION, "games": rows}


class Handler(BaseHTTPRequestHandler):
    server_version = "cnc3d-gamelist/1"
    games = None

    def _reply(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        # The list is a live thing and a cached copy is a list of games that have gone.
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _source(self):
        return self.client_address[0]

    def do_GET(self):
        self._reply(200, self.games.listing(time.time()))

    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        refusal = request_refusal(self.headers.get("Origin"), self.headers.get("Sec-Fetch-Site"),
                                  self.headers.get("Content-Type"))
        if refusal:
            # The body is not judged, but a small one is read off the connection so the client
            # is not reset before it can read the answer.
            if 0 < length <= MAX_BODY:
                self.rfile.read(length)
            self._reply(*refusal)
            return
        if length < 0 or length > MAX_BODY:
            self._reply(413, {"ok": False, "error": "that is not a game row"})
            return
        raw = self.rfile.read(length) if length else b""
        try:
            row = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            self._reply(400, {"ok": False, "error": "not JSON"})
            return
        if not isinstance(row, dict):
            self._reply(400, {"ok": False, "error": "not a game row"})
            return
        status, payload = self.games.publish(row, self._source(), time.time())
        self._reply(status, payload)

    def do_PUT(self):
        self._reply(405, {"ok": False, "error": "only GET and POST"})

    do_DELETE = do_PUT
    do_PATCH = do_PUT

    def log_message(self, fmt, *args):
        # One line per request, on stdout, so a test run leaves a readable trace.
        print("gamelist: %s - %s" % (self._source(), fmt % args), flush=True)


def main():
    ap = argparse.ArgumentParser(description="the internet game list")
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--ttl", type=float, default=DEFAULT_TTL,
                    help="seconds a row survives without a heartbeat")
    # A STAND-IN ON ONE MACHINE NEEDS A HIGHER CAP. Every row a local test lists arrives
    # from 127.0.0.1, so the per-address cap that protects the public list would stop a
    # test at eight rows. The default is the public list's own.
    ap.add_argument("--per-source", type=int, default=MAX_ROWS_PER_SOURCE,
                    help="rows one address may list (default %d)" % MAX_ROWS_PER_SOURCE)
    args = ap.parse_args()

    Handler.games = GameList(args.ttl, per_source=max(1, args.per_source))
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print("gamelist: listening on http://%s:%d/  rows expire after %gs"
          % (args.host, args.port, args.ttl), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
