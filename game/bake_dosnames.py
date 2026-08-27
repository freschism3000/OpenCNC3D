#!/usr/bin/env python3
"""Bake game/dosnames.h -- the human-readable name of every buildable.

WHY THIS EXISTS, AND WHY IT IS NOT AN EXPORT. The sidebar the renderer draws needs to
print "Med. Tank" where the brain hands it "MTNK". The brain KNOWS the readable name --
ObjectTypeClass::Full_Name() -- but it returns a TEXT ID, and resolving an id needs
Text_String(), which reads a table assigned only inside the engine's own start-up path
(init.cpp:289-291). An additive export that called Full_Name would hand back an integer,
and one that called Text_String would hand back NULL for every entry, because the DLL
front end never runs that start-up. This project already found that and wrote it down at
house.cpp:4018-4025. So the join happens HERE, at bake time, against files already in the
tree, and the renderer gets a flat table.

The precedent for translating an ident renderer-side rather than forking the brain is
sb_art_name in game/cnc_sidebar.h, which maps SW_Ion/SW_Nuke/SW_AirStrike onto ION/ATOM/
BOMB at the one place the lookup is handed a name.

THE JOIN, three sources, all already in the repo:
  1. brain/vanilla/tiberiandawn/conquer.h        TXT_* -> string index
  2. bdata.cpp / idata.cpp / udata.cpp / aadata.cpp
                                                 INI name -> TXT_ constant, read off the
                                                 type constructors, where the two sit on
                                                 adjacent lines
  3. data/dosdata/LOCAL.MIX -> CONQUER.ENG       index -> the actual 1995 English string

Every name in the output is the game's own, read out of the shipped archive rather than
typed from memory -- the same rule game/dosopt.h and menu/dosmenu.h already state about
their labels.

CROSS-CHECK: conquer.h's defines carry the English name in a trailing comment. That is a
convenience, not the source, but it is a free second opinion -- this script compares the
two and reports any disagreement rather than silently preferring one.

    python3 game/bake_dosnames.py [--lang ENG|FRE|GER]
"""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BRAIN = os.path.join(REPO, "brain", "vanilla", "tiberiandawn")
OUT = os.path.join(HERE, "dosnames.h")
sys.path.insert(0, os.path.join(REPO, "menu", "tools"))

DATA_FILES = ["bdata.cpp", "idata.cpp", "udata.cpp", "aadata.cpp"]

# The chrome the sidebar draws that is not a buildable. 1995 had help text for these too,
# behind #ifdef NEVER at sidebar.cpp:902-923, so the STRINGS are the game's and the
# DECISION to show them is ours.
CHROME = [
    # TXT_REPAIR ("Repair Structure") rather than TXT_REPAIR_BUTTON ("Repair"): the
    # tooltip says what the control DOES, and the button already says "Repair" on its
    # own face. Same reasoning for sell.
    ("SB_REPAIR",   "TXT_REPAIR"),
    ("SB_SELL",     "TXT_SELL"),
    ("SB_DEMOLISH", "TXT_DEMOLISH"),     # Nod's word for the same control
    ("SB_MAP",      "TXT_MAP"),
    ("SB_OPTIONS",  "TXT_OPTIONS"),
]


def txt_table():
    """TXT_NAME -> (index, the trailing comment)."""
    out = {}
    pat = re.compile(r"^#define\s+(TXT_\w+)\s+(\d+)\s*(?://\s*(.*))?$")
    for ln in open(os.path.join(BRAIN, "conquer.h"), encoding="latin-1"):
        m = pat.match(ln.rstrip("\n"))
        if m:
            out[m.group(1)] = (int(m.group(2)), (m.group(3) or "").strip())
    return out


def ini_to_txt():
    """INI name -> TXT_ constant, off the type constructors.

    The constructors put the text id and the INI name on adjacent lines with a comment
    between, so this reads the whole file and pairs each TXT_ with the next quoted string
    that looks like an INI ident. Restricted to the four type files so nothing else in the
    tree can feed it.
    """
    out = {}
    for f in DATA_FILES:
        p = os.path.join(BRAIN, f)
        if not os.path.exists(p):
            continue
        src = open(p, encoding="latin-1").read()
        # TXT_FOO, ... "BAR" with only comments/whitespace between the two
        for m in re.finditer(r"\b(TXT_\w+)\b\s*,(?:[^\"\n]*\n)*?[^\"\n]*\"([A-Z0-9]{2,8})\"", src):
            ini = m.group(2)
            if ini not in out:
                out[ini] = m.group(1)
    return out


