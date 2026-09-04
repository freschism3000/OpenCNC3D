#!/usr/bin/env python3
"""G172. THE DESERT THEATER DRAWS DESERT FLORA, and no other theater does.

WHAT THE CARTRIDGE DOES. It ships ONE set of tree types and swaps the MODEL underneath
them when the theater is desert. The draw-command enqueue at RAM 0x8004A420 (resident
ROM 0x04B020) reads the theater word at RAM 0x80097150 and, when it is zero, adds 97 to
any model id in [103,121]:

    0x8004A430  lw    $v0, 0x7150($v0)     the theater word
    0x8004A454  bnez  $v0, ...             not theater 0: leave the id alone
    0x8004A45C  slti  $v0, $s0, 0x67       id < 103: leave it alone
    0x8004A468  slti  $v0, $s0, 0x7A       id < 122
    0x8004A470  addiu $s0, $s0, 0x61       +97, in a BRANCH-LIKELY delay slot

Branch-likely means the add runs only when the branch is taken, so the input range is
exactly [103,121], which is exactly T01..T18. Model slot is id + 10, so the outputs are
slots 210..228 of the primary model table (base ROM 0x9A598, stride 16, scene-graph node
pointer at +0x0C). Read straight off the cartridge those nineteen slots hold FOUR nodes:

    210..213 -> 0x801B46C8   dl_00FE360  tall columnar cactus   12 tris
    214..218 -> 0x801B46E4   dl_00FE660  scrub tree              8 tris
    219..222 -> 0x801B4700   dl_00FE918  small twin cactus       8 tris
    223..228 -> 0x801B471C   dl_00FEBD8  largest scrub tree      8 tris

The family widths are 4, 5, 4 and 6 = nineteen slots for eighteen tree types, and the
one spare is id 111, which no TerrainTypeClass registers. It falls in the five-wide
family, which is the only family that could hold it. That is the enumeration closing on
itself.

WHY THIS IS A BAKER-SIDE FIX. cnc_eyes.cpp mesh_for() resolves a terrain object by
looking its INI NAME up in the pack's type table (g_pack.type.find(o.type)). A pack is
baked for one scenario and therefore for one theater, so binding the name to the right
mesh when the type table is written computes exactly the answer the console computes at
draw time -- with no renderer change, no pack-format change and no per-frame cost. The
alternative, a theater test inside the draw loop, would have to re-derive the theater in
the view layer from a string the pack carries for a different purpose, which is the very
confusion the theater resolver below exists to prevent.

THE THEATER IDENTITY, and why this gate does not compare a string to "DESERT".
There are TWO theater words in this project and they legitimately disagree.
missions/SCA01EA.INI says Theater=DESERT, which is what the Tiberian Dawn brain runs,
and carries CNC3DTheater=SAND, which is what we repaint the ground with -- so its baked
pack's theater field says SAND. A gate keyed on the string "DESERT" therefore misses a
genuine desert mission, and the same hole swallows SNOW, our repaint of WINTER. So the
identity used here is the ENGINE theater, read from the scenario's own INI Theater= key
-- the key the brain itself indexes the TerrainTypeClass per-theater fields by -- and
the pack's look name is then CROSS-CHECKED against it through an exhaustive table.
Either one missing, either one unknown, or the two disagreeing is a FAILURE, never a
skip. Adding a sixth repaint has to be a decision, not a fall-through to "not desert".

WHAT IT REFUSES TO DO. It refuses to pass by not looking. A file that carries the
mission magic and cannot be parsed is UNREAD and unread is a failure, not a skip. Every
mission pack in the run folder must be read, its theater must resolve, and the number of
DESERT packs it checked is compared against a count taken independently off the mission
INIs. Zero packs checked is a failure too.

    python3 g172_desert_flora.py                     # every .pack in ../../playable
    python3 g172_desert_flora.py --dir=some/folder
    python3 g172_desert_flora.py A.pack B.pack ...   # only these (coverage leg off)

Prints one G172| line per pack plus a G172|total| line, and exits non-zero on any
failure.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import packinspect as PI          # noqa: E402

# The four cartridge meshes, and the slot family each covers (see the header).
DESERT_FAMILY = ((210, 213, "dl_00FE360"),
                 (214, 218, "dl_00FE660"),
                 (219, 222, "dl_00FE918"),
                 (223, 228, "dl_00FEBD8"))

# The N64 model id of each tree type, from the 32 TerrainTypeClass registrations
# (ctor RAM 0x801EAC0C, jals ROM 0x18CEE0..0x18DD70): T01..T08 are 103..110, id 111 is
# unused, T09..T18 are 112..121. The same numbers are carried per entry in
# support/unit_models.json as n64_model_index, and the four that matter -- T04=106,
# T08=110, T09=112, T18=121 -- are the four confirmed against the ROM node table above.
TERRAIN_N64_ID = dict(("T%02d" % n, (102 + n) if n <= 8 else (103 + n))
                      for n in range(1, 19))

# The remap, applied: slot = id + 97 + 10.
DESERT_MESH = {}
for _code, _id in TERRAIN_N64_ID.items():
    _slot = _id + 97 + 10
    for _lo, _hi, _mesh in DESERT_FAMILY:
        if _lo <= _slot <= _hi:
            DESERT_MESH[_code] = _mesh
            break
assert len(DESERT_MESH) == 18, DESERT_MESH
# The four desert-legal types, one per family, checked against the arithmetic so a typo
# in the tables above cannot pass silently.
assert (DESERT_MESH["T04"], DESERT_MESH["T08"],
        DESERT_MESH["T09"], DESERT_MESH["T18"]) == \
       ("dl_00FE360", "dl_00FE660", "dl_00FE918", "dl_00FEBD8"), DESERT_MESH
DESERT_MESH_SET = frozenset(DESERT_MESH.values())

# Triangle count and per-triangle draw-mode histogram of each desert mesh, measured off
# a bake of the cartridge's own display lists. The two scrub trees are alpha-keyed
# CUTOUT billboards with a flat shadow patch, like the temperate trees; the two cacti
# have their silhouette cut by GEOMETRY rather than by texture alpha, so their body
# faces are OPAQUE. That is the data: a bake that turns them into cutouts has changed
# the art, and a bake that produced a stub would have the wrong count.
EXPECT_TRIS = {"dl_00FE360": (12, {"opaque": 10, "shadow": 2}),
               "dl_00FE660": (8,  {"cutout": 6,  "shadow": 2}),
               "dl_00FE918": (8,  {"opaque": 6,  "shadow": 2}),
               "dl_00FEBD8": (8,  {"cutout": 6,  "shadow": 2})}

# EXHAUSTIVE on purpose: an unknown look name is an error, never "not desert".
LOOK_THEATER = {"DESERT": "DESERT", "SAND": "DESERT",
                "TEMPERAT": "TEMPERATE", "TEMPERATE": "TEMPERATE",
                "WINTER": "WINTER", "SNOW": "WINTER"}
# The Tiberian Dawn theater enum, which is also the cartridge's: its loader at
# RAM 0x801E5190 selects DESERT.TL4/.TL8 for 0 and TEMPERAT.TL4/.TL8 for 2, and prints
# "Illegal theater type: %d" for anything else.
TD_THEATER = {"DESERT": 0, "JUNGLE": 1, "TEMPERATE": 2, "WINTER": 3}
THEATER_DESERT = 0

MISSION_MAGIC = b"CNC3DPK"


def ini_theater(root, scen):
    """The scenario's ENGINE theater off its own INI under `root`, or None.

    Both places the run folder keeps missions are searched, because the three USER
    packs live in one and the campaign in the other.
    """
    for sub in ("missions", os.path.join("missions", "user_maps"), "."):
        path = os.path.join(root, sub, scen.upper() + ".INI")
        if not os.path.isfile(path):
            continue
        for line in open(path, errors="ignore"):
            t = line.strip().upper()
            if t.startswith("THEATER="):
                return t.split("=", 1)[1].strip()
    return None


def is_mission_pack(path):
    """True for a baked scenario pack, False for an asset pack in its own format.

    The asset packs (cameos, dossidebar, efx, doscrate and the rest) legitimately carry
    no meshes and no theater, and passing over them is correct. A file with the mission
    magic that the reader still cannot open is NOT the same thing and must never be
    counted with them, so the two are separated by the magic and not by whether the read
    happened to work.
    """
    try:
        with open(path, "rb") as fh:
            return fh.read(8).startswith(MISSION_MAGIC)
    except Exception:
        return False


def mode_hist(mesh):
    """Per-triangle draw modes of one baked mesh, as {name: count}."""
    out = {}
    for i in range(mesh["ntris"]):
        rec = struct.unpack_from(PI.TRI_FMT, mesh["tris"], i * PI.TRI_STRIDE)
        out[PI.MODE_NAME[rec[1]]] = out.get(PI.MODE_NAME[rec[1]], 0) + 1
    return out


def check(path, root):
    """-> (verdict, scen, theater_id, [note...]).

    verdict is 'pass', 'fail', 'asset' or 'unread'. There is no 'skip'.
    """
    if not os.path.isfile(path):
        return "unread", None, None, ["is not a file"]
    if not is_mission_pack(path):
        return "asset", None, None, ["not a mission pack (own magic)"]
    try:
        pk = PI.read(path)
    except Exception as e:
        return "unread", None, None, [
            "carries the mission magic but the pack reader cannot open it (%s: %s), so "
            "its flora is UNPROVEN rather than correct" % (type(e).__name__, e)]

    notes = []
    scen = (pk.get("scen") or "").upper()
    look = (pk.get("theater") or "").upper()

    # ---- the theater, from the engine key, cross-checked against the look name -----
    if not scen:
        return "fail", scen, None, ["the pack carries no scenario name, so its theater "
                                    "cannot be resolved from any INI"]
    from_ini = ini_theater(root, scen)
    if from_ini is None:
        return "fail", scen, None, [
            "no INI for %s under %s (missions/ or missions/user_maps/), so its ENGINE "
            "theater cannot be established. Unresolved is not a pass" % (scen, root)]
    if from_ini not in TD_THEATER:
        return "fail", scen, None, [
            "INI Theater=%s is not a Tiberian Dawn theater (%s)"
            % (from_ini, ", ".join(sorted(TD_THEATER)))]
    if look not in LOOK_THEATER:
        return "fail", scen, None, [
            "the pack's theater field is %r, which is not a known look name (%s). Add "
            "it with the ENGINE theater it repaints rather than letting it default to "
            "'not desert'" % (look, ", ".join(sorted(LOOK_THEATER)))]
    if LOOK_THEATER[look] != from_ini:
        return "fail", scen, None, [
            "INI Theater=%s but the pack was baked for look %s, which repaints %s. The "
            "pack and the mission disagree about what world this is"
            % (from_ini, look, LOOK_THEATER[look])]
    tid = TD_THEATER[from_ini]

    meshes = [m["name"] for m in pk["meshes"]]
    bymesh = dict((m["name"], m) for m in pk["meshes"])
    types = dict((n, mi) for n, mi, _c in pk["types"])

    def bound(code):
        mi = types.get(code)
        if mi is None:
            return None
        return meshes[mi] if 0 <= mi < len(meshes) else "(index %d out of range)" % mi

    # ---- leg 1: every tree name draws what its own theater calls for ---------------
    for code in sorted(TERRAIN_N64_ID):
        got = bound(code)
        if got is None:
            notes.append("%s is not in the type table at all" % code)
            continue
        if tid == THEATER_DESERT:
            if got != DESERT_MESH[code]:
                notes.append("%s draws %s, wanted the desert mesh %s"
                             % (code, got, DESERT_MESH[code]))
        else:
            # leg 3, the anti-vacuity half: a non-desert pack that binds a tree to a
            # desert mesh is an unconditional swap, which is the mirror bug.
            if got in DESERT_MESH_SET:
                notes.append("%s draws the DESERT mesh %s in a %s map"
                             % (code, got, from_ini))
            elif got.startswith("(index"):
                notes.append("%s resolves to nothing: %s" % (code, got))

    # ---- leg 2: the four cartridge meshes are really in the pack, as authored ------
    # DESERT PACKS ONLY, and that is deliberate. The baker bakes every unit_models
    # entry regardless of theater, so a freshly baked temperate pack carries these four
    # too -- but they are dead weight there, they assert nothing about what a temperate
    # map draws, and demanding them would permanently redden this gate over an authored
    # map that cannot be re-baked (USER50 is 256x256 and ships no .HGT, so bake5 has no
    # corner heightmap for it). Where the meshes are actually USED they are checked to
    # the triangle. Where they are not used, leg 1's other half already forbids a tree
    # from reaching one.
    for m in (sorted(EXPECT_TRIS) if tid == THEATER_DESERT else ()):
        if m not in bymesh:
            notes.append("mesh %s is absent from this pack" % m)
            continue
        ntri, hist = EXPECT_TRIS[m]
        if bymesh[m]["ntris"] != ntri:
            notes.append("mesh %s has %d triangles, wanted %d"
                         % (m, bymesh[m]["ntris"], ntri))
        got = mode_hist(bymesh[m])
        if got != hist:
            notes.append("mesh %s draw modes %s, wanted %s" % (m, got, hist))

    return ("fail" if notes else "pass"), scen, tid, notes


def expected_desert(root, packs):
    """Scenarios with a pack in this folder whose INI says Theater=DESERT.

    Counted from the INIs, which is a DIFFERENT source to the packs' own theater
    fields, so it can catch a run in which the gate resolved a desert pack as
    temperate and passed it.
    """
    out = set()
    for p in packs:
        scen = os.path.basename(p)
        scen = scen[:-5] if scen.lower().endswith(".pack") else scen
        if ini_theater(root, scen) == "DESERT":
            out.add(scen.upper())
    return out


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    where = None
    for a in argv:
        if a.startswith("--dir"):
            where = a.split("=", 1)[1] if "=" in a else None
    root = where or os.path.normpath(os.path.join(HERE, "..", "..", "playable"))
    coverage = not args
    if not args:
        if not os.path.isdir(root):
            print("G172|FAIL|%s is not a directory" % root)
            return 1
        args = sorted(os.path.join(root, f) for f in os.listdir(root)
                      if f.endswith(".pack"))
        if not args:
            print("G172|FAIL|no .pack files under %s" % root)
            return 1

    npass = nfail = nasset = 0
    unread = []
    desert_checked = set()
    for p in args:
        verdict, scen, tid, notes = check(p, root)
        base = os.path.basename(p)
        if verdict == "pass":
            npass += 1
            if tid == THEATER_DESERT:
                desert_checked.add((scen or base).upper())
            print("G172|pass|%s|%s" % (base, "DESERT" if tid == THEATER_DESERT
                                       else "not-desert"))
        elif verdict == "asset":
            nasset += 1
            print("G172|asset|%s|%s" % (base, "; ".join(notes)))
        elif verdict == "unread":
            unread.append(base)
            print("G172|unread|%s|%s" % (base, "; ".join(notes)))
        else:
            nfail += 1
            print("G172|FAIL|%s|%s" % (base, "; ".join(notes)))

    missed = set()
    if coverage:
        missed = expected_desert(root, args) - desert_checked
    print("G172|total|pass=%d|fail=%d|unread=%d|asset=%d|desert=%d|missed=%d"
          % (npass, nfail, len(unread), nasset, len(desert_checked), len(missed)))

    rc = 0
    if nfail:
        print("G172 desert flora: %d pack(s) bind a tree name to the wrong theater's "
              "mesh, cannot have their theater resolved, or are missing one of the four "
              "cartridge meshes. A pack baked before the desert-flora bind reports "
              "exactly this; re-bake and re-install it." % nfail)
        rc = 1
    if unread:
        print("G172 desert flora: %d file(s) carry the mission magic and could not be "
              "read at all (%s), so their flora is UNPROVEN rather than correct. "
              "Unproven is not proven, so this is a failure and not a skip."
              % (len(unread), ", ".join(unread)))
        rc = 1
    if missed:
        print("G172 desert flora: %d scenario(s) have a pack in this folder and an INI "
              "that says Theater=DESERT, yet were not checked as desert maps (%s). The "
              "theater resolver and the mission INIs disagree, and the gate will not "
              "call that a pass." % (len(missed), ", ".join(sorted(missed))))
        rc = 1
    if npass == 0:
        print("G172 desert flora: not one pack was checked, which is not a pass")
        rc = 1
    elif not desert_checked:
        print("G172 desert flora: %d pack(s) checked and not ONE of them was a desert "
              "map, so the half of this gate that matters never ran. That is not a pass"
              % npass)
        rc = 1
    if rc == 0:
        print("G172 desert flora: %d mission pack(s), %d of them DESERT, bind all "
              "eighteen of T01..T18 to the mesh their own ENGINE theater calls for; "
              "each DESERT pack carries the four cartridge meshes with their authored "
              "triangle counts and draw modes; and no non-desert map draws a cactus"
              % (npass, len(desert_checked)))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
