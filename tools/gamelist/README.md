# The internet game list

One URL, two methods. A host announces its open game and keeps saying so; a browsing
player reads the list. That is the whole service, and it is deliberately small enough to
reimplement on any host in an afternoon, because being tied to one provider is the only
real risk in it.

`gamelist.py` is both the reference implementation and the stand-in the game is developed
and gated against. `test_gamelist.py` starts its own copy on a free port and needs nothing
configured.

```
python3 gamelist.py [--port 8099] [--host 0.0.0.0] [--ttl 45]
python3 test_gamelist.py
```

## The contract

```
GET  <URL>     read the list
POST <URL>     announce, heartbeat, or withdraw
```

Anything else answers `405`. There is no path built from an id, and no `DELETE`: a
withdrawal is a `POST` carrying `"open": false`.

### The row a host sends

```json
{
  "v": 1,
  "id":       "9f3c1a2b8e4d5067a1b2c3d4e5f60718",
  "token":    "0badc0de00112233445566778899aabb",
  "name":     "GREEN ACRES BRAWL",
  "map":      "Green Acres",
  "scenario": "SCM01EA",
  "players":  2,
  "max":      4,
  "locked":   true,
  "abi":      3735928559,
  "scen":     2596069104,
  "relay":    true,
  "room":     "#K7M-3QX",
  "open":     true
}
```

The host mints `id` and `token` itself, so announcing is one request with nothing to
allocate first and no state to lose between two calls. `id` is the public key of the row
and appears in every listing. `token` is the secret that proves the row is yours; it is
never returned by a `GET` and never goes in a URL. Both are 32 lowercase hex characters.

`relay` decides which kind of row this is, and the two kinds are not cosmetic variants:

- **`"relay": true`** carries `room`, a six-character code that is a random id on a relay.
  It says nothing about where its host lives, and no address is ever served for it. The
  server compares the address the row came from with others, for the per-address cap, but
  keeps nothing that outlives that comparison: the reference holds it in memory only, and a
  store that writes rows to disk keeps a keyed hash of it instead.
- **`"relay": false`** carries `port`, and the server fills in `addr` **from the
  connection**, ignoring anything in the body. A host can therefore only ever list itself,
  and cannot point the list at somebody else's machine.

That second case publishes the host's home address to everyone who opens the browser. That
is a deliberate choice rather than an oversight, and the game says so plainly before a host
makes it.

### What a browser gets

```json
{ "v": 1, "games": [ { ...row..., "addr": "203.0.113.7", "age": 3 } ] }
```

`token` is never present. `age` is seconds since the row was last heard from. Relayed rows
sort first.

### Staying listed

A row is shown while its last heartbeat is newer than the timeout, which defaults to 45
seconds. A host re-POSTs the same row every 15 seconds, so one lost packet does not empty
the list. **Expiry is the only cleanup that works**: a game whose machine crashes or loses
its network withdraws nothing, so nothing may depend on a clean goodbye.

**An expired row's id stays with its token for ten minutes.** Every listing shows every id,
so without this anybody watching the list could take the id of a game whose host lost its
connection for a minute, and every heartbeat that host sent afterwards would be refused as
not its row. During the hold only the row's own token can bring it back, and it comes back
as a new row. A withdrawn id is not held, and the game draws a fresh id for every game it
lists.

**A heartbeat from a different address moves the row there**, which is what happens when a
host's connection changes. A move counts against the new address like a new row, so rows
announced from one address cannot be kept alive from another to get around the cap.

### Refusals

`400` a malformed row, with a sentence saying which field. `403` a row exists under that
id, or held it within the last ten minutes, and the token does not match; the same answer
is given for a wrong id, so guessing tells an attacker nothing. `403` also answers a POST
that carries an `Origin` header or a `Sec-Fetch-Site` other than `none` or `same-origin`,
and `415` one whose body
is not labelled `application/json`: both are how a browser marks a request a web page made,
and neither is anything the game sends, so a page on another site cannot make its visitors
list themselves. `413` a body larger than 4 KB. `429` too many games from one address,
counting a row that moves there. `503` the list is full.

## Caps, and why each one is there

A public endpoint with no accounts behind it can be filled with junk. Every cap below is
cheap to enforce and costs an honest host nothing: 32 rows in total, 8 rows from any one
address, 4 KB per request, and every string trimmed to the buffer the game will read it
into.

The whole list is 32 rows because that is all the game keeps of a listing (`MB_MAX_ROWS` in
`menu/mpbrowse.h`). A thirty-third row would be listed, answered with success, and seen by
nobody; a full list refuses the host instead, which is a failure the host can read. It also
means four addresses can fill the list, and the only way past that is a game build that
reads more rows, with this cap raised to match. Names typed by players are stripped of anything outside the printable range, because
a game name goes onto other players' screens.

## Where it runs

**Settled: a fourth route beside the ones the site already serves**, at
`https://cnc3dgame.com/api/games`. No new provider, no new domain, no new credential, and
it is the address the game already compiles in, so nothing in the game changes. A separate
small host would isolate a game-list outage from the website and cost one more thing to
run; the game learns exactly one string either way, so the choice stays reversible and
moving is a config change.

Three things about that home are not optional, and each one fails quietly rather than
loudly if it is missed.

- **The rows need a store with key expiry, not memory.** The site's routes are serverless
  and more than one copy of them can be running, so a row POSTed to one copy is invisible
  to a GET served by another, and a restart empties whatever was held. A key-value store
  with a per-key timeout is the exact shape of this problem: a key that expires ten
  minutes and 45 seconds after its last heartbeat is the hold, a row shown only while that
  heartbeat is under 45 seconds old is the liveness rule, and no sweeper has to exist.
- **This route must not be cached.** The other routes on that site are served with a
  minute of shared caching, which is right for a build manifest and wrong for a list whose
  whole content is a few seconds old: it would show games that have finished and hide ones
  that have just opened. It answers `no-store`.
- **The publish route must answer `200` and must never redirect.** Both HTTP backends the
  game uses turn a redirected POST into a GET and drop the body, so a publish that is
  redirected succeeds having sent nothing. Watch for a trailing-slash rule in particular,
  because that is a redirect nobody writes on purpose.
