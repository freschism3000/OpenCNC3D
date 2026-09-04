#!/usr/bin/env python3
"""Every number the Database screen shows, read out of EA's GPL Tiberian Dawn source.

NOTHING HERE IS TYPED BY HAND. The four type tables (`udata.cpp`, `idata.cpp`,
`aadata.cpp`, `bdata.cpp`) are positional constructor calls, and the parameter NAMES for
each position are in `type.h`'s declaration of that constructor. So the reader:

  1. lifts the constructor signature out of `type.h` -> an ordered list of parameter names
  2. lifts each `static <Family>TypeClass const Var(...)` body out of the data file,
     strips its comments, splits it on TOP-LEVEL commas -> an ordered list of values
  3. zips the two.

That is why it survives the four files not sharing a comment convention: `udata.cpp`
writes `// STRENGTH:`, `bdata.cpp` writes `// STRNTH:` and `aadata.cpp` writes
`// The strength of this unit.` A comment-tag parser reads two of the three and silently
drops the third. Position is the thing the compiler reads, so position is what we read.

An arity mismatch is a hard error, never a shrug: if a signature and a call disagree the
whole family is refused, because a silent off-by-one here would print one unit's armour
next to another unit's speed and look perfectly reasonable.

The derived fields:

  hitpoints    STRENGTH, verbatim.
  damage       Weapons[primary].damage, from `const.cpp`'s own table.
  attack speed Weapons[primary].rof, in game ticks between shots. LOWER IS FASTER, so the
               bar inverts it; the number stays the source's.
  speed        MPHType, resolved through `defines.h` (MPH_MEDIUM = 18, and so on).
  strong/weak  COMPUTED, not authored. Warheads[].modifier[armor] is a 0x100-relative
               multiplier per armour class; a warhead that does 0x40 against steel does
               quarter damage to it. Each entry is scored against the five armour classes,
               and the units wearing the best/worst armour for it are named. See
               `strong_weak()` for the exact rule.
  unlocks      The REVERSE of the `pre` (STRUCTF_ prerequisite) field: everything whose
               prerequisite bit names this structure.

Descriptions are NOT here. See docs/design-database-codex.md section 6: the 1995 manual
text is not in this repository and inventing it would be inventing 1995 copy.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(os.path.join(HERE, "..", ".."))
GPL = os.path.join(ROOT, "brain", "vanilla", "tiberiandawn")


def src(name):
    return open(os.path.join(GPL, name), encoding="latin-1").read()


# ------------------------------------------------------------------ text handling
def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", " ", s)
    return s


# ------------------------------------------------------------------ preprocessor
#
# The type tables are written under conditionals and TWO OF THE FIVE MACROS ARE LIVE.
# Reading the files as flat text hands the parser both branches of every one of them: an
# arity error where the branches differ in argument count, and worse, a silent wrong
# number where they do not.
#
# The verdict is not guessed. `defines.h` is the header every one of these files includes,
# and its own object-like `#define`s are the definition list -- SCENARIO_EDITOR
# (defines.h:93) and PATCH (defines.h:61) are both in it, so those branches COMPILE.
# ADVANCED, NEVER and OBSOLETE appear in no header, no CMakeLists and no build script, so
# theirs do not; they are listed below with that evidence.
#
# A macro in neither list is a HARD ERROR. The next conditional somebody adds to a data
# table cannot ride in on this one's precedent.
NOT_DEFINED = {
    "ADVANCED": "in no header, no CMakeLists and no build script (checked across brain/ and tools/)",
    "NEVER":    "the source's own dead-code marker; defined nowhere",
    "OBSOLETE": "the source's own dead-code marker; defined nowhere",
}


def object_macros(header="defines.h"):
    """Object-like #defines in the header the data tables include. `#define PATCH` with
    no replacement list is a definition; `#define MAGIC_COL_COUNT 12` is a constant and
    is irrelevant here, but harmless to carry."""
    out = set()
    for line in src(header).split("\n"):
        m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)\s*(//.*)?$", line)
        if m:
            out.add(m.group(1))
    return out


DEFINED = object_macros()


def macro_state(name):
    if name in DEFINED:
        return True
    if name in NOT_DEFINED:
        return False
    raise ValueError(
        "#ifdef %s inside a type table: it is not an object-like #define in defines.h "
        "and it is not in NOT_DEFINED. Establish which and record the evidence rather "
        "than letting the reader pick a branch." % name)


def preprocess(text):
    """Keep only the lines the compiler keeps. Nesting is handled; #if is not, because
    none appears in the four data files and a wrong expression verdict is worse than a
    refusal."""
    out, stack = [], []
    for line in text.split("\n"):
        t = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|else|elif|endif|if)\b\s*(\w+)?", t)
        if m:
            kind, macro = m.group(1), m.group(2)
            if kind == "ifdef":
                stack.append([macro_state(macro), macro])
            elif kind == "ifndef":
                stack.append([not macro_state(macro), macro])
            elif kind == "else" and stack:
                stack[-1][0] = not stack[-1][0]
            elif kind == "endif" and stack:
                stack.pop()
            elif kind in ("if", "elif"):
                raise ValueError("#%s inside a type table is not handled: %s" % (kind, t))
            out.append("")
            continue
        out.append(line if all(f[0] for f in stack) else "")
    return "\n".join(out)


def split_top(s):
    """Split on commas that are not inside (), [] or a string literal."""
    out, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c in '"\'':
            q = c
            cur.append(c)
            i += 1
            while i < len(s):
                cur.append(s[i])
                if s[i] == "\\":
                    i += 1
                    if i < len(s):
                        cur.append(s[i])
                elif s[i] == q:
                    break
                i += 1
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


PAIR = {"(": ")", "{": "}", "[": "]"}


def balanced(s, start):
    """Text between the bracket at `start` and its matching close, brace or paren."""
    open_c = s[start]
    close_c = PAIR[open_c]
    depth, i = 0, start
    while i < len(s):
        if s[i] == open_c:
            depth += 1
        elif s[i] == close_c:
            depth -= 1
            if depth == 0:
                return s[start + 1 : i], i
        i += 1
    raise ValueError("unbalanced")


# ------------------------------------------------------------------ enums
def enum_ints(pattern, text):
    return {m.group(1): int(m.group(2)) for m in re.finditer(pattern, text)}


DEFINES = src("defines.h")
MPH = enum_ints(r"\b(MPH_[A-Z_]+)\s*=\s*(\d+)", DEFINES)
ARMOR_ORDER = re.findall(r"\b(ARMOR_[A-Z]+)\b", DEFINES[DEFINES.index("typedef enum ArmorType") :])
ARMOR = []
for a in ARMOR_ORDER:
    if a == "ARMOR_COUNT":
        break
    if a not in ARMOR:
        ARMOR.append(a)
ARMOR_INDEX = {a: i for i, a in enumerate(ARMOR)}
ARMOR_LABEL = {
    "ARMOR_NONE": "None",
    "ARMOR_WOOD": "Wood",
    "ARMOR_ALUMINUM": "Aluminium",
    "ARMOR_STEEL": "Steel",
    "ARMOR_CONCRETE": "Concrete",
}

# STRUCTF_X names the structure STRUCT_X, which is the enum a bdata entry's first
# argument carries. That is the whole of the prerequisite link.
STRUCTF = {
    m.group(1): m.group(2)
    for m in re.finditer(r"#define\s+(STRUCTF_[A-Z0-9_]+)\s+\(1L\s*<<\s*(STRUCT_[A-Z0-9_]+)\)", DEFINES)
}


# ------------------------------------------------------------------ tables in const.cpp
def weapons():
    """WEAPON_x -> dict. const.cpp's rows are self-labelling: each ends `// WEAPON_NAME`."""
    text = src("const.cpp")
    i = text.index("WeaponTypeClass const Weapons[")
    body, _ = balanced(text, text.index("{", i))
    out = {}
    for row in re.finditer(r"\{([^}]*)\}\s*,?\s*//\s*(WEAPON_[A-Z0-9_]+)", body):
        f = split_top(strip_comments(row.group(1)))
        out[row.group(2)] = dict(
            bullet=f[0], damage=int(f[1]), rof=int(f[2]), range=int(f[3], 0), sound=f[4]
        )
    return out


