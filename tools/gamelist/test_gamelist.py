#!/usr/bin/env python3
"""THE GAME LIST, EXERCISED AGAINST ITSELF. Starts its own server on a free port, so it
needs nothing running and nothing configured.

WHAT IT IS FOR. The list is the one place a direct host's home address becomes public, so
the checks that matter most here are not that the list works but that it refuses: that a
relayed row carries no address at all, that a direct row's address is taken from the
connection rather than from the body (so a host cannot list somebody else's machine), that
a token nobody guessed cannot move a row, that a row nobody is keeping alive goes away on its
own, that an expired row's id cannot then be taken over by somebody watching the list, and
that a web page cannot make its visitors' browsers list them. A row that outlives its game
is a row every player clicks and every player is refused by, and for a direct game it is an
address left standing after its owner quit.

AGAINST A LIST THAT IS ALREADY RUNNING. --url points the same checks at a server somebody
else started, which is how a deployment of the list is proven rather than this stand-in.
Give --ttl the timeout that server was started with (45 seconds unless it says otherwise),
because two of the checks wait for rows to expire. A list that is already running is shared
and sees this machine from the outside, so some checks change shape there, and each one
that does prints a NOTE saying how rather than passing quietly:

  - its rows use ids and tokens drawn fresh for the run, not the fixed ones below, so a row
    somebody else left under a fixed id cannot fail the run and a known token cannot be
    used against a real list;
  - "both rows are listed" and "expires on its own" look for this run's rows, since other
    games may be listed beside them;
  - the direct row is sent with a forged address in its body and in the forwarding headers,
    and the check is that the listed address is none of the forged ones and is a public IPv4
    address, the only kind the game can dial. The exact address is required when the URL is
    this machine, or when it is given with --expect-addr, which is the only way to tell a
    list that sees this machine from one that sees a proxy in front of it. The listed
    address is printed either way;
  - the rows this run listed are withdrawn at the end.

It also checks, there only, what the stand-in cannot get wrong but a deployment can: that
neither method is redirected, that both answers are sent no-store, that a third method is
refused with 405, and that a body over 4 KB is refused with 413.

RUN IT:  python3 test_gamelist.py [--url URL [--expect-addr A.B.C.D]] [--ttl SECONDS]
"""

import argparse
import ipaddress
import json
import os
import secrets
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from urllib.parse import urlsplit

HERE = os.path.dirname(os.path.abspath(__file__))
TTL = 5.0
REMOTE_TTL = 45.0

# Documentation addresses (RFC 5737), so a forged address can never belong to anybody.
FORGED_BODY_ADDR = "198.51.100.23"
FORGED_HEADER_ADDR = "198.51.100.24"

PASS = 0
FAIL = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("PASS  " + name)
    else:
        FAIL += 1
        print("FAIL  " + name + ("  " + str(detail) if detail else ""))


def note(text):
    print("NOTE  " + text, flush=True)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    """A redirect is a failure here, never something to follow. Both HTTP clients the game
    uses turn a redirected POST into a GET and drop the body, so a list that redirects
    accepts nothing while looking as if it does. Refusing it here turns the 3xx into the
    status a check reports."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def _is_ip(text):
    try:
        ipaddress.ip_address(text)
        return True
    except (ValueError, TypeError):
        return False


def _is_public_ipv4(text):
    """An address another player on the internet could dial: IPv4, because the game resolves
    and connects over IPv4 only, and global, because a private, shared or link-local address
    is one the list saw from inside some network rather than the host's own."""
    try:
        ip = ipaddress.ip_address(text)
    except (ValueError, TypeError):
        return False
    return ip.version == 4 and ip.is_global


