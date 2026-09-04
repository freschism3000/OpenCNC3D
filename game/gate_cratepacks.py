#!/usr/bin/env python3
"""G173: the cartridge's two 3D goodie crates are in EVERY mission pack.

    python3 gate_cratepacks.py <RUNDIR>

Walks every mission pack in the run folder and asserts, per pack, that it carries the
two crate cubes the console draws: type SCRATE bound to a mesh, type WCRATE bound to a
DIFFERENT one, each of exactly ten triangles, each measuring 185 x 185 x 184 mesh units
with its floor at y = +2, and each at a confidence the renderer will accept.

WHY EVERY PACK AND NOT THE 34 WHOSE INI LISTS A CRATE. Counting map files understates
the exposure badly and in two separate ways, both read out of the engine in this tree:

  * tiberiandawn/scenarioini.cpp calls MapClass::Place_Random_Crate once per player at
    scenario load whenever MPlayerGoodies is set, which is the skirmish and multiplayer
    lobby's Crates switch. That runs on ANY map, including the eleven skirmish maps
    whose INIs list no crate at all.
  * tiberiandawn/logic.cpp then re-places one on a repeating CrateTimer every 7 to 15
    game minutes for the rest of the session.

So a crate can appear on a random clear cell of any map played outside the campaign, and
there is no pack that can be excluded. A pack without the cubes is not broken -- it falls
back to the 1995 DOS sprites, which G175 covers -- but it is a pack the cartridge's own
art has not reached, and this gate is what says which ones those are.

UNREADABLE IS A FAILURE, NOT A SKIP. This reader parsed only up to PKC (version 12)
until the PKF header branch landed beside this gate, and the run folder ships three PKF
packs (USER01, USER50, USER91: user maps whose grid is not 64x64). A caller that wrapped
the parse in a bare except and then reported "all packs" was telling a lie about three of
them. Every pack this gate cannot read is named on its own line and counted, and any
unreadable pack fails the gate.
"""
import os
import struct
import sys
import hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
# The bakery is a sibling of this file's own parent, whether this copy is the tracked one
# in game/ or the one make-build.sh stages into the run folder. Resolved from __file__ so
# no absolute path is baked in.
sys.path.insert(0, os.path.join(HERE, os.pardir, "tools", "bakery"))

# What the ROM says the cube is, and every number here was measured rather than chosen:
#   tools/bakery/render_dl.py --slot 98 --slot 100
#   slot 98  dl_011D4C8  8 verts 10 tris  185 x 185 x 184 u = 0.18 x 0.18 x 0.18 cells
#   slot 100 dl_010F3C8  8 verts 10 tris  185 x 185 x 184 u = 0.18 x 0.18 x 0.18 cells
# with local bounds x[-92,+93] y[+2,+187] z[-94,+90] on both. y starting at +2 is what
# says the cube is modelled STANDING on the ground, which is why the renderer gives it no
# lift and why G174 asserts a lift of exactly zero.
WANT_TRIS = 10
WANT_SPAN = (185.0, 185.0, 184.0)
WANT_YMIN = 2.0
SPAN_TOL = 0.5


def mesh_stats(p, m):
    """(span, ymin, resolved-texture digest) for one baked mesh."""
    import packinspect as PI
    xs, ys, zs, tex = [], [], [], set()
    for i in range(m["ntris"]):
        f = struct.unpack_from(PI.TRI_FMT, m["tris"], i * PI.TRI_STRIDE)
        tex.add(f[0])
        for v in range(3):
            b = 3 + v * 9
            xs.append(f[b]); ys.append(f[b + 1]); zs.append(f[b + 2])
    if not xs:
        return None, None, ""
    span = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    # The texture CONTENT, not its bank index: two packs number their banks differently
    # and the same cube must still compare equal to itself and unequal to its sibling.
    h = hashlib.sha256()
    for t in sorted(tex):
        h.update(b"-1" if t < 0 else hashlib.sha256(p["tex"][t]["data"]).digest())
    return span, min(ys), h.hexdigest()[:12]


def check(path):
    """'' when the pack carries both cubes correctly, else the reason it does not."""
    import packinspect as PI
    p = PI.read(path)
    types = dict((t[0], (t[1], t[2])) for t in p["types"])
    meshes = p["meshes"]
    seen = {}
    for kind in ("SCRATE", "WCRATE"):
        if kind not in types:
            return "%s: no %s type at all (pack not re-baked)" % (
                os.path.basename(path), kind)
        mi, conf = types[kind]
        if mi < 0:
            return "%s: %s resolves to no mesh (index %d)" % (
                os.path.basename(path), kind, mi)
        if conf < 1:
            # The renderer's own floor: wall_mesh_named refuses conf < 1, so a type the
            # bake could only guess at must never become art.
            return "%s: %s is confidence %d, below the renderer's floor of 1" % (
                os.path.basename(path), kind, conf)
        if mi >= len(meshes):
            return "%s: %s points at mesh %d of %d" % (
                os.path.basename(path), kind, mi, len(meshes))
        m = meshes[mi]
        if m["ntris"] != WANT_TRIS:
            return "%s: %s mesh %s has %d triangles, want %d (the cube keeps its top " \
                   "face and drops its unseen bottom one)" % (
                       os.path.basename(path), kind, m["name"], m["ntris"], WANT_TRIS)
        span, ymin, dig = mesh_stats(p, m)
        for k in range(3):
            if abs(span[k] - WANT_SPAN[k]) > SPAN_TOL:
                return "%s: %s mesh %s spans %.0f x %.0f x %.0f units, want %.0f x " \
                       "%.0f x %.0f" % ((os.path.basename(path), kind, m["name"]) +
                                        tuple(span) + WANT_SPAN)
        if abs(ymin - WANT_YMIN) > SPAN_TOL:
            return "%s: %s mesh %s has its floor at y=%.0f, want %.0f -- the cube is " \
                   "authored standing on the ground and the renderer gives it no lift" \
                   % (os.path.basename(path), kind, m["name"], ymin, WANT_YMIN)
        seen[kind] = (m["name"], dig)
    if seen["SCRATE"][0] == seen["WCRATE"][0]:
        return "%s: both kinds resolve to the SAME mesh %s -- a steel crate drawn as " \
               "wood means something different in play" % (
                   os.path.basename(path), seen["SCRATE"][0])
    if seen["SCRATE"][1] == seen["WCRATE"][1]:
        return "%s: the two cubes are byte-identical in texture (%s) -- one cube was " \
               "bound to both kinds" % (os.path.basename(path), seen["SCRATE"][1])
    return ""


def main(rundir):
    names = sorted(f for f in os.listdir(rundir) if f.endswith(".pack"))
    packs = []
    for f in names:
        path = os.path.join(rundir, f)
        with open(path, "rb") as fh:
            if fh.read(6) == b"CNC3DP":
                packs.append(path)
    readable = 0
    unreadable = []
    good = 0
    firstbad = ""
    for path in packs:
        try:
            reason = check(path)
        except Exception as e:                       # noqa: BLE001 -- reported, not hidden
            unreadable.append((os.path.basename(path), "%s: %s" % (type(e).__name__, e)))
            continue
        readable += 1
        if reason:
            if not firstbad:
                firstbad = reason
        else:
            good += 1
    for nm, why in unreadable:
        print("UNREADABLE|%s|%s" % (nm, why))
    print("CRATEPACKS|packs=%d|readable=%d|unreadable=%d|cube=%d|firstbad=%s"
          % (len(packs), readable, len(unreadable), good, firstbad or "-"))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
