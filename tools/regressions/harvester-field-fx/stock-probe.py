#!/usr/bin/env python3
"""Probe the installed Harvester effect in unmodified production TD missions.

All config, save, script and screenshot paths are private to --out. --script
creates a hidden window and OPENCNC3D_BACKGROUND keeps the macOS process in
BackgroundOnly mode. No map is generated and no game window is shown or raised.

Run only after the final runtime has been built and delivered, for example:
  python3 stock-probe.py --runtime "$HOME/CNC3D/playable" --out /tmp/harvest-stock
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


SCENARIOS = {
    # G134: ordinal 1 settles on (40,55) by tick 180; early movement is on TIB.
    "desert": {"scenario": "SCB13EC", "pack": "SCB13EC.pack", "camera": (40, 55),
               "biome": "dust", "source": "game/gates.sh G119/G134"},
    # G296 c: the stock test mission harvests at (54,43), then docks near tick 970.
    "temperate": {"scenario": "SCG90EA", "pack": "SCG01EA.pack", "camera": (54, 43),
                  "biome": "grass", "source": "game/gates.sh G296 c"},
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def settings_inventory(runtime):
    return {str(path.relative_to(runtime)): digest(path)
            for path in runtime.rglob("*") if path.is_file()
            and (path.suffix.lower() == ".cfg"
                 or "saves" in path.relative_to(runtime).parts)}


def states(log):
    result, label = {}, None
    for line in log.splitlines():
        if line.startswith("ECHO|STOCKPROBE|"):
            label = line.split("|", 2)[2]
        elif line.startswith("HARVESTFX|") and "statehash=" in line and label:
            require(label not in result, "Duplicate particle-state label: " + label)
            result[label] = dict(re.findall(r"(\w+)=([^|]+)", line))
            label = None
    return result


def marked(label):
    return ["echo STOCKPROBE|" + label, "harvestfx dump"]


def setup(scene):
    x, z = scene["camera"]
    return ["reveal 1", "gfxmode 2", "gfx tib3d 1", "hud 1", "tickalpha 0",
            "gfx show_stats 0", f"cam {x} {z}", "dist 1600"]


class StockProbe:
    def __init__(self, runtime, output, executable, timeout):
        self.runtime, self.output = runtime, output
        self.executable, self.timeout = executable, timeout
        self.background, self.runs = {}, []

    def run(self, name, scene, commands):
        folder = self.output / name
        folder.mkdir()
        (folder / "saves").mkdir()
        preset = self.runtime / "opencnc3d-fx.cfg"
        (folder / "fx.cfg").write_bytes(preset.read_bytes() if preset.is_file() else b"")
        (folder / "visuals.cfg").write_text("")
        commands = [line.replace("@OUT@", str(folder)) for line in commands] + ["quit"]
        script = folder / "probe.txt"
        script.write_text("\n".join(commands) + "\n")
        cmd = [str(self.executable), "--scen", scene["scenario"], "--pack", scene["pack"],
               "--dir", "missions/", "--content", "content/", "--cameos", "cameos.pack",
               "--dospack", "dossidebar.pack", "--dosinf", "dosinfantry.pack",
               "--dylib", "TiberianDawn.dylib", "--seed", "1", "--nosound",
               "--w", "1280", "--h", "720", "--gfx", str(folder / "fx.cfg"),
               "--gfxsave", str(folder / "fx.cfg"), "--visualscfg", str(folder / "visuals.cfg"),
               "--savedir", str(folder / "saves"), "--script", str(script)]
        (folder / "invocation.json").write_text(json.dumps(cmd, indent=2) + "\n")
        env = {**os.environ, "OPENCNC3D_BACKGROUND": "1", "OPENCNC3D_RA3DORE": "off"}
        samples = []
        with (folder / "run.log").open("wb") as stream:
            process = subprocess.Popen(cmd, cwd=self.runtime, env=env,
                                       stdout=stream, stderr=subprocess.STDOUT)
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
        self.background[name] = {"pid": process.pid, "samples": samples,
                                 "background_only": any("BackgroundOnly" in s for s in samples)}
        log = (folder / "run.log").read_text(errors="replace")
        self.runs.append({"name": name, "returncode": returncode,
                          "log": str(folder / "run.log")})
        require(returncode == 0, name + ": process failed\n" + log[-6000:])
        if sys.platform == "darwin":
            require(self.background[name]["background_only"], name + ": BackgroundOnly was not observed")
        require(re.search(r"^SCRIPT\|end .*: \d+ lines, 0 failures$", log, re.M),
                name + ": script did not complete cleanly\n" + log[-6000:])
        for command in commands:
            if command.startswith("shot "):
                require(Path(command[5:]).is_file(), "Missing screenshot: " + command[5:])
        return folder, log, states(log)

    def scenario(self, name, scene, interval, ticks, bench_frames):
        commands = setup(scene)
        for frame in range(interval, ticks + 1, interval):
            commands += [f"tick {interval}", "obj HARV 0", "obj HARV 1",
                         "drawpos HARV 0", "drawpos HARV 1"] + marked(f"tick-{frame}")
        _, log, sampled = self.run(name + "-scan", scene, commands)
        require(sampled, name + ": no particle state was reported")
        active = [(int(label[5:]), state) for label, state in sampled.items()
                  if int(state["active"]) > 0 and int(state["crystal"]) > 0]
        require(active, name + ": stock scenario never produced active crystal particles")
        # Pick a real sampled active frame. No claim about an uncertain emitter count.
        frame, peak = max(active, key=lambda item: (int(item[1]["active"]), -item[0]))
        require(int(peak[scene["biome"]]) > 0, name + ": expected theater debris was absent")
        require(int(peak["snow"]) == 0, name + ": unexpected snow debris")
        if name == "desert":
            require(int(peak["grass"]) == 0, name + ": grass appeared in the desert")
        replay = setup(scene) + [f"tick {interval}"] * (frame // interval)
        replay += marked("selected") + ["obj HARV 0", "obj HARV 1", "drawpos HARV 0",
                                        "drawpos HARV 1", "shot @OUT@/active.png", "frames 12"]
        replay += marked("frozen")
        if bench_frames:
            replay += ["echo STOCKBENCH|on", f"framebench {bench_frames}"]
        replay += ["harvestfx 0", "shot @OUT@/disabled.png"]
        if bench_frames:
            replay += ["frames 12", "echo STOCKBENCH|off", f"framebench {bench_frames}"]
        folder, capture_log, captured = self.run(name + "-active", scene, replay)
        # Compare physics fields before the first replay image is drawn. Optional draw
        # diagnostics are view evidence and need not match a scan that rendered no image.
        physics = ("enabled", "enhanced", "active", "births", "crystal", "dust",
                   "grass", "snow", "statehash")
        require(all(captured["selected"].get(k) == peak.get(k) for k in physics),
                name + ": selected stock frame did not reproduce particle physics")
        require(all(captured["selected"].get(k) == captured["frozen"].get(k) for k in physics),
                name + ": drawing frames advanced particle physics")
        benchmarks = []
        bench_label = None
        for line in capture_log.splitlines():
            if line.startswith("ECHO|STOCKBENCH|"):
                bench_label = line.rsplit("|", 1)[1]
            elif line.startswith("FRAMEBENCH|"):
                benchmarks.append({"effect": bench_label,
                                   **dict(re.findall(r"(\w+)=([^|]+)", line))})
        return {"scenario": scene["scenario"], "pack": scene["pack"],
                "camera": list(scene["camera"]), "timing_reference": scene["source"],
                "unmodified_stock_mission": True, "sample_interval_ticks": interval,
                "sampled_until_tick": ticks, "active_sample_count": len(active),
                "max_active": max(int(s["active"]) for s in sampled.values()),
                "selected_tick": frame, "selected_state": captured["selected"],
                "replay_physics_identical": True, "frozen_draw_physics_identical": True,
                "active_capture": str(folder / "active.png"),
                "disabled_capture": str(folder / "disabled.png"),
                "samples": sampled, "same_frame_benchmarks_ms": benchmarks,
                "shard_source_diagnostics": [line for line in log.splitlines()
                                              if line.startswith("HARVESTFXART|")]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--executable", default="cnc_eyes",
                        help="Installed TD script executable relative to --runtime (default cnc_eyes).")
    parser.add_argument("--ticks", type=int, default=600)
    parser.add_argument("--interval", type=int, default=15)
    parser.add_argument("--bench-frames", type=int, default=60, help="0 skips the same-frame benchmark.")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.out.resolve()
    executable = (runtime / args.executable).resolve()
    require(runtime.is_dir(), "Missing runtime: " + str(runtime))
    require(executable.is_file() and executable.is_relative_to(runtime), "Choose an installed executable")
    require(not output.is_relative_to(runtime), "Keep evidence outside the installation")
    require(args.interval > 0 and args.ticks >= args.interval and args.ticks % args.interval == 0,
            "Ticks must be a positive multiple of the sample interval")
    require(args.bench_frames >= 0, "Benchmark frame count must be nonnegative")
    require(not output.exists() or not any(output.iterdir()), "Use an empty output directory")
    output.mkdir(parents=True, exist_ok=True)
    protected = settings_inventory(runtime)
    inputs = {str(executable.relative_to(runtime)), "TiberianDawn.dylib", "tib3d_rings.pack"}
    for scene in SCENARIOS.values():
        inputs.add(scene["pack"])
        inputs.update("missions/" + scene["scenario"] + ext for ext in (".INI", ".BIN"))
    manifest = {}
    for name in sorted(inputs):
        require((runtime / name).is_file(), "Missing production input: " + name)
        manifest[name] = digest(runtime / name)
    probe = StockProbe(runtime, output, executable, args.timeout)
    report = {"status": "FAIL", "runtime": str(runtime), "executable": str(executable),
              "visible_window": False, "generated_missions": False,
              "production_inputs_sha256": manifest, "results": {}, "runs": probe.runs,
              "background_process_samples": probe.background}
    error = None
    try:
        for name, scene in SCENARIOS.items():
            report["results"][name] = probe.scenario(name, scene, args.interval, args.ticks,
                                                     args.bench_frames)
        report["status"] = "PASS"
    except Exception as exc:
        error = exc
        report["error"] = str(exc)
    finally:
        current = settings_inventory(runtime)
        changed_settings = sorted(name for name in set(protected) | set(current)
                                  if protected.get(name) != current.get(name))
        changed_inputs = [name for name, sha in manifest.items()
                          if not (runtime / name).is_file() or digest(runtime / name) != sha]
        report["installed_settings_and_saves_preserved"] = not changed_settings
        report["production_inputs_preserved"] = not changed_inputs
        report["changed_installed_settings_or_saves"] = changed_settings
        report["changed_production_inputs"] = changed_inputs
        if changed_settings or changed_inputs:
            report["status"] = "FAIL"
        (output / "stock-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    if error:
        raise error
    require(report["status"] == "PASS", "Production input, setting or save was modified")
    print("PASS: stock missions produced deterministic live debris; evidence "
          + str(output / "stock-verification.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