def main():
    ap = argparse.ArgumentParser(description="check the internet game list against its contract")
    ap.add_argument("--url", help="run against a list that is already running at this URL, "
                                  "exactly as the game would call it")
    ap.add_argument("--ttl", type=float,
                    help="seconds a row survives without a heartbeat on that server "
                         "(default %g with --url, %g for the stand-in)" % (REMOTE_TTL, TTL))
    ap.add_argument("--expect-addr", metavar="A.B.C.D",
                    help="with --url: this machine's public IPv4 address, which the direct "
                         "row must be listed at exactly")
    args = ap.parse_args()
    if args.expect_addr is not None:
        if args.url is None:
            ap.error("--expect-addr needs --url")
        try:
            if ipaddress.ip_address(args.expect_addr).version != 4:
                raise ValueError
        except ValueError:
            ap.error("--expect-addr must be an IPv4 address, the only kind the game can dial")

    remote = args.url is not None
    ttl = args.ttl if args.ttl is not None else (REMOTE_TTL if remote else TTL)
    if remote and urlsplit(args.url).scheme not in ("http", "https"):
        ap.error("--url must be an http or https URL")

    proc = None
    if remote:
        url = args.url
        timeout = 20
        opener = urllib.request.build_opener(_NoRedirect)
    else:
        port = free_port()
        url = "http://127.0.0.1:%d/" % port
        timeout = 5
        opener = urllib.request.build_opener()
        proc = subprocess.Popen(
            [sys.executable, os.path.join(HERE, "gamelist.py"),
             "--port", str(port), "--host", "127.0.0.1", "--ttl", str(ttl)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    last = {"headers": None}

    def send(method, data=None, headers=None):
        hdrs = {"Content-Type": "application/json"} if data is not None else {}
        hdrs.update(headers or {})
        req = urllib.request.Request(url, data=data, headers=hdrs, method=method)
        try:
            with opener.open(req, timeout=timeout) as r:
                return r.status, r.headers, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.headers, e.read()

    def decode(raw):
        try:
            return json.loads(raw)
        except ValueError:
            return {"not_json": raw[:200]}

    def post(body, headers=None):
        s, h, raw = send("POST", json.dumps(body).encode(), headers)
        last["headers"] = h
        return s, decode(raw)

    def get():
        s, h, raw = send("GET")
        last["headers"] = h
        if s != 200:
            raise RuntimeError("GET answered HTTP %s: %r" % (s, raw[:200]))
        return json.loads(raw)

    if remote:
        ids = {c: secrets.token_hex(16) for c in "abcdef012"}
        tokens = {c: secrets.token_hex(16) for c in "123456789"}
    else:
        ids = {c: c * 32 for c in "abcdef012"}
        tokens = {c: c * 32 for c in "123456789"}

    listed_here = []   # (id, token) of every row this run may have left on a shared list

    try:
        # The server is up when it answers, not when the process exists. A list somebody else
        # started is either up or it is not, and each try there can take the whole timeout.
        for _ in range(3 if remote else 100):
            try:
                get()
                break
            except Exception as e:
                reason = e
                time.sleep(0.05)
        else:
            print("FAIL  the service never came up" + ("  %s" % reason if remote else ""))
            return 1

        if remote:
            note("running against %s with rows expiring after %gs; this takes about %d seconds"
                 % (url, ttl, int(ttl * 2.2 + 5)))
            s, h, _ = send("GET")
            check("the list answers a GET with 200, not a redirect", s == 200, s)
            check("and sends the list with Cache-Control: no-store",
                  "no-store" in (h.get("Cache-Control") or ""), h.get("Cache-Control"))

        relay = dict(v=1, id=ids["a"], token=tokens["1"], name="RELAYED GAME",
                     map="Green Acres", scenario="SCM01EA", players=1, max=4,
                     locked=False, abi=3735928559, scen=2596069104,
                     relay=True, room="#K7M-3QX", open=True)
        direct = dict(v=1, id=ids["b"], token=tokens["2"], name="DIRECT GAME",
                      map="Sand Trap", scenario="SCM02EA", players=2, max=8,
                      locked=True, abi=3735928559, scen=2596069104,
                      relay=False, port=17421, open=True)
        if remote:
            # The server must ignore this entirely.
            direct["addr"] = FORGED_BODY_ADDR
        listed_here.extend([(ids["a"], tokens["1"]), (ids["b"], tokens["2"]),
                            (ids["0"], tokens["7"])])

        s, b = post(relay)
        check("a relayed row is accepted", s == 200 and b.get("listed"), (s, b))
        if remote:
            check("and the answer to a publish is sent no-store, not redirected",
                  "no-store" in (last["headers"].get("Cache-Control") or "") and s == 200,
                  (s, last["headers"].get("Cache-Control")))

        if remote:
            forged = {"X-Forwarded-For": FORGED_HEADER_ADDR, "X-Real-IP": FORGED_HEADER_ADDR,
                      "X-Vercel-Forwarded-For": FORGED_HEADER_ADDR}
            s, b = post(direct, forged)
            if s != 200:
                note("a publish carrying forged forwarding headers was answered HTTP %s (%s); "
                     "sending it again without them, so the address check below still runs"
                     % (s, b))
                s, b = post(direct)
        else:
            s, b = post(direct)
        check("a direct row is accepted", s == 200 and b.get("listed"), (s, b))

        listing = get()
        rows = {g["id"]: g for g in listing["games"]}
        if remote:
            note("both rows are listed: a shared list may carry other games, so this looks "
                 "for this run's two rows rather than for exactly two")
            check("both rows are listed", ids["a"] in rows and ids["b"] in rows,
                  sorted(rows)[:10])
        else:
            check("both rows are listed", len(rows) == 2, listing)
        check("no token is ever served",
              all("token" not in g for g in listing["games"]), listing)
        check("a relayed row carries its room code",
              rows.get(ids["a"], {}).get("room") == "#K7M-3QX")
        # A row that is missing must not pass as a row with no address.
        check("A RELAYED ROW CARRIES NO ADDRESS",
              ids["a"] in rows and "addr" not in rows[ids["a"]], rows.get(ids["a"]))
        if remote:
            addr = rows.get(ids["b"], {}).get("addr")
            host = (urlsplit(url).hostname or "").lower()
            loopback = {"127.0.0.1": {"127.0.0.1"}, "localhost": {"127.0.0.1", "::1"},
                        "::1": {"::1"}}.get(host)
            ok = _is_ip(addr) and addr not in (FORGED_BODY_ADDR, FORGED_HEADER_ADDR)
            if args.expect_addr is not None:
                note("the direct row is listed at %s and must be exactly %s, given with "
                     "--expect-addr" % (addr, args.expect_addr))
                ok = ok and addr == args.expect_addr
            elif loopback:
                ok = ok and addr in loopback
            else:
                note("the direct row is listed at %s. Without --expect-addr this can only "
                     "check that it is a public IPv4 address and neither forged one; compare "
                     "it with this machine's public IPv4 address, because a proxy's address "
                     "passes this check too" % addr)
                ok = ok and _is_public_ipv4(addr)
            check("A DIRECT ROW'S ADDRESS CAME FROM THE CONNECTION, NOT THE BODY", ok,
                  rows.get(ids["b"]))
        else:
            check("A DIRECT ROW'S ADDRESS CAME FROM THE CONNECTION, NOT THE BODY",
                  rows["b" * 32].get("addr") == "127.0.0.1", rows["b" * 32])

        s, _ = post(dict(relay, token=tokens["9"], name="STOLEN"))
        check("a wrong token cannot change a row", s == 403, s)
        check("and the row it aimed at is untouched",
              any(g["id"] == ids["a"] and g["name"] == "RELAYED GAME"
                  for g in get()["games"]))

        s, _ = post(dict(relay, id="nothex"))
        check("a bad id is refused", s == 400, s)
        s, _ = post(dict(relay, id=ids["c"], token=tokens["3"], room="NOTACODE"))
        check("a relayed row without a real room code is refused", s == 400, s)
        s, _ = post(dict(direct, id=ids["d"], token=tokens["4"], players=9))
        check("a player count out of range is refused", s == 400, s)
        s, _ = post(dict(direct, id=ids["e"], token=tokens["5"], port=None))
        check("a direct row with no port is refused", s == 400, s)
        s, _ = post(dict(relay, id=ids["f"], token=tokens["6"], v=99))
        check("an unknown row version is refused", s == 400, s)

        # A WEB PAGE CANNOT LIST ITS VISITORS. What a page on another site can make a browser
        # send without asking first: a body labelled text/plain, and an Origin on every POST.
        listed_here.extend([(ids["1"], tokens["8"]), (ids["2"], tokens["8"])])
        s, _, _ = send("POST", json.dumps(dict(direct, id=ids["1"], token=tokens["8"])).encode(),
                       {"Content-Type": "text/plain;charset=UTF-8"})
        check("a row labelled text/plain, as a web page's form sends one, is refused with 415",
              s == 415, s)
        s, _, _ = send("POST", json.dumps(dict(direct, id=ids["2"], token=tokens["8"])).encode(),
                       {"Origin": "https://example.com"})
        check("a row carrying Origin, as every browser POST does, is refused with 403", s == 403, s)
        check("and neither was listed",
              all(g["id"] not in (ids["1"], ids["2"]) for g in get()["games"]))

        s, _ = post(dict(relay, id=ids["0"], token=tokens["7"], name="BAD\x07NAME\x00HERE"))
        row = {g["id"]: g for g in get()["games"]}.get(ids["0"], {})
        check("control characters are stripped from a name typed by a player",
              row.get("name") == "BADNAMEHERE", row)

        s, b = post(dict(direct, open=False))
        check("a row can be withdrawn", s == 200 and b.get("listed") is False, (s, b))
        check("and it leaves the list at once",
              all(g["id"] != ids["b"] for g in get()["games"]))

        # THE ONE CLEANUP THAT SURVIVES A CRASH. A host that loses power withdraws nothing.
        time.sleep(ttl + 1.5)
        if remote:
            note("expires on its own: a shared list may carry other games, so this checks "
                 "that none of this run's rows are left rather than that the list is empty")
            mine = {i for i, _ in listed_here}
            games = get()["games"]
            check("A ROW NOBODY KEEPS ALIVE EXPIRES ON ITS OWN",
                  not any(g["id"] in mine for g in games),
                  [g for g in games if g["id"] in mine])
        else:
            check("A ROW NOBODY KEEPS ALIVE EXPIRES ON ITS OWN", get()["games"] == [], get())

        # AN EXPIRED ID STAYS WITH ITS TOKEN. Every listing shows every id, so a host that
        # lost its connection for a minute must not come back to find its id taken.
        s, _ = post(dict(relay, token=tokens["9"], name="TAKEN OVER"))
        check("an expired row's id cannot be taken by another token", s == 403, s)

        post(relay)
        time.sleep(ttl * 0.6)
        post(relay)
        time.sleep(ttl * 0.6)
        check("a heartbeat keeps a row alive past its timeout",
              any(g["id"] == ids["a"] for g in get()["games"]), get())

        if remote:
            s, _, raw = send("PUT", json.dumps(relay).encode())
            check("a method other than GET or POST is refused with 405",
                  s == 405 and decode(raw).get("ok") is False, (s, raw[:200]))
            big = json.dumps(dict(relay, name="X" * 5000)).encode()
            try:
                s, _, raw = send("POST", big)
                check("a body over 4 KB is refused with 413", s == 413, (s, raw[:200]))
            except (urllib.error.URLError, ConnectionError) as e:
                check("a body over 4 KB is refused with 413", False,
                      "the connection failed instead: %s" % e)

        print("\n%d passed, %d failed" % (PASS, FAIL))
        return 1 if FAIL else 0
    except Exception as e:
        # A list that stops answering part way is a failure to report, not a traceback.
        print("FAIL  the list stopped answering part way: %s: %s" % (type(e).__name__, e))
        print("\n%d passed, %d failed" % (PASS, FAIL + 1))
        return 1
    finally:
        if remote and listed_here:
            withdrawn = 0
            for rid, token in listed_here:
                try:
                    s, _ = post({"v": 1, "id": rid, "token": token, "open": False})
                    withdrawn += 1 if s == 200 else 0
                except Exception:
                    pass
            note("withdrew %d of the %d rows this run may have listed"
                 % (withdrawn, len(listed_here)))
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    sys.exit(main())