def warheads():
    """WARHEAD_x -> the five armour modifiers, 0x100 == full damage."""
    text = src("const.cpp")
    i = text.index("WarheadTypeClass const Warheads[")
    body, _ = balanced(text, text.index("{", i))
    rows = []
    depth, cur = 0, []
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                cur = []
                continue
        if ch == "}":
            depth -= 1
            if depth == 0:
                rows.append("".join(cur))
                continue
        if depth >= 1:
            cur.append(ch)
    # The trailing `// WARHEAD_x` comment sits AFTER each row's closing brace, so the
    # names are taken in declaration order from the same text.
    names = re.findall(r"//\s*(WARHEAD_[A-Z_]+)", body)
    out = {}
    for row, name in zip(rows, names):
        f = split_top(strip_comments(row))
        mods = [int(x, 0) for x in split_top(f[4].strip().lstrip("{").rstrip("}"))]
        out[name] = dict(spread=int(f[0]), wall=f[1] == "true", wood=f[2] == "true",
                         tiberium=f[3] == "true", mods=mods)
    return out


def bullets():
    """BULLET_x -> WARHEAD_x. bbdata.cpp is positional too, but the warhead is the only
    field wanted and it is the sole WARHEAD_ token in the body, so it is taken by name."""
    text = strip_comments(src("bbdata.cpp"))
    out = {}
    for m in re.finditer(r"BulletTypeClass\s+const\s+\w+\s*\(", text):
        body, _ = balanced(text, m.end() - 1)
        b = re.search(r"\b(BULLET_[A-Z0-9_]+)\b", body)
        w = re.search(r"\b(WARHEAD_[A-Z_]+)\b", body)
        if b and w:
            out[b.group(1)] = w.group(1)
    return out


