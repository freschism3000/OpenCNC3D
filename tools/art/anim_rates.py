#!/usr/bin/env python3
"""How fast the game walks a baked animation clip, and which part of it it walks.

ONE TABLE, TWO CONSUMERS. `fbxout.py` needs a frame rate to declare in the FBX so an
artist's timeline plays at the speed the game plays; the model gallery needs the same
number plus the segment boundaries so its viewer can play a clip instead of drawing a
still. Both used to have to read it out of the renderer by eye. It lives here once.

NOTHING HERE IS INVENTED. Every row is a transcription of `structure_anim_frame` in
`game/cnc_eyes.cpp`, which is itself a transcription of the cartridge's own per-StructType
draw arms; the ROM addresses are carried across so the two can be diffed. The reasoning
behind each arm, and the evidence for it, is in `docs/animation-drivers.md`.

THE ARITHMETIC, in one place, because it is the whole file:

    the engine ticks at 15 Hz
    a building's stage counter advances once every `rate / ANIM_VIDEO_VS_TICK` ticks
    one stage advances the clip by `per_stage` baked frames

so a clip is walked at `per_stage * 15 * 2 / rate` baked frames per second, and a segment
of N frames takes N / that seconds. `rate` is the 1995 engine's own BSTATE_IDLE rate out
of bdata.cpp's `_anims[]`; `per_stage` is the cartridge arm's own multiplier.

ANIM_VIDEO_VS_TICK IS A HYPOTHESIS, NOT A DECODE, and it is the one free number in the
file. `rate` counts stages against the 15 Hz GAME tick; the console runs its display at
roughly 30 Hz, so a counter advanced once per VIDEO frame runs at exactly twice ours.
That factor was also arrived at independently by eye on the Barracks flag and confirmed
against the cartridge. The console's animation clock source has never been found. If
somebody reads it out of the ROM, this constant is the thing to delete.

WHERE THERE IS NO CLOCK there is no rate, and this file says so rather than inventing
one. A war factory's clip is indexed by its door position, a SAM's by its launcher
facing, a refinery rig's by the refinery's own stage; two more arms address exactly one
frame and the building stands still. Those cases come back with `fps` set to the pack's
own baked grid rate (one frame per engine tick) and `fps_source` saying it is the grid
rather than the game, so a caller can print the difference instead of hiding it.
"""

ENGINE_HZ = 15.0                 # the sim tick the baked frame grid is on
ANIM_VIDEO_VS_TICK = 2.0         # see the header: hypothesis, not decode
BUILDUP_SECONDS = 5.0            # bdata.cpp:3845, `(5 * TICKS_PER_SECOND) / count`

# per_stage: baked frames the cartridge's arm advances per stage of the counter.
# rate:      engine ticks per stage, from bdata.cpp `_anims[]` BSTATE_IDLE. 0 = never
#            counts, so the arm can only ever address frame 0.
# folded:    the arm runs up the clip and back down it instead of sawtoothing.
# seg1:      first frame of the clip that is NOT the idle animation, or 0 for none.
ARMS = [
    dict(code="FACT", per_stage=1.0,  rate=3,  folded=False, seg1=50,
         cite="RAM 0x8003DFC0", arm="frame = stage",
         tail="the yard BUILDING something, which the engine marks with BSTATE_ACTIVE"),
    dict(code="EYE",  per_stage=1.0,  rate=4,  folded=False, seg1=963,
         cite="RAM 0x8003DDE8", arm="frame = stage",
         tail="the masts and dishes falling over, a destruction sequence with no trigger"),
    dict(code="HQ",   per_stage=1.0,  rate=4,  folded=False, seg1=0,
         cite="RAM 0x8003DDD0", arm="frame = stage", tail=None),
    dict(code="PYLE", per_stage=10.0, rate=3,  folded=False, seg1=0,
         cite="RAM 0x8003E1A4", arm="frame = stage * 10", tail=None),
    dict(code="AFLD", per_stage=2.0,  rate=3,  folded=False, seg1=0,
         cite="RAM 0x8003DF48", arm="frame = stage * 2", tail=None),
    dict(code="HAND", per_stage=10.0, rate=3,  folded=False, seg1=0,
         cite="RAM 0x8003DE00", arm="frame = stage * 10", tail=None),
    dict(code="V19",  per_stage=2.5,  rate=4,  folded=False, seg1=0,
         cite="RAM 0x8003E1C8", arm="frame = stage * 2.5", tail=None),
    dict(code="NUKE", per_stage=1.0,  rate=15, folded=True,  seg1=0,
         cite="RAM 0x8003DF64", arm="frame = stage < 20 ? stage : 39 - stage",
         tail=None),
    dict(code="NUK2", per_stage=1.0,  rate=15, folded=True,  seg1=0,
         cite="RAM 0x8003DF64", arm="frame = stage < 20 ? stage : 39 - stage",
         tail=None),
    # STRUCT_ATOWER has no BSTATE_IDLE row in bdata.cpp `_anims[]` at all, so its idle
    # AnimControlType is whatever the BuildingTypeClass constructor leaves behind:
    # {Start 0, Count 1, Rate 0}. A StageClass with rate 0 never counts and the arm is
    # frame = stage, so frame 0 is the only frame it can address. There is no rate to
    # borrow, which is why this row carries a 0 rather than a default.
    dict(code="ATWR", per_stage=1.0,  rate=0,  folded=False, seg1=963,
         cite="RAM 0x8003DDB8", arm="frame = stage",
         tail="the masts and dishes falling over, a destruction sequence with no trigger"),
]
ARM_BY_CODE = {a["code"]: a for a in ARMS}

