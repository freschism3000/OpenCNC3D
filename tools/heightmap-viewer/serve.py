#!/usr/bin/env python3
"""serve.py -- the mission editor's companion server: save, bake, play.

WHY THIS EXISTS

The editor is a browser page, and a browser page cannot run a build or start a
program. Without a companion the loop was: press Check and save, find three files in
~/Downloads, copy them somewhere, run stage-skirmish-maps.sh by hand, then find the
binary and pass it the right flags. That is four manual steps between an edit and
seeing it, which is enough friction that nobody checks their work.

This closes it. The page is served from here instead of from `python3 -m http.server`,
and two endpoints are added:

    POST /api/save   the three files land in game/authored/ directly
    POST /api/play   bake that map and launch the game on it

WHAT PLAY ACTUALLY DOES

    sh tools/stage-skirmish-maps.sh --from game/authored <SCEN>
    playable/cnc3d --scen <SCEN> --pack tools/bakery/game/<SCEN>.pack

Both steps already existed and are validated on their own; this is the wiring. The
bake takes the better part of a minute, so play starts a job and the page polls it,
which is why there is a job table rather than one blocking request.

WHAT IT IS NOT

It is not hot reload -- but only because this server launches a separate process, not
because the engine cannot do it. cnc_eyes restarts a mission in-process already:
game_shutdown() followed by game_boot(), which is what the application shell runs
between missions and what the `remission` script verb exposes (cnc_eyes.cpp:13538),
optional SCEN and PACK included. An earlier note here said hot reload was "engine work
and a separate job"; that was wrong, and the native editor inherits the capability
rather than having to build it.

SAFETY

Binds 127.0.0.1 only, and every scenario name is checked against a strict pattern
before it is ever joined onto a path -- these endpoints run a shell script and launch
a binary, so a name that could contain `..` or a slash is the one input that must not
be trusted.
"""

import base64
import json
import os
import re
import subprocess
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PUBLIC = os.path.join(HERE, "public")
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
AUTHORED = os.path.join(ROOT, "game", "authored")
STAGE = os.path.join(ROOT, "tools", "stage-skirmish-maps.sh")
PACKS = os.path.join(ROOT, "tools", "bakery", "game")

# The engine binary. playable/ is the staged tree the game actually runs from; app/ is
# the build output. Prefer the staged one, because that is the tree whose data
# directories match what the bake writes.
BINARIES = [os.path.join(ROOT, "playable", "cnc3d"),
            os.path.join(ROOT, "app", "cnc3d")]

# A scenario name is four to eight of A-Z and 0-9 and nothing else. This is the only
# untrusted string that reaches a path join, a shell script and a process argument.
SCEN_RE = re.compile(r"^[A-Z0-9]{4,8}$")

JOBS = {}
JOB_LOCK = threading.Lock()
NEXT_JOB = [1]