# ------------------------------------------------------------------ the type tables
TYPE_H = src("type.h")


def signature(family):
    """The ordered parameter NAMES of a family's real constructor.

    A class declares several: a NoInitClass copy shim and the real one. The real one is
    the longest, which is also the only one that can match a data-file call."""
    best = None
    for m in re.finditer(re.escape(family) + r"\s*\(", TYPE_H):
        try:
            body, _ = balanced(TYPE_H, m.end() - 1)
        except ValueError:
            continue
        parts = split_top(strip_comments(body))
        names, required = [], 0
        for p in parts:
            # A DEFAULTED PARAMETER (`bool is_unsellable = false`) must lose its default
            # before the name is taken, or the name comes out as "false" and every call
            # that omits it reads one argument short. That is exactly what happened to
            # all 65 BuildingTypeClass rows on the first pass.
            head = p.split("=")[0]
            n = re.findall(r"[A-Za-z_]\w*", head)
            if n:
                names.append(n[-1])
                if "=" not in p:
                    required += 1
        if best is None or len(names) > len(best[0]):
            best = (names, required)
    return best


def declarations(filename, family):
    text = preprocess(src(filename))
    out = []
    for m in re.finditer(r"\b" + re.escape(family) + r"\s+const\s+(\w+)\s*\(", text):
        body, _ = balanced(text, m.end() - 1)
        out.append((m.group(1), split_top(strip_comments(body))))
    return out


