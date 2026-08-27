#!/usr/bin/env python3
"""Extract the campaign's cinematics from the 1995 MS-DOS CD's MOVIES.MIX.

WHY THIS EXISTS. The 36-mission campaign asks for 73 distinct .VQA files by name: 72 from
the Intro / Brief / Action / Win / Lose keys in each scenario's own [BASIC] section. As
of `data/dosdata/movies/` holds 8, of which 6 are ones the campaign asks
for, so 66 briefings and cutscenes are missing and every one of them reports

    CAMPAIGN|movie|GDI7|missing

on the real campaign path. The flow does NOT break on a missing movie -- camp_movie
returns 1 for anything but a user quit -- so the campaign is playable end to end without
them; it is simply silent where the briefings belong.

They are not recoverable from the cartridge. The N64 version has NO FMV AT ALL: there is
not one .VQA in the 33 MB ROM. It replaces the movies with a text briefing engine
(`../src/briefing_engine.c`, ROM 0x1B6820, a 25-verb table at ROM 0x1DE758 driving 39
plain-text .BRF scripts). The PC presentation wants the PC movies, and those live only
on the original 1995 CDs, which are not redistributed with this repository.

WHAT TO RUN, once the CD or its ISO is mounted:

    python3 tools/extract_movies.py /Volumes/<CD>/MOVIES.MIX data/dosdata/movies

Both C&C discs carry a MOVIES.MIX and the two are NOT the same file -- disc 1 has the
GDI set, disc 2 the Nod set. Run it once per disc into the same output directory; the
second run fills in what the first could not find. Pass --dry-run to see what a disc
holds without writing anything.

Westwood MIX archives store a HASH of each filename, never the name, so extraction is
by computed id: this asks the archive for the exact 72 names the campaign needs and
reports, by name, the ones that disc did not have.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(REPO, "menu", "tools"))
import mixlib  # noqa: E402


def campaign_scenarios():
    """The 36 the campaign state machine can actually reach.

    Kept in step with game/gate_campaign.sh, which is the other place this list lives.
    """
    return """SCB01EA SCB02EA SCB03EA SCB04EA SCB05EA SCB06EA SCB07EA SCB08EA SCB09EA
    SCB10EA SCB11EA SCB12EA SCB13EA SCG01EA SCG02EA SCG03EA SCG04EA SCG04WA SCG04WB
    SCG05EA SCG05WA SCG05WB SCG06EA SCG07EA SCG08EA SCG08EB SCG09EA SCG10EA SCG10EB
    SCG11EA SCG12EA SCG12EB SCG13EA SCG13EB SCG14EA SCG15EA""".split()


# Movies the ENGINE names directly, which no scenario file mentions. The list that
# used to be derived purely from the .INI files missed these, and the Nod campaign walk
# caught it: every one of Nod's 35 cues played and NOD1PRE alone reported missing,
# because Choose_Side plays the side's opening briefing itself rather than reading it
# out of scenario 1 (app/cnc3d.cpp: camp_movie(win, au, side ? "NOD1PRE" : "GDI1"),
# mirroring 1995's intro.cpp). GDI1 is also SCG01EA's Brief=, so only NOD1PRE was
# actually absent -- but deriving the list from the scenario files alone is the kind of
# completeness bug that hides until someone plays the other faction.
ENGINE_MOVIES = {
    "GDI1": ["app/cnc3d.cpp:Choose_Side (GDI opening briefing)"],
    "NOD1PRE": ["app/cnc3d.cpp:Choose_Side (Nod opening briefing)"],
}


def wanted_movies(missiondir):
    """Every movie name the campaign asks for, and which scenarios ask for it.

    ONLY the [BASIC] section. A scenario's [TRIGGERS] section also carries Win= and
    Lose= words -- "DESTROYED,WIN,0,NONE,NONE,0" -- and a regex that scans the whole
    file happily reports those as missing movies called `DESTROYED,WIN,0,...`.
    """
    want = {}
    for scen in campaign_scenarios():
        path = os.path.join(missiondir, scen + ".INI")
        if not os.path.isfile(path):
            print("  no .INI for %s (looked in %s)" % (scen, missiondir))
            continue
        text = open(path, encoding="latin-1").read()
        m = re.search(r"(?ims)^\[BASIC\](.*?)(?=^\[|\Z)", text)
        if not m:
            continue
        for key in ("Intro", "Brief", "Action", "Win", "Lose"):
            k = re.search(r"(?im)^%s=(.+)$" % key, m.group(1))
            if not k:
                continue
            name = k.group(1).strip().upper()
            # `x` is the scenario files' own "no movie here" marker, and camp_movie in
            # app/cnc3d.cpp treats it as a no-op rather than a missing file.
            if name and name not in ("X", "NONE", "<NONE>"):
                want.setdefault(name, []).append("%s:%s" % (scen, key))
    for name, why in ENGINE_MOVIES.items():
        want.setdefault(name, []).extend(why)
    return want


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry-run" in sys.argv
    if len(args) < 1:
        print(__doc__)
        return 2
    mixpath = args[0]
    outdir = args[1] if len(args) > 1 else os.path.join(REPO, "data", "dosdata", "movies")
    missiondir = os.path.join(REPO, "game", "missions")

    want = wanted_movies(missiondir)
    if not want:
        print("no movies wanted -- is %s populated?" % missiondir)
        return 1

    have = set()
    if os.path.isdir(outdir):
        have = {f.upper()[:-4] for f in os.listdir(outdir) if f.upper().endswith(".VQA")}

    print("campaign wants %d distinct movies; %d of them already in %s"
          % (len(want), len(set(want) & have), outdir))

    if not os.path.isfile(mixpath):
        print("MIX not found: %s" % mixpath)
        print("Mount the C&C CD (or its ISO) and pass the path to its MOVIES.MIX.")
        return 1

    mix = mixlib.Mix(mixpath)
    print("%s: %d records" % (os.path.basename(mixpath), mix.count))

    if not dry:
        os.makedirs(outdir, exist_ok=True)

    got, already, absent = [], [], []
    for name in sorted(want):
        if name in have:
            already.append(name)
            continue
        if not mix.has(name + ".VQA"):
            absent.append(name)
            continue
        data = mix.get(name + ".VQA")
        got.append(name)
        if not dry:
            with open(os.path.join(outdir, name + ".VQA"), "wb") as f:
                f.write(data)

    print("%s %d, already had %d, NOT on this disc %d"
          % ("would extract" if dry else "extracted", len(got), len(already), len(absent)))
    if got:
        print("  new: " + " ".join(got))
    if absent:
        print("  still missing (try the other disc): " + " ".join(absent))
        for name in absent:
            print("      %-10s wanted by %s" % (name, ", ".join(want[name])))
    return 0 if not absent else 3


if __name__ == "__main__":
    raise SystemExit(main())