# The war factory's door. building.cpp opens it with Open_Door(2, 11): rate 2 engine ticks
# per stage, and door.cpp stores `Stages = stages - 1`, so Door_Stage() runs 0..9. WEAP's
# arm multiplies that by RAM 0x80003E04 = 6.55556, which is exactly 59/9, mapping the ten
# door stages onto clip frames 0..59. Frames 60..100 are a second motion the cartridge's
# own arm never reaches.
DOOR_TICKS_PER_STAGE = 2
DOOR_STAGES = 10
DOOR_FRAMES_PER_STAGE = 59.0 / 9.0

# The SAM site. Mission_Attack writes the stage itself, in DOS code the cartridge kept
# verbatim: Set_Rate(2); Set_Stage(0) to rise, finished at Fetch_Stage() == 15, and
# Set_Stage(48) to lower, finished at >= 63. The arm is frame = stage * 12.5 while it is
# rising or lowering (RAM 0x80003E1C), and a pure function of the launcher's facing while
# it is tracking, which is not a clock at all.
SAM_TICKS_PER_STAGE = 2
SAM_FRAMES_PER_STAGE = 12.5
SAM_RAISE_STAGES = 15
SAM_TRACK_LO, SAM_TRACK_HI = 200.0, 400.0

# The refinery's second model. Its arm is frame = (stage - 11) * 10 while the refinery is
# docking and (29 - stage) * 10 while it is undocking, and the stage is the engine's own:
# bdata.cpp gives STRUCT_REFINERY {BSTATE_ACTIVE, 12, 7, 4}, i.e. seven stages at four
# engine ticks each, with AUX1 and AUX2 continuing at the same rate.
PROC_TICKS_PER_STAGE = 4
PROC_FRAMES_PER_STAGE = 10.0
PROC_DOCK_STAGES = 7

# ANIM_VIDEO_VS_TICK applies to the ARMS and to nothing else. The arms need it because
# their stage counter is this project's substitute for a console counter that never
# moves; the door, the SAM, the refinery rig and the MCV rig all read a counter the 1995
# engine really does advance, so their rates are the engine's own with no factor on top.


def arm_fps(per_stage, rate):
    """Baked frames per second for an arm with an engine rate. See the header."""
    if not rate:
        return None
    return per_stage * ENGINE_HZ * ANIM_VIDEO_VS_TICK / float(rate)


def _seg(name, f0, f1, plays, why):
    return dict(name=name, f0=int(f0), f1=int(f1), plays=bool(plays), why=why)