def base_constants(family, filename):
    """Fields a family fixes for EVERY member, in its own constructor's base-class call.

    InfantryTypeClass takes no `armor` and no `is_buildable` parameter. Both are still
    real: its constructor passes ARMOR_NONE and `true` straight into TechnoTypeClass
    (idata.cpp:1504-1536). Read positionally against TechnoTypeClass's own signature,
    exactly as the per-entry tables are read one level up, and only the arguments that
    are literal CONSTANTS are kept -- anything that forwards a parameter varies per
    entry and is already in hand.

    Without this the twenty infantry came out armour-less, which silently emptied every
    strong/weak list they appear in, in both directions."""
    text = strip_comments(src(filename))
    m = re.search(re.escape(family) + r"::" + re.escape(family) + r"\s*\(", text)
    if not m:
        return {}
    _, close = balanced(text, m.end() - 1)
    tail = text[close:]
    b = re.search(r":\s*TechnoTypeClass\s*\(", tail)
    if not b:
        return {}
    body, _ = balanced(tail, b.end() - 1)
    vals = split_top(body)
    found = signature("TechnoTypeClass")
    if not found:
        return {}
    names, _req = found
    if len(vals) != len(names):
        return {}
    out = {}
    for n, v in zip(names, vals):
        v = v.strip()
        if re.fullmatch(r"true|false|-?\d+|[A-Z][A-Z0-9_]*", v):
            out[n] = v
    return out


FAMILIES = [
    ("udata.cpp", "UnitTypeClass", "Vehicles"),
    ("idata.cpp", "InfantryTypeClass", "Infantry"),
    ("aadata.cpp", "AircraftTypeClass", "Aircraft"),
    ("bdata.cpp", "BuildingTypeClass", "Structures"),
]


def txt_strings():
    out = {}
    pat = re.compile(r"^#define\s+(TXT_[A-Z0-9_]+)\s+(\d+)\s*//\s*(.*?)\s*$")
    for line in open(os.path.join(GPL, "conquer.h"), encoding="latin-1"):
        m = pat.match(line)
        if m and m.group(3):
            out[m.group(1)] = m.group(3)
    return out