def strings(lang):
    import mixshp
    mixes = mixshp.MixFile(os.path.join(REPO, "data", "dosdata", "LOCAL.MIX"))
    d = mixes.read("CONQUER.%s" % lang)
    count = struct.unpack_from("<H", d, 0)[0] // 2
    out = []
    for i in range(count):
        off = struct.unpack_from("<H", d, i * 2)[0]
        out.append(d[off:d.index(b"\0", off)].decode("latin-1"))
    return out


def main():
    lang = "ENG"
    a = sys.argv[1:]
    while a:
        x = a.pop(0)
        if x == "--lang" and a:
            lang = a.pop(0).upper()
    txt = txt_table()
    i2t = ini_to_txt()
    strs = strings(lang)
    print("  conquer.h: %d TXT_ ids | type files: %d INI names | CONQUER.%s: %d strings"
          % (len(txt), len(i2t), lang, len(strs)))

    rows, missing, disagree = [], [], []
    for ini in sorted(i2t):
        tname = i2t[ini]
        if tname not in txt:
            missing.append((ini, tname, "no such TXT_ id"))
            continue
        idx, comment = txt[tname]
        if idx >= len(strs):
            missing.append((ini, tname, "index %d past the table" % idx))
            continue
        s = strs[idx]
        if comment and comment != s:
            disagree.append((ini, comment, s))
        rows.append((ini, s, tname))

    chrome = []
    for code, tname in CHROME:
        if tname in txt and txt[tname][0] < len(strs):
            chrome.append((code, strs[txt[tname][0]], tname))
        else:
            missing.append((code, tname, "chrome string absent"))

    if disagree:
        print("  NOTE: %d name(s) where conquer.h's comment and the archive differ; the"
              " ARCHIVE wins:" % len(disagree))
        for ini, c, s in disagree[:6]:
            print("    %-6s comment %-22r archive %r" % (ini, c, s))
    if missing:
        print("  MISSING %d:" % len(missing))
        for m in missing[:8]:
            print("    %s (%s): %s" % m)

    with open(OUT, "w") as f:
        f.write("/* GENERATED by game/bake_dosnames.py -- do not hand-edit.\n"
                "\n"
                "   The readable name of every buildable, for the sidebar's tooltip. Every\n"
                "   string here was read out of the game's own CONQUER.%s inside\n"
                "   data/dosdata/LOCAL.MIX rather than typed from memory, joined to the INI\n"
                "   idents through the TXT_ ids on the type constructors in bdata/idata/\n"
                "   udata/aadata.cpp.\n"
                "\n"
                "   It is baked rather than exported because the brain cannot answer the\n"
                "   question: Full_Name() returns a text id, and Text_String() reads a table\n"
                "   the DLL front end never initialises. See bake_dosnames.py.\n"
                "\n"
                "   %d buildables, %d chrome labels, %d missing.\n"
                "*/\n" % (lang, len(rows), len(chrome), len(missing)))
        f.write("#ifndef DOSNAMES_H\n#define DOSNAMES_H\n\n")
        f.write("struct DosName { const char* asset; const char* title; };\n\n")
        f.write("static const DosName DOSNAMES[] = {\n")
        for ini, s, tname in rows:
            f.write('    { "%s", "%s" },%s/* %s */\n'
                    % (ini, s.replace('"', '\\"'), " " * max(1, 26 - len(ini) - len(s)), tname))
        for code, s, tname in chrome:
            f.write('    { "%s", "%s" },   /* %s, chrome */\n'
                    % (code, s.replace('"', '\\"'), tname))
        f.write("};\n#define DOSNAMES_COUNT ((int)(sizeof(DOSNAMES)/sizeof(DOSNAMES[0])))\n\n")
        f.write("#endif\n")
    print("  wrote %s: %d buildables + %d chrome" % (os.path.relpath(OUT, REPO), len(rows), len(chrome)))


if __name__ == "__main__":
    main()
