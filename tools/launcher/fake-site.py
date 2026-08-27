#!/usr/bin/env python3
"""
fake-site.py -- cnc3dgame.com's three build routes, on this machine, in one file.

    tools/launcher/fake-site.py <folder-of-zips> [--tag v0.6.3] [--port 0]

The launcher reads the live site and nothing else, so a test that does not speak
the site's API is not testing the launcher. This serves the same three routes,
with the same JSON shapes, recorded from the real thing on 24 Aug 2026:

    GET /api/builds                {"ok":true,"latest":{"tag":...,"assets":[
                                    {"id":..,"name":..,"size":..,
                                     "platform":"macos|windows",
                                     "kind":"full|binaries"}]},"releases":[...]}
    GET /api/changelog             {"ok":true,"total":N,"entries":[
                                    {"title":..,"version":..,"codename":..,
                                     "date":..,"body":"markdown"}]}
    GET /api/download?asset=<id>   302 -> /files/<name>

THE REDIRECT IS THE POINT OF THAT LAST ROUTE. The real site answers with a 302 to
a short-lived signed githubusercontent URL, so the launcher's HTTP layer has to
follow a CROSS-HOST redirect on both platforms: libcurl needs FOLLOWLOCATION, and
WinINet needs not to have been told otherwise. A test host that served the bytes
directly would pass without ever exercising the one thing most likely to be wrong.

Asset ids are derived from the filename so a run is reproducible, and the port is
taken from the kernel because several sessions work this machine at once and a
hardcoded port means measuring somebody else's server.
"""

import argparse
import json
import os
import sys
import zlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

CHANGELOG = [
    {
        "title": 'C&C 3D v0.6.3 "The Front Door"',
        "version": "0.6.3",
        "codename": "The Front Door",
        "date": "2026-08-24",
        "body": "### New features\n\n- A launcher on both platforms.\n"
                "- Play becomes Update when a newer build is up.\n",
    },
    {
        "title": 'C&C 3D v0.6.2 "Now You See It"',
        "version": "0.6.2",
        "codename": "Now You See It",
        "date": "2026-08-24",
        "body": "### Bugs fixed\n\n- The Hand of Nod dropped its globe.\n",
    },
]


def classify(name):
    """Exactly what the site does: platform and kind out of the filename."""
    platform = "macos" if "macos" in name else "windows" if "windows" in name else None
    kind = "binaries" if name.endswith("-bins.zip") else "full" if name.endswith(".zip") else None
    return platform, kind


class Handler(SimpleHTTPRequestHandler):
    root = "."
    tag = "v0.6.3"
    assets = {}   # id -> filename
    short = None  # a filename to serve TRUNCATED, see --short

    def send_json(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, name):
        path = os.path.join(self.root, name)
        if not os.path.isfile(path):
            self.send_error(404, "no such asset")
            return
        size = os.path.getsize(path)
        # --short models a transfer that DIES MID-FLIGHT: the true length is
        # advertised, in the asset list and in the header, and fewer bytes
        # arrive. Truncating the file on disk instead would shrink what
        # /api/builds reports too, so the launcher would compare a short download
        # against a short expectation, agree, and never exercise the check the
        # test is there to exercise. That is exactly how this test first passed
        # for the wrong reason.
        limit = size // 2 if (self.short and name == self.short) else size
        self.send_response(200)
        self.send_header("content-type", "application/octet-stream")
        self.send_header("content-length", str(size))
        self.send_header("content-disposition", 'attachment; filename=%s' % name)
        self.end_headers()
        sent = 0
        with open(path, "rb") as f:
            while sent < limit:
                chunk = f.read(min(64 * 1024, limit - sent))
                if not chunk:
                    break
                self.wfile.write(chunk)
                sent += len(chunk)

    def do_GET(self):
        u = urlparse(self.path)

        if u.path == "/api/builds":
            entries = []
            for name in sorted(self.assets.values()):
                platform, kind = classify(name)
                item = {
                    "id": [k for k, v in self.assets.items() if v == name][0],
                    "name": name,
                    "size": os.path.getsize(os.path.join(self.root, name)),
                }
                # The site only labels what it recognises. A manifest .txt gets
                # no platform and no kind, which is exactly why the launcher
                # matches it by name.
                if platform:
                    item["platform"] = platform
                if kind:
                    item["kind"] = kind
                entries.append(item)
            latest = {"tag": self.tag, "name": "CNC3D " + self.tag,
                      "published": "2026-08-24T09:05:43Z", "prerelease": False,
                      "assets": entries}
            self.send_json({"ok": True, "latest": latest, "releases": [latest]})
            return

        if u.path == "/api/changelog":
            self.send_json({"ok": True, "total": len(CHANGELOG), "entries": CHANGELOG})
            return

        if u.path == "/api/download":
            q = parse_qs(u.query)
            try:
                aid = int(q.get("asset", ["0"])[0])
            except ValueError:
                self.send_error(400, "bad asset")
                return
            name = self.assets.get(aid)
            if not name:
                self.send_error(404, "no such asset")
                return
            # A 302, like the real one. See the module docstring.
            self.send_response(302)
            self.send_header("location", "/files/" + name)
            self.send_header("content-length", "0")
            self.end_headers()
            return

        if u.path.startswith("/files/"):
            name = os.path.basename(u.path[len("/files/"):])
            self.send_file(name)
            return

        self.send_error(404, "not a route this site has")

    def log_message(self, fmt, *args):
        sys.stderr.write("  site: " + (fmt % args) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--tag", default="v0.6.3")
    ap.add_argument("--short", default=None,
                    help="serve this filename truncated to half, while still "
                         "advertising its true length: a transfer that died")
    ap.add_argument("--port", type=int, default=0,
                    help="0 (the default) takes a free port, which is the only "
                         "safe choice on a machine running several sessions")
    a = ap.parse_args()

    Handler.root = os.path.abspath(a.folder)
    Handler.tag = a.tag
    Handler.short = a.short
    # Ids from the name, so a run is reproducible and a log is readable.
    Handler.assets = {
        (zlib.crc32(n.encode()) & 0x7FFFFFFF): n
        for n in sorted(os.listdir(Handler.root))
        if os.path.isfile(os.path.join(Handler.root, n))
    }

    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    port = srv.server_address[1]
    print("PORT %d" % port, flush=True)
    print("serving %s as %s at http://127.0.0.1:%d/"
          % (Handler.root, a.tag, port), flush=True)
    if a.short:
        print("  NOTE: %s will be served truncated to half its length" % a.short,
              flush=True)
    for aid, name in sorted(Handler.assets.items()):
        print("  asset %d  %s" % (aid, name), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