def build():
    W, WH, BU, TXT = weapons(), warheads(), bullets(), txt_strings()
    entries, problems = [], []

    for filename, family, category in FAMILIES:
        found = signature(family)
        if not found:
            problems.append("%s: no constructor found in type.h" % family)
            continue
        sig, required = found
        base = base_constants(family, filename)
        for var, vals in declarations(filename, family):
            # A call may stop short of the full signature only across DEFAULTED tail
            # parameters. Anything else is an off-by-one and gets refused rather than
            # zipped, because a shifted zip prints one row's armour beside another's speed
            # and looks entirely plausible.
            if not (required <= len(vals) <= len(sig)):
                problems.append(
                    "%s %s: %d args against a %d-parameter signature (%d required); REFUSED"
                    % (family, var, len(vals), len(sig), required)
                )
                continue
            f = dict(base)
            f.update(dict(zip(sig, vals)))
            code = None
            for v in vals:
                m = re.fullmatch(r'"([A-Za-z0-9_]{1,8})"', v.strip())
                if m:
                    code = m.group(1).upper()
                    break
            if not code:
                continue
            tid = next((v for v in vals if v.strip().startswith("TXT_")), None)
            name = TXT.get(tid.strip(), code) if tid else code

            e = dict(code=code, var=var, family=family, category=category, name=name,
                     provenance="%s %s (%s)" % (family, var, filename))

            def num(key):
                v = f.get(key, "")
                v = v.strip()
                return int(v, 0) if re.fullmatch(r"-?(0[xX][0-9a-fA-F]+|\d+)", v) else None

            e["hitpoints"] = num("strength")
            e["cost"] = num("cost")
            e["sight"] = num("sightrange")
            e["level"] = num("level")
            e["enum"] = vals[0].strip()

            armor = f.get("armor", "").strip()
            e["armor"] = armor if armor in ARMOR_INDEX else None

            # SPEED IS TAKEN BY TOKEN, NOT BY PARAMETER NAME. The four families spell the
            # parameter three ways (`maxSpeed` on a unit, `max_speed` on an aircraft,
            # absent on a building), and a name lookup quietly returned None for every
            # aircraft in the game. An MPH_ token is unambiguous: the only other speed-ish
            # enum in these tables is SPEED_ (the locomotion type), a different prefix.
            sp = next((v.strip() for v in vals if v.strip() in MPH), None)
            e["speed_enum"] = sp
            e["speed"] = MPH.get(sp)

            for slot, key in (("primary", "primary"), ("secondary", "secondary")):
                wn = f.get(key, "").strip()
                if wn in W:
                    w = dict(W[wn])
                    w["name"] = wn
                    w["warhead"] = BU.get(w["bullet"])
                    w["range_cells"] = round(w["range"] / 256.0, 2)
                    e[slot] = w
                else:
                    e[slot] = None

            pre = f.get("pre", "").strip()
            e["prereq"] = [STRUCTF[t] for t in re.findall(r"STRUCTF_[A-Z0-9_]+", pre) if t in STRUCTF]

            # InfantryTypeClass CARRIES NO is_buildable. Reading a missing field as
            # `false` filed all twenty infantry as unbuildable, which is both wrong and
            # invisible. Absent means unknown here, and the screen says so.
            e["buildable"] = ("true" in f["is_buildable"].lower()) if "is_buildable" in f else None

            # WHICH SIDE CAN FIELD IT, from the ownable bit field the entry already
            # carries. HOUSEF_GOOD is GDI and HOUSEF_BAD is Nod (defines.h); an entry
            # with neither is multiplayer-only or neutral and the screen shows it to
            # both sides rather than hiding it from both.
            own = f.get("ownable", "")
            e["houses"] = [h for tok, h in (("HOUSEF_GOOD", "GDI"), ("HOUSEF_BAD", "NOD"))
                           if tok in own]

            e["locomotion"] = next((v.strip() for v in vals
                                    if re.fullmatch(r"SPEED_[A-Z]+", v.strip())), None)
            e["power"] = num("power")
            e["drain"] = num("drain")
            e["capacity"] = num("capacity")
            e["is_factory"] = "true" in (f.get("is_factory", "") or "").lower()
            e["is_civilian"] = "true" in (f.get("is_civilian", "") or "").lower()
            entries.append(e)

    by_enum = {e["enum"]: e for e in entries}
    for e in entries:
        e["unlocks"] = sorted(
            {o["code"] for o in entries for p in o["prereq"] if p == e["enum"]}
        )
        e["prereq_codes"] = [by_enum[p]["code"] for p in e["prereq"] if p in by_enum]
        # The PLAYER-FACING prerequisite. "needs a NUKE" is the INI ident talking; the
        # screen says "needs a Power Plant", which is the same fact in the game's own
        # display name.
        e["prereq_names"] = [by_enum[p]["name"] for p in e["prereq"] if p in by_enum]
        e["unlock_names"] = sorted({o["name"] for o in entries
                                    for q in o["prereq"] if q == e["enum"]})

    for e in entries:
        e["strong_against"], e["weak_against"] = strong_weak(e, entries, WH)
        e["brief"] = brief(e)

    # THE BAR MAXIMA, computed across the whole table rather than picked. A bar is only
    # readable if two entries can be compared by eye, and that needs one scale per stat
    # for the whole game. Structures are excluded from the SPEED maximum for the obvious
    # reason and from nothing else.
    def mx(key, pick):
        vals = [pick(e) for e in entries if pick(e) is not None]
        return max(vals) if vals else 1

    maxima = dict(
        hitpoints=mx("hp", lambda e: e["hitpoints"]),
        damage=mx("dmg", lambda e: e["primary"]["damage"] if e["primary"] else None),
        speed=mx("spd", lambda e: e["speed"]),
        # ATTACK SPEED IS INVERTED BY THE BAR, NOT BY THE NUMBER. rof is ticks BETWEEN
        # shots, so small is fast; the bar shows (max - rof) and the printed figure stays
        # the source's own tick count. Storing the maximum lets the screen do that without
        # a second opinion about which way round the stat runs.
        rate=mx("rof", lambda e: e["primary"]["rof"] if e["primary"] else None),
        cost=mx("cost", lambda e: e["cost"]),
    )

    return dict(maxima=maxima, weapons=W, warheads=WH, bullets=BU, armor=ARMOR,
                armor_label=ARMOR_LABEL, mph=MPH, entries=entries, problems=problems)