def is_canon(scen):
    """Is this scenario cartridge or disc data rather than something you made?

    git tracks the mission files that came off the cartridge and the 1995 disc. Those
    are the corpus every part of this pipeline is validated against, and the bake refuses
    to stage over them -- for good reason: an earlier version of that staging step did
    not, and destroyed SCG01EA's INI and BIN during a test.
    """
    for rel in ("game/missions/%s.INI" % scen, "game/missions/%s.BIN" % scen):
        r = subprocess.run(["git", "-C", ROOT, "ls-files", "--error-unmatch", rel],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            return True
    return False


def derive_name(scen):
    """A free scenario name for an edit of a shipped map.

    Editing a cartridge mission is the ordinary thing to do -- it is what the editor is
    FOR -- so the moment you play one, you are playing a copy, and the copy needs a name
    of its own. Making the user invent one is homework the tool should do itself. The
    mapping is remembered for the session so a second Play does not mint a third name and
    leave two stale packs behind.
    """
    if scen in DERIVED:
        return DERIVED[scen]
    for n in range(91, 100):
        cand = "SCM%02dEA" % n
        if not is_canon(cand) and cand not in DERIVED.values():
            DERIVED[scen] = cand
            return cand
    return None


DERIVED = {}


def game_binary():
    for p in BINARIES:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def check_scen(scen):
    if not isinstance(scen, str) or not SCEN_RE.match(scen):
        raise ValueError("scenario name must be 4-8 characters of A-Z and 0-9; got %r"
                         % (scen,))
    return scen


def run_job(job, scen):
    """Bake the authored map, then launch the game on it."""
    def log(line):
        with JOB_LOCK:
            job["log"].append(line)

    src = scen
    if is_canon(scen):
        alias = derive_name(scen)
        if not alias:
            log("%s is cartridge data and every SCM91..SCM99 name is taken. "
                "Free one, or save under a name of your own." % scen)
            with JOB_LOCK:
                job["state"] = "failed"
            return
        log("%s is cartridge data, so this edit plays as %s. The shipped mission is "
            "left exactly as it was." % (scen, alias))
        for ext in (".INI", ".BIN", ".HGT"):
            a, b = os.path.join(AUTHORED, scen + ext), os.path.join(AUTHORED, alias + ext)
            if os.path.exists(a):
                with open(a, "rb") as fh:
                    data = fh.read()
                with open(b, "wb") as fh:
                    fh.write(data)
        scen = alias
        with JOB_LOCK:
            job["scen"] = alias

    hgt = os.path.join(AUTHORED, scen + ".HGT")
    log("baking %s from game/authored%s" % (scen, "" if os.path.exists(hgt)
                                            else "  (no authored heights)"))
    log("$ sh tools/stage-skirmish-maps.sh --from game/authored %s" % scen)
    try:
        proc = subprocess.Popen(
            ["sh", STAGE, "--from", AUTHORED, scen],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1)
        for line in proc.stdout:
            log(line.rstrip("\n"))
        rc = proc.wait()
    except Exception as e:                                   # noqa: BLE001
        log("bake failed to start: %s" % e)
        with JOB_LOCK:
            job["state"] = "failed"
        return
    if rc != 0:
        log("bake failed (exit %d) -- not launching" % rc)
        with JOB_LOCK:
            job["state"] = "failed"
        return

    pack = os.path.join(PACKS, scen + ".pack")
    if not os.path.isfile(pack):
        log("the bake reported success but wrote no %s.pack" % scen)
        with JOB_LOCK:
            job["state"] = "failed"
        return

    exe = game_binary()
    if not exe:
        log("no runnable cnc3d binary at playable/cnc3d or app/cnc3d -- build it first")
        with JOB_LOCK:
            job["state"] = "failed"
        return

    log("$ %s --scen %s --pack %s"
        % (os.path.relpath(exe, ROOT), scen, os.path.relpath(pack, ROOT)))
    try:
        # Detached on purpose: the game outlives this request, and the editor stays
        # usable while it runs.
        subprocess.Popen([exe, "--scen", scen, "--pack", pack],
                         cwd=os.path.dirname(exe),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         start_new_session=True)
    except Exception as e:                                   # noqa: BLE001
        log("could not launch: %s" % e)
        with JOB_LOCK:
            job["state"] = "failed"
        return
    log("launched. Quit the game to come back; your edits are still here.")
    with JOB_LOCK:
        job["state"] = "done"


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=PUBLIC, **kw)

    def log_message(self, fmt, *args):
        # One line per API call; the static file traffic is noise.
        if self.path.startswith("/api/"):
            sys.stderr.write("  %s %s\n" % (self.command, self.path))

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 32 * 1024 * 1024:
            raise ValueError("request body is %d bytes; that is not a map" % n)
        return json.loads(self.rfile.read(n) or b"{}")

    # ---------------------------------------------------------------- GET
    def do_GET(self):
        if self.path.startswith("/api/job"):
            jid = self.path.split("id=")[-1]
            with JOB_LOCK:
                job = JOBS.get(jid)
                if not job:
                    return self._json(404, {"error": "no such job"})
                return self._json(200, {"state": job["state"], "log": job["log"]})
        if self.path == "/api/hello":
            exe = game_binary()
            return self._json(200, {
                "ok": True,
                "authored": os.path.relpath(AUTHORED, ROOT),
                "binary": os.path.relpath(exe, ROOT) if exe else None,
            })
        return super().do_GET()

    # ---------------------------------------------------------------- POST
    def do_POST(self):
        try:
            if self.path == "/api/save":
                return self.save()
            if self.path == "/api/play":
                return self.play()
        except ValueError as e:
            return self._json(400, {"error": str(e)})
        except Exception as e:                               # noqa: BLE001
            return self._json(500, {"error": "%s: %s" % (type(e).__name__, e)})
        self._json(404, {"error": "no such endpoint"})

    def save(self):
        d = self._body()
        scen = check_scen(d.get("scen"))
        os.makedirs(AUTHORED, exist_ok=True)
        written = []
        # The .INI is text; the .BIN and .HGT arrive base64 because they are bytes and
        # JSON has no way to carry those.
        if d.get("ini"):
            p = os.path.join(AUTHORED, scen + ".INI")
            with open(p, "w") as fh:
                fh.write(d["ini"])
            written.append(os.path.basename(p))
        for key, ext in (("bin", ".BIN"), ("hgt", ".HGT")):
            if not d.get(key):
                continue
            raw = base64.b64decode(d[key])
            p = os.path.join(AUTHORED, scen + ext)
            with open(p, "wb") as fh:
                fh.write(raw)
            written.append("%s (%d bytes)" % (os.path.basename(p), len(raw)))
        self._json(200, {"ok": True, "dir": os.path.relpath(AUTHORED, ROOT),
                         "written": written})

    def play(self):
        d = self._body()
        scen = check_scen(d.get("scen"))
        ini = os.path.join(AUTHORED, scen + ".INI")
        binf = os.path.join(AUTHORED, scen + ".BIN")
        if not os.path.isfile(ini):
            raise ValueError("no %s.INI in game/authored -- save first" % scen)
        if not os.path.isfile(binf):
            raise ValueError(
                "no %s.BIN in game/authored. The editor writes one only when a TILE "
                "changed; a map whose terrain came from a shipped scenario still needs "
                "that scenario's .BIN beside the .INI." % scen)
        with JOB_LOCK:
            jid = str(NEXT_JOB[0])
            NEXT_JOB[0] += 1
            job = {"state": "running", "log": [], "scen": scen, "t": time.time()}
            JOBS[jid] = job
        threading.Thread(target=run_job, args=(job, scen), daemon=True).start()
        self._json(200, {"job": jid})


def main():
    """Port order: the PORT environment variable, then argv, then 8099.

    PORT first because the preview harness assigns one and passes it that way; a
    hardcoded argument would make the server ignore the assignment and then collide
    with whatever already holds the fixed port. Nothing here needs a specific port --
    the page fetches its own endpoints relatively."""
    port = int(os.environ.get("PORT") or (sys.argv[1] if len(sys.argv) > 1 else 8099))
    exe = game_binary()
    print("CNC3D mission editor")
    print("  http://127.0.0.1:%d/" % port)
    print("  authored maps -> %s" % os.path.relpath(AUTHORED, ROOT))
    print("  play launches -> %s" % (os.path.relpath(exe, ROOT) if exe
                                     else "NOTHING: no cnc3d binary built yet"))
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
