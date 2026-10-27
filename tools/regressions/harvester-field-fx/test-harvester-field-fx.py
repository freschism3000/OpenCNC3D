#!/usr/bin/env python3
"""Hidden checks of the installed Harvester Tiberium wheel effect.

Runs real brain-driven Harvesters through private, flat Tiberium fixtures. TD's
--script implies hidden; RA also receives --hidden. Both have
OPENCNC3D_BACKGROUND=1 and no desktop input is used. The
installation is read only: settings, saves, missions and captures are private.
Requires Pillow for matched framebuffer/HUD and shroud-leak checks.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from PIL import Image, ImageChops


THEATERS = {
    "temperate": ("TEMPERATE", "SCG01EA.pack", "grass"),
    "desert": ("DESERT", "SCB01EA.pack", "dust"),
    "winter": ("WINTER", "SCW01EA.pack", "grass"),
    "snow": ("WINTER", "SCS01EA.pack", "snow"),
    "sand": ("DESERT", "SCA01EA.pack", "dust"),
}
DRAW_BITS = {"crystal": 1, "dust": 2, "grass": 4, "snow": 8, "dirt": 16}
DRAW_ALL = sum(DRAW_BITS.values())
THEATER_PARTICLES = {
    "temperate": {"crystal", "dust", "grass", "dirt"},
    "desert": {"crystal", "dust", "dirt"},
    "winter": {"crystal", "dust", "grass", "dirt"},
    "snow": {"crystal", "snow"},
    "sand": {"crystal", "dust", "dirt"},
}
SETUP = ["gfxmode 2", "hud 1", "tickalpha 0", "gfx show_stats 0",
         "gfx ss_scale 1", "gfx bloom_on 0", "gfx cloud_on 0", "gfx fog_fx 0",
         "gfx sand_fx 0", "gfx dfog_on 0", "gfx tibfog_on 0", "gfx rain_fx 0",
         "gfx tib3d 1", "cam 25 34", "dist 2800", "tick 1"]
SUBJECT = "HARV:GoodGuy"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def marked(label, commands):
    return ["echo HARVESTTEST|" + label, *commands, "harvestfx dump"]


def fixture(folder, theater, unit="HARV", field=True, enemy=False):
    """TD clear cells are FF00. Flat HGT avoids inheriting donor-map relief."""
    folder.mkdir()
    brain_theater, pack, _ = THEATERS[theater]
    lines = ["[Basic]", "Player=GoodGuy", "Action=x", "Lose=x", "Win=x",
             "Brief=x", "Intro=x", "BuildLevel=98", "Theme=No theme", "Percent=0",
             "CarryOverCap=-1", "CarryOverMoney=0", "", "[MAP]", "X=3", "Y=3",
             "Width=58", "Height=58", "Theater=" + brain_theater]
    if theater in ("snow", "sand"):
        lines += ["OpenCNC3DTheater=" + theater.upper()]
    for house, allies in [("GoodGuy", "GoodGuy,Neutral"), ("BadGuy", "BadGuy"),
                          ("Neutral", "Neutral,GoodGuy")]:
        lines += ["", "[" + house + "]", "Allies=" + allies,
                  "MaxBuilding=150", "MaxUnit=150", "Credits=0", "Edge=South"]
    lines += ["", "[STRUCTURES]", "", "[UNITS]",
              f"000=GoodGuy,{unit},256,{34 * 64 + 18},64,Guard,None"]
    if enemy:
        lines += [f"001=BadGuy,HARV,256,{34 * 64 + 45},64,Harvest,None"]
    lines += ["", "[INFANTRY]", "", "[AIRCRAFT]", "", "[OVERLAY]"]
    if field:
        lines += [f"{y * 64 + x}=TI1" for y in range(28, 40) for x in range(14, 28)]
    if enemy:
        lines += [f"{y * 64 + x}=TI1" for y in range(28, 40) for x in range(40, 52)]
    lines += ["", "[TERRAIN]", "", "[SMUDGE]", "", "[Triggers]", "",
              "[CellTriggers]", "", "[Teams]", "", "[Waypoints]", "",
              "[Base]", "Count=0", ""]
    (folder / "SCG94EA.INI").write_bytes("\r\n".join(lines).encode("latin-1"))
    (folder / "SCG94EA.BIN").write_bytes(bytes([255, 0]) * 4096)
    (folder / "SCG94EA.HGT").write_bytes(bytes(65 * 65))
    return pack


def dumps(log):
    result = {}
    label = None
    for row in log.splitlines():
        if row.startswith("ECHO|HARVESTTEST|"):
            label = row.split("|", 2)[2]
        elif row.startswith("HARVESTFX|") and "statehash=" in row:
            if label is None:
                continue
            assert label not in result, "Duplicate dump label: " + label
            result[label] = dict(re.findall(r"(\w+)=([^|]+)", row))
            label = None
    return result


def number(state, name):
    assert name in state, "Diagnostic field missing: " + name
    return int(state[name])


def no_births(state):
    assert number(state, "births") == 0, state
    for name in DRAW_BITS:
        assert number(state, name) == 0, state


def image_diff(first, second, box=None):
    with Image.open(first) as a, Image.open(second) as b:
        a, b = a.convert("RGB"), b.convert("RGB")
        assert a.size == b.size == (1280, 720), (a.size, b.size)
        if box:
            a, b = a.crop(box), b.crop(box)
        d = ImageChops.difference(a, b)
        data = d.tobytes()
        return sum(bool(r or g or b) for r, g, b in zip(data[0::3], data[1::3], data[2::3]))


def image_delta(first, second):
    """Record visibility separately from particle births and simulation state."""
    with Image.open(first) as a, Image.open(second) as b:
        a, b = a.convert("RGB"), b.convert("RGB")
        assert a.size == b.size == (1280, 720), (a.size, b.size)
        delta = ImageChops.difference(a, b)
        channels = delta.split()
        maximum = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
        histogram = maximum.histogram()
        bounds = delta.getbbox()
        return {"changed_pixels": sum(histogram[1:]),
                "pixels_delta_at_least_3": sum(histogram[3:]),
                "absolute_channel_delta_sum": sum(value * count for channel in channels
                                                   for value, count in enumerate(channel.histogram())),
                "max_channel_delta": max(value for value, count in enumerate(histogram) if count),
                "bounds_xyxy": list(bounds) if bounds else None}


class Probe:
    def __init__(self, runtime, output, timeout):
        self.runtime, self.output, self.timeout = runtime, output, timeout
        self.results, self.executables = [], {}
        self.background_samples = {}

    def run(self, name, commands, theater="temperate", unit="HARV", field=True,
            enemy=False, shroud=False, ra=False, extra=()):
        folder = self.output / name
        folder.mkdir()
        (folder / "saves").mkdir()
        preset = self.runtime / "opencnc3d-fx.cfg"
        (folder / "fx.cfg").write_bytes(preset.read_bytes() if preset.exists() else b"")
        commands = [line.replace("@OUT@", str(folder)) for line in commands] + ["quit"]
        script = folder / "probe.txt"
        script.write_text("\n".join(commands) + "\n")
        if ra:
            executable = self.runtime / "ra3d/cnc3d"
            cmd = [str(executable), "--module", "ra3d", "--harness", "1", "--rounds", "1",
                   "--no-movies", "--hidden", "--rascen", "SCG01EA", "--shotdir", str(folder)]
        else:
            executable = self.runtime / "cnc_eyes"
            mission = folder / "missions"
            pack = fixture(mission, theater, unit, field, enemy)
            assert (self.runtime / pack).is_file(), "Missing theater pack: " + pack
            cmd = [str(executable), "--scen", "SCG94EA", "--pack", pack,
                   "--dir", str(mission) + "/", "--reskin", "--content", "content/",
                   "--cameos", "cameos.pack", "--dospack", "dossidebar.pack",
                   "--dosinf", "dosinfantry.pack", "--dylib", "TiberianDawn.dylib"]
        assert executable.is_file(), "Missing installed executable: " + str(executable)
        self.executables[str(executable)] = digest(executable)
        cmd += ["--seed", "1", "--nosound", "--w", "1280", "--h", "720",
                "--gfx", str(folder / "fx.cfg"), "--visualscfg", str(folder / "visuals.cfg"),
                "--savedir", str(folder / "saves"), "--script", str(script), *extra]
        if not shroud:
            cmd += ["--noshroud"]
        (folder / "invocation.json").write_text(json.dumps(cmd, indent=2) + "\n")
        env = {**os.environ, "OPENCNC3D_BACKGROUND": "1", "OPENCNC3D_RA3DORE": "off"}
        with (folder / "run.log").open("wb") as stream:
            process = subprocess.Popen(cmd, cwd=self.runtime, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT)
            samples = []
            try:
                if sys.platform == "darwin":
                    for _ in range(12):
                        if process.poll() is not None:
                            break
                        sample = subprocess.run(["/usr/bin/lsappinfo", "info", "-only",
                                                 "ApplicationType", "#" + str(process.pid)],
                                                capture_output=True, text=True, timeout=5)
                        samples.append(sample.stdout + sample.stderr)
                        if "BackgroundOnly" in samples[-1]:
                            break
                        time.sleep(0.25)
                returncode = process.wait(timeout=self.timeout)
            except BaseException:
                process.kill()
                process.wait()
                raise
            (folder / "background-process.txt").write_text("\n".join(samples))
            self.background_samples[name] = {"pid": process.pid, "samples": samples,
                                             "background_only": any("BackgroundOnly" in row for row in samples)}
        log = (folder / "run.log").read_text(errors="replace")
        assert returncode == 0, name + ": process failed\n" + log[-6000:]
        if sys.platform == "darwin":
            assert self.background_samples[name]["background_only"], name + ": BackgroundOnly process state was not observed"
        assert re.search(r"^SCRIPT\|end .*: \d+ lines, 0 failures$", log, re.M), name + ": script failed\n" + log[-6000:]
        assert not re.search(r"FX\|(?:shader|link|rt)\|.*(?:fail|incomplete)", log, re.I), log[-6000:]
        for command in commands:
            if command.startswith("shot "):
                assert Path(command[5:]).is_file(), "Missing capture: " + command
        return folder, log, dumps(log)

    def lifecycle(self, theater, name=None):
        name = name or "td-" + theater
        commands = SETUP + [f"lclickobj {SUBJECT} 0", "expectsel 1", "stop", "tick 4",
                            "harvestfx reset", "drawpos HARV 0"]
        commands += marked("stationary", [f"watch {SUBJECT} 0 45", "drawpos HARV 0"])
        commands += marked("moving", ["cam 31 34", "order 34 34 a", "tick 90",
                                     "cam 25 34", f"obj {SUBJECT} 0",
                                     "shot @OUT@/active.png"])
        commands += marked("frozen-frames", ["frames 12"])
        # Draw masks preserve the same live particles and engine tick. Comparing
        # only-kind/none proves the category renders; all/without-kind proves it
        # remains visible in the normal combined effect rather than just in isolation.
        for kind, bit in DRAW_BITS.items():
            commands += [f"harvestfx drawmask {DRAW_ALL ^ bit}",
                         f"shot @OUT@/without-{kind}.png",
                         f"harvestfx drawmask {bit}",
                         f"shot @OUT@/only-{kind}.png"]
        commands += ["harvestfx drawmask 0", "shot @OUT@/no-particles.png",
                     f"harvestfx drawmask {DRAW_ALL}"]
        commands += marked("draw-masks-restored", [])
        commands += ["harvestfx 0", "shot @OUT@/disabled.png", "harvestfx 1", "stop",
                     "tick 32", "harvestfx reset", "drawpos HARV 0"]
        # Finish the current drive cell, then observe a genuinely stationary interval
        # before the brain can resume automatic Harvest movement.
        commands += marked("stopped", [f"watch {SUBJECT} 0 20", "drawpos HARV 0"])
        commands += ["cam 31 34", "order 34 34 a", f"watch {SUBJECT} 0 700", f"expectcell {SUBJECT} 0 34 34",
                     "stop", "tick 4", "harvestfx reset", "cam 37 34", "order 40 34 a"]
        commands += marked("outside", [f"watch {SUBJECT} 0 100"])
        folder, log, states = self.run(name, commands, theater=theater)
        assert set(states) == {"stationary", "moving", "frozen-frames", "draw-masks-restored", "stopped", "outside"}, states
        pos = re.findall(r"^DRAWPOS\|HARV\|\d+\|ex=([^|]+)\|ez=([^|]+)", log, re.M)
        assert len(pos) == 4 and pos[0] == pos[1] and pos[2] == pos[3], name + ": expected stationary interval actually moved: " + str(pos)
        for label in ("stationary", "stopped", "outside"):
            no_births(states[label])
            assert number(states[label], "active") == 0, states[label]
        moving = states["moving"]
        assert number(moving, "births") > 0 and number(moving, "crystal") > 0, moving
        assert number(moving, "active") > 0, moving
        art = re.findall(r"HARVESTFXART\|shards=(\d+)\|source=current-tib3d-pack\|texture=current-tib3d-atlas", log)
        assert art and set(art) == {"50"}, name + ": did not reuse the current 50 crystal pieces and atlas"
        biome = THEATERS[theater][2]
        assert number(moving, biome) > 0, moving
        for category in ("grass", "snow"):
            if category != biome:
                assert number(moving, category) == 0, moving
        assert moving == states["frozen-frames"], name + ": drawing frames advanced particle simulation"
        assert moving == states["draw-masks-restored"], name + ": draw masks changed particle simulation"
        assert number(moving, "drawmask") == DRAW_ALL, moving
        categories = {}
        expected = THEATER_PARTICLES[theater]
        for kind in DRAW_BITS:
            isolated = image_delta(folder / f"only-{kind}.png", folder / "no-particles.png")
            combined = image_delta(folder / "active.png", folder / f"without-{kind}.png")
            hud_isolated = image_diff(folder / f"only-{kind}.png", folder / "no-particles.png", (1160, 24, 1260, 680))
            hud_combined = image_diff(folder / "active.png", folder / f"without-{kind}.png", (1160, 24, 1260, 680))
            assert hud_isolated == hud_combined == 0, (name, kind, "category reached HUD")
            if kind in expected:
                assert number(moving, "live_" + kind) > 0, (name, kind, "no live category particles")
                assert isolated["pixels_delta_at_least_3"] > 0, (name, kind, "category drew no distinct pixels", isolated)
                assert combined["pixels_delta_at_least_3"] > 0, (name, kind, "category invisible in combined effect", combined)
            else:
                assert number(moving, kind) == number(moving, "live_" + kind) == 0, (name, kind, moving)
                assert isolated["changed_pixels"] == combined["changed_pixels"] == 0, (name, kind, isolated, combined)
            categories[kind] = {"expected": kind in expected, "isolated": isolated,
                                "combined": combined, "hud_changed_pixels": hud_combined}
        changed = image_diff(folder / "active.png", folder / "disabled.png")
        assert changed > 10, name + ": active particles made no measurable framebuffer change"
        hud_changed = image_diff(folder / "active.png", folder / "disabled.png", (1160, 24, 1260, 680))
        assert hud_changed == 0, name + ": particles reached the HUD"
        assert len(re.findall(r"^WATCH\|.*\|cell=", log, re.M)) >= 10, name + ": actual movement not witnessed"
        self.results.append({"name": name, "states": states, "changed_pixels": changed,
                             "hud_changed_pixels": hud_changed, "real_brain_orders": True,
                             "category_pixels": categories, "draw_masks_preserve_physics": True})
        return states

    def isolation(self, name, setup=(), extra=(), unit="HARV", field=True):
        subject = unit + ":GoodGuy"
        commands = SETUP + [f"lclickobj {subject} 0", "expectsel 1"] + list(setup)
        commands += ["harvestfx reset", "order 25 34 a"]
        commands += marked("moving", [f"watch {subject} 0 180"])
        _, log, states = self.run(name, commands, unit=unit, field=field, extra=extra)
        no_births(states["moving"])
        assert number(states["moving"], "active") == 0, states
        assert len(re.findall(r"^WATCH\|.*\|cell=", log, re.M)) >= 4, name + ": subject did not actually move"
        self.results.append({"name": name, "states": states, "real_brain_orders": True})

    def smoke(self, ra=False):
        name = "ra-hud-fog-smoke" if ra else "td-hud-fog-smoke"
        commands = ["gfxmode 2", "hud 1", "gfx show_stats 0", "tick 30", "shroudstat", "expectshroud 1 1",
                    "sbwalk", "shot @OUT@/hud-fog.png"]
        if ra:
            commands += ["harvestfx 0", "shot @OUT@/effect-disabled.png", "harvestfx 1"]
        commands += marked("smoke", [])
        folder, log, states = self.run(name, commands, ra=ra, shroud=True)
        assert re.search(r"SHROUDSTAT\|on=1\|valid=1", log), log[-4000:]
        rows = re.findall(r"SBWALK\|rows=\d+\|barh=\d+\|scale=\d+\|probes=(\d+)\|ok=(\d+)", log)
        if ra:
            # RA now shares the accepted Enhanced hit grid, with its own artwork.
            assert "HUD|new=1" in log and "RAHUD|loaded|approved-v06" in log, "RA Enhanced HUD was not observed"
            assert rows and all(a == b and int(a) > 0 for a, b in rows), "RA Enhanced HUD hit grid failed"
            assert image_diff(folder / "hud-fog.png", folder / "effect-disabled.png", (1040, 80, 1280, 700)) == 0, "Harvester FX changed the RA native sidebar"
            with Image.open(folder / "hud-fog.png") as im:
                pixels = im.convert("RGB").crop((1040, 80, 1280, 700)).tobytes()
            visible = sum(bool(r or g or b) for r, g, b in zip(pixels[0::3], pixels[1::3], pixels[2::3]))
            assert visible > 20000 and len(set(pixels)) > 30, "RA native sidebar did not render"
        else:
            assert rows and all(a == b and int(a) > 0 for a, b in rows), log[-4000:]
        no_births(states["smoke"])
        self.results.append({"name": name, "shroud_valid": True, "hud_hit_grid": rows,
                             "enhanced_sidebar_pixel_check": ra, "states": states})

    def hidden_enemy(self):
        commands = SETUP + ["cam 45 34", "harvestfx reset", "watch HARV:BadGuy 0 180",
                    "shroudstat", "expectshroud 1 1", "shroudat 45 34",
                    "shot @OUT@/effect.png"] + marked("hidden", [])
        commands += ["harvestfx 0", "shot @OUT@/disabled.png"]
        folder, log, states = self.run("td-hidden-enemy", commands, enemy=True, shroud=True)
        assert "SHROUDAT|45,34|HIDDEN" in log, "Enemy fixture was visible; no leak check performed"
        motion = re.search(r"WATCH\|HARV:BadGuy#\d+\|ticks=180\|cells entered=(\d+)", log)
        assert motion and int(motion[1]) >= 2, "Hidden enemy did not actually move through the field"
        changed = image_diff(folder / "effect.png", folder / "disabled.png")
        assert changed == 0, "Hidden enemy particles leaked through shroud"
        self.results.append({"name": "td-hidden-enemy", "hidden_changed_pixels": changed,
                             "real_cells_entered": int(motion[1]), "states": states})

    def motion_only(self):
        commands = SETUP + [f"lclickobj {SUBJECT} 0", "expectsel 1", "order 25 34 a",
                           f"watch {SUBJECT} 0 220", f"expectcell {SUBJECT} 0 25 34",
                           "stop", f"watch {SUBJECT} 0 45", "shot @OUT@/motion.png"]
        self.run("motion-preflight", commands)
        self.results.append({"name": "motion-preflight", "real_brain_orders": True})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--motion-only", action="store_true", help="Validate private fixture and real motion before the new effect is built.")
    parser.add_argument("--skip-ra", action="store_true")
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.out.resolve()
    assert runtime.is_dir(), "Missing installation: " + str(runtime)
    assert not output.is_relative_to(runtime), "Outputs must be outside the installation"
    assert not output.exists() or not any(output.iterdir()), "Use an empty output directory"
    output.mkdir(parents=True, exist_ok=True)
    protected = {str(path.relative_to(runtime)): digest(path) for path in runtime.rglob("*")
                 if path.is_file() and (path.suffix == ".cfg" or "saves" in path.relative_to(runtime).parts)}
    probe = Probe(runtime, output, args.timeout)
    report = {"status": "FAIL", "runtime": str(runtime), "visible_window": False,
              "results": probe.results, "installed_settings_preserved": False}
    error = None
    try:
        if args.motion_only:
            probe.motion_only()
        else:
            first = probe.lifecycle("temperate")
            for theater in ("desert", "winter", "snow", "sand"):
                probe.lifecycle(theater)
            repeated = probe.lifecycle("temperate", "td-temperate-repeat")
            assert first == repeated, "Same seeded real motion produced nondeterministic particle states"
            probe.isolation("td-disabled", setup=["harvestfx 0"])
            probe.isolation("td-classic", setup=["gfxmode 0"])
            probe.isolation("td-tiberium-off", setup=["gfx tib3d 0"])
            probe.isolation("td-no-field", field=False)
            probe.isolation("td-non-harvester", unit="MTNK")
            probe.hidden_enemy()
            probe.smoke()
            if not args.skip_ra:
                probe.smoke(ra=True)
        report["status"] = "PASS"
    except Exception as exc:
        error = exc
        report["error"] = str(exc)
    finally:
        changed = [name for name, sha in protected.items()
                   if not (runtime / name).is_file() or digest(runtime / name) != sha]
        report["installed_settings_preserved"] = not changed
        report["changed_installed_settings"] = changed
        report["executables_sha256"] = probe.executables
        report["background_process_samples"] = probe.background_samples
        if changed:
            report["status"] = "FAIL"
        (output / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    if error:
        raise error
    assert report["installed_settings_preserved"], "Installed settings/saves changed: " + str(changed)
    print("PASS: hidden installed-runtime checks; evidence " + str(output / "verification.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