def effective(attacker, target, WH):
    """Damage one entry's PRIMARY weapon actually lands on another, in the engine's own
    arithmetic: base damage scaled by the warhead's modifier for the target's armour
    class, where 0x100 is full damage. This is `Modify_Damage` in combat.cpp reduced to
    the part that varies per pairing."""
    w = attacker.get("primary")
    if not w or w.get("warhead") not in WH:
        return None
    ai = ARMOR_INDEX.get(target.get("armor"))
    if ai is None:
        return None
    mods = WH[w["warhead"]]["mods"]
    if ai >= len(mods):
        return None
    return w["damage"] * mods[ai] / 256.0, mods[ai]


LOCO = {"SPEED_TRACK": "Tracked", "SPEED_WHEEL": "Wheeled", "SPEED_HOVER": "Hover",
        "SPEED_WINGED": "Winged", "SPEED_FLOAT": "Naval", "SPEED_FOOT": "Foot"}
WEAPON_WORD = {
    "WEAPON_RIFLE": "sniper rifle", "WEAPON_CHAIN_GUN": "chain gun",
    "WEAPON_PISTOL": "pistol", "WEAPON_M16": "assault rifle",
    "WEAPON_DRAGON": "Dragon missile", "WEAPON_FLAMETHROWER": "flamethrower",
    "WEAPON_FLAME_TONGUE": "flame tongue", "WEAPON_CHEMSPRAY": "chemical spray",
    "WEAPON_GRENADE": "grenades", "WEAPON_75MM": "75mm cannon",
    "WEAPON_105MM": "105mm cannon", "WEAPON_120MM": "120mm cannon",
    "WEAPON_TURRET_GUN": "turret gun", "WEAPON_MAMMOTH_TUSK": "Mammoth Tusk missiles",
    "WEAPON_MLRS": "MLRS rockets", "WEAPON_155MM": "155mm gun",
    "WEAPON_M60MG": "M60 machine gun", "WEAPON_TOMAHAWK": "Tomahawk missiles",
    "WEAPON_TOW_TWO": "TOW missiles", "WEAPON_NAPALM": "napalm",
    "WEAPON_OBELISK_LASER": "obelisk laser", "WEAPON_NIKE": "surface-to-air missiles",
    "WEAPON_HONEST_JOHN": "Honest John rocket",
}


def brief(e):
    """ONE DERIVED LINE, AND IT IS NOT THE MANUAL.

    The 1995 manual's unit copy is not in this repository, and writing a paragraph in its
    voice would be inventing 1995 text -- the one thing this project does not do. So the
    screen gets a factual line assembled out of fields it already holds, and the design
    doc records the manual paragraph as owed. Replacing this with the real copy is a data
    change, not a code change.
    """
    bits = []
    if e["locomotion"] in LOCO:
        bits.append(LOCO[e["locomotion"]])
    if e["category"] == "Structures":
        if e["power"]:
            bits.append("supplies %d power" % e["power"])
        if e["drain"]:
            bits.append("draws %d power" % e["drain"])
        if e["capacity"]:
            bits.append("stores %d credits of Tiberium" % e["capacity"])
        if e["is_factory"]:
            bits.append("production facility")
    p = e.get("primary")
    if p:
        bits.append("armed with the %s" % WEAPON_WORD.get(p["name"], p["name"]))
        bits.append("range %.1f cells" % p["range_cells"])
    else:
        bits.append("unarmed")
    if e.get("armor"):
        # ARMOR_NONE is the class infantry wear, and "none armour" is not a sentence.
        bits.append("unarmoured" if e["armor"] == "ARMOR_NONE"
                    else "%s armour" % ARMOR_LABEL[e["armor"]].lower())
    if e.get("prereq_names"):
        bits.append("needs a %s" % ", ".join(e["prereq_names"]))
    return "; ".join(bits).capitalize() + "."