def driver(code, frames, ticks_per_frame=1):
    """How the game walks `code`'s clip, given its baked frame count.

    Returns a dict a caller can print without knowing any of the above:

      driver      which rule applies: arm / door / sam / mcvrig / procrig / clip
      fps         baked frames per second to declare for this model
      fps_source  what that number is: the arm, the door, the SAM's own stage rate,
                  the buildup span, or the pack's baked grid when the game has no clock
      plays       whether the game ever walks this clip on a clock
      hold        set when the game holds a single frame instead, with the reason
      segments    the clip cut into ranges, each saying whether the game plays it
      period      seconds for one cycle of the playing segment, or None
    """
    frames = int(frames or 0)
    tpf = int(ticks_per_frame or 1) or 1
    grid = ENGINE_HZ / tpf
    out = dict(driver="clip", fps=grid, fps_source="the pack's baked grid",
               plays=True, hold=None, folded=False, per_stage=None, rate=None,
               segments=[_seg("clip", 0, frames, True,
                              "one baked frame per engine tick, the whole clip, looping")],
               cite=None, period=(frames / grid if frames and grid else None),
               note="No decoded per-type arm: the renderer loops the clip at the pack's "
                    "own baked grid rate, one frame per engine tick.")
    if frames <= 1:
        out["plays"] = False
        out["hold"] = "the mesh carries fewer than two baked frames"
        out["period"] = None
        return out

    if code == "WEAP":
        # Door_Stage() runs 0..Stages-1, so the last frame the arm can address is
        # (Stages - 1) * 59/9 = 59, and the exclusive bound is 60.
        last = min(frames, int(round((DOOR_STAGES - 1) * DOOR_FRAMES_PER_STAGE)) + 1)
        segs = [_seg("door", 0, last, True,
                     "door stages 0..9 at 59/9 frames each, played forward as the door "
                     "opens and backward as it closes")]
        if frames > last:
            segs.append(_seg("beyond the door", last, frames, False,
                             "a second motion the cartridge's own arm never reaches"))
        out.update(driver="door",
                   fps=DOOR_FRAMES_PER_STAGE * ENGINE_HZ / DOOR_TICKS_PER_STAGE,
                   fps_source="the door's own stage rate, Open_Door(2, 11)",
                   segments=segs, cite="RAM 0x8003DD68 -> 0x801D3C70",
                   period=(DOOR_STAGES - 1) * DOOR_TICKS_PER_STAGE / ENGINE_HZ,
                   note="Not a loop and not stage driven: the clip is indexed by the "
                        "door's position, so it holds frame 0 while the door is shut.")
        return out

    if code == "SAM":
        track0 = min(frames, int(SAM_TRACK_LO))
        segs = [_seg("rising and lowering", 0, track0, True,
                     "stage 0..15 rising and 48..63 lowering, at 12.5 frames per stage")]
        if frames > track0:
            segs.append(_seg("tracking", track0, min(frames, int(SAM_TRACK_HI) + 1), True,
                             "a pure function of the launcher's facing, not a clock"))
        if frames > int(SAM_TRACK_HI) + 1:
            segs.append(_seg("beyond the arm", int(SAM_TRACK_HI) + 1, frames, False,
                             "no stage or facing the arm can produce reaches here"))
        out.update(driver="sam",
                   fps=SAM_FRAMES_PER_STAGE * ENGINE_HZ / SAM_TICKS_PER_STAGE,
                   fps_source="the SAM's own stage rate, Set_Rate(2) in Mission_Attack",
                   segments=segs, cite="RAM 0x8003DE24",
                   period=SAM_RAISE_STAGES * SAM_TICKS_PER_STAGE / ENGINE_HZ,
                   note="Engine driven throughout: the launcher rises and lowers on a "
                        "stage counter and points where its facing points.")
        return out

    if code == "MCVANIM":
        fps = (frames - 1) / BUILDUP_SECONDS
        out.update(driver="mcvrig", fps=fps,
                   fps_source="the whole clip across the engine's five second buildup",
                   segments=[_seg("deploy", 0, frames, True,
                                  "the whole clip, once, forwards")],
                   cite="RAM 0x8003DC68", period=BUILDUP_SECONDS,
                   note="The cartridge's own formula is frame = stage * 1.5625, which "
                        "walks the whole clip once across the construction state. The "
                        "five second span is the 1995 engine's; the counter is this "
                        "project's, because the brain has no stage counter here.")
        return out

    if code == "PROCANIM":
        out.update(driver="procrig",
                   fps=PROC_FRAMES_PER_STAGE * ENGINE_HZ / PROC_TICKS_PER_STAGE,
                   fps_source="the refinery's own stage rate, four engine ticks a stage",
                   segments=[_seg("docking", 0, frames, True,
                                  "ten frames per refinery stage while a harvester "
                                  "unloads, then back down again as it undocks")],
                   cite="RAM 0x8003DFD8",
                   period=(PROC_DOCK_STAGES * PROC_TICKS_PER_STAGE / ENGINE_HZ),
                   note="Driven by the refinery's own unload stage, not by a free "
                        "counter: the rig moves when a harvester is docked and stands "
                        "still the rest of the time.")
        return out

    arm = ARM_BY_CODE.get(code)
    if not arm:
        return out

    f0, f1 = 0, frames
    segs = []
    if arm["seg1"] > 0 and arm["seg1"] < frames:
        f1 = arm["seg1"]
        segs.append(_seg("idle", 0, f1, True, "the building standing and idling"))
        segs.append(_seg("second animation", f1, frames,
                         code == "FACT", arm["tail"] or ""))
    else:
        segs.append(_seg("idle", 0, frames, True, "the building standing and idling"))
    seg_frames = f1 - f0
    fps = arm_fps(arm["per_stage"], arm["rate"])

    out.update(driver="arm", folded=arm["folded"], per_stage=arm["per_stage"],
               rate=arm["rate"], segments=segs, cite=arm["cite"],
               note="The cartridge's arm is %s, walked at the 1995 engine's own "
                    "BSTATE_IDLE rate for this building." % arm["arm"])

    # AN ARM WITH NO ENGINE RATE HOLDS ITS REST POSE. 0 does not mean "as fast as
    # possible": it is the number Begin_Mode hands Set_Rate, and a StageClass with rate 0
    # never counts, so the arm's stage input is frozen at frame 0 for ever.
    if fps is None:
        out.update(fps=grid, fps_source="the pack's baked grid, because the game has no "
                                        "clock here", plays=False, period=None,
                   hold="this structure type has no BSTATE_IDLE rate in the engine's "
                        "own table, so its stage counter never advances and the arm can "
                        "only ever address frame 0")
        for s in segs:
            s["plays"] = False
        return out

    # AN ARM WHOSE STEP IS LONGER THAN ITS OWN CLIP IS NOT INDEXING THAT CLIP. The Hand
    # of Nod is the case: two baked frames against an arm that steps ten at a time is a
    # modulo, not an animation, and it flips between the two authored poses for ever.
    if arm["per_stage"] >= float(seg_frames):
        out.update(fps=grid, fps_source="the pack's baked grid, because the game has no "
                                        "clock here", plays=False, period=None,
                   hold="one stage of this arm advances %g frames and the segment is "
                        "only %d, so the arm and the clip do not belong to each other "
                        "and the game holds frame 0"
                        % (arm["per_stage"], seg_frames))
        for s in segs:
            s["plays"] = False
        return out

    span = (seg_frames - 1) * 2 if arm["folded"] else seg_frames
    out.update(fps=fps, fps_source="the arm's own multiplier and the engine's idle rate",
               period=span / fps if fps else None)
    return out


def fbx_frame_rate(code, frames, ticks_per_frame=1):
    """The frame rate to declare in an FBX for this model: the rate the game walks its
    clip at, or the pack's baked grid rate where the game has no clock for it."""
    return driver(code, frames, ticks_per_frame)["fps"]


if __name__ == "__main__":
    import sys
    rows = []
    for code, frames in (("FACT", 101), ("EYE", 1003), ("HQ", 49), ("PYLE", 301),
                         ("AFLD", 41), ("HAND", 2), ("V19", 141), ("NUKE", 21),
                         ("NUK2", 21), ("ATWR", 999), ("WEAP", 101), ("SAM", 501),
                         ("MCVANIM", 102), ("PROCANIM", 81), ("HARV", 41)):
        d = driver(code, frames)
        rows.append("%-9s %-8s %8.4f fps  %-9s %s"
                    % (code, d["driver"], d["fps"],
                       ("%.2f s" % d["period"]) if d["period"] else "-",
                       d["hold"] or d["fps_source"]))
    sys.stdout.write("\n".join(rows) + "\n")