def strong_weak(e, entries, WH):
    """WHAT THIS IS, AND WHAT IT IS NOT.

    C&C has no "strong against" field, and no shipped list of matchups. What it has is a
    warhead-versus-armour table, and every claim this screen makes is that table read out
    loud. Two readings, both deterministic, so nothing here is an editorial pick:

      STRONG  the opponents MY primary weapon lands the most damage on
      WEAK    the opponents whose primary weapon lands the most damage on ME

    Both rank by effective damage (base x armour modifier), take the leaders, and break
    ties on the code so two runs never disagree. The candidate pool is the BUILDABLE
    fighting entries only -- naming a Civilian Hospital as a matchup would be true and
    useless.

    The ARMOUR CLASSES stay in the payload beside the names, because the classes are the
    actual rule and the names are its illustration. If a display ever has to choose, the
    class is the honest half.
    """
    pool = [o for o in entries
            if o is not e
            and o["category"] in ("Vehicles", "Infantry", "Aircraft", "Structures")
            and o.get("armor") is not None
            and (o.get("buildable") is not False)
            and not o["code"].startswith("V")]

    strong_n, weak_n = [], []
    for o in pool:
        hit = effective(e, o, WH)
        if hit and hit[1] >= 0xC0:
            strong_n.append((hit[0], o["code"], o["name"], o.get("cost")))
        back = effective(o, e, WH)
        if back and back[1] >= 0xC0:
            weak_n.append((back[0], o["code"], o["name"], o.get("cost")))

    def top(rows, n=4):
        # RANK BY ADVANTAGE, THEN BY SIGNIFICANCE. Sorting on effective damage alone put
        # the same four names on every armour-piercing weapon in the game, because within
        # one armour class every pairing scores identically and the tie broke
        # alphabetically -- an "Airstrip / APC / Flame Tank / Gun Turret" list that told
        # the player nothing. Cost is the tiebreak: of the things this weapon beats, the
        # expensive ones are the ones worth naming. Code last, so the order is stable.
        rows.sort(key=lambda r: (-r[0], -(r[3] or 0), r[1]))
        return [{"code": c, "name": nm, "damage": round(d, 1)} for d, c, nm, _k in rows[:n]]

    classes_s, classes_w = [], []
    p = e.get("primary")
    if p and p.get("warhead") in WH:
        mods = WH[p["warhead"]]["mods"]
        classes_s = [ARMOR_LABEL[a] for i, a in enumerate(ARMOR)
                     if i < len(mods) and mods[i] >= 0xC0]
    ai = ARMOR_INDEX.get(e.get("armor"))
    if ai is not None:
        classes_w = sorted({wn.replace("WARHEAD_", "") for wn, wh in WH.items()
                            if ai < len(wh["mods"]) and wh["mods"][ai] >= 0xC0})

    return (dict(units=top(strong_n), armor_classes=classes_s,
                 warhead=p["warhead"] if p else None),
            dict(units=top(weak_n), warheads=classes_w,
                 armor=e.get("armor"), armor_label=ARMOR_LABEL.get(e.get("armor"))))


if __name__ == "__main__":
    d = build()
    out = os.path.join(HERE, "codex.json")
    json.dump(d, open(out, "w"), indent=1)
    ent = d["entries"]
    print("%d entries -> %s" % (len(ent), out))
    for cat in ("Structures", "Infantry", "Vehicles", "Aircraft"):
        n = [e for e in ent if e["category"] == cat]
        print("  %-11s %3d  (%d buildable)" % (cat, len(n), sum(1 for e in n if e["buildable"])))
    if d["problems"]:
        print("\nPROBLEMS (nothing below was emitted):")
        for p in d["problems"]:
            print("  " + p)
        sys.exit(1)
