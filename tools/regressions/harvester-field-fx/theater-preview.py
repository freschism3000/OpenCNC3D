#!/usr/bin/env python3
"""Capture actual hidden Harvester debris loops in each installed theater.

Uses the regression's private flat-field missions and real brain-driven movement,
with the installed theater packs, Harvester and Tiberium assets. These are runtime
captures from verification fixtures, not stock mission footage or generated art.
Full-size original PNGs are retained alongside cropped review animations.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys

from PIL import Image


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("field_test", Path(__file__).with_name("test-harvester-field-fx.py"))
field = importlib.util.module_from_spec(spec)
spec.loader.exec_module(field)


def inventory(runtime):
    return {str(path.relative_to(runtime)): field.digest(path)
            for path in runtime.rglob("*") if path.is_file()
            and (path.suffix.lower() == ".cfg" or "saves" in path.relative_to(runtime).parts)}


def animation(captures, output, theater, duration):
    frames = []
    for path in captures:
        with Image.open(path) as image:
            assert image.size == (1280, 720), image.size
            frames.append(image.convert("RGB"))
    full = output / f"harvester-{theater}-actual.webp"
    frames[0].save(full, save_all=True, append_images=frames[1:], duration=duration,
                   loop=0, lossless=True, method=4)
    crop = (200, 210, 840, 570)
    closeups = [frame.crop(crop) for frame in frames]
    gif = output / f"harvester-{theater}-closeup.gif"
    closeups[0].save(gif, save_all=True, append_images=closeups[1:], duration=duration,
                      loop=0, disposal=2, optimize=False)
    closeups[len(closeups) // 2].save(output / "poster.png")
    sheet = Image.new("RGB", (4 * 320, ((len(frames) + 3) // 4) * 180))
    for i, image in enumerate(closeups):
        sheet.paste(image.resize((320, 180)), ((i % 4) * 320, (i // 4) * 180))
    sheet.save(output / "contact-sheet.jpg", quality=92)
    for path in (full, gif):
        with Image.open(path) as image:
            assert image.n_frames == len(frames), "Animation dropped actual captured frames: " + str(path)
    return {"full_frame_animation": str(full), "closeup_animation": str(gif),
            "crop_xyxy": list(crop), "duration_ms_per_frame": duration}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--theaters", nargs="+", choices=list(field.THEATERS), default=list(field.THEATERS))
    parser.add_argument("--start-tick", type=int, default=65)
    parser.add_argument("--frames", type=int, default=24)
    parser.add_argument("--step-ticks", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.out.resolve()
    assert runtime.is_dir(), "Missing installed runtime"
    assert not output.is_relative_to(runtime), "Keep captures outside the installation"
    assert not output.exists() or not any(output.iterdir()), "Use an empty preview directory"
    assert args.frames > 1 and args.start_tick > 0 and args.step_ticks > 0, "Invalid frame range"
    assert len(args.theaters) == len(set(args.theaters)), "Specify each theater once"
    output.mkdir(parents=True, exist_ok=True)
    inputs = {"cnc_eyes", "TiberianDawn.dylib", "tib3d_rings.pack"}
    inputs.update(field.THEATERS[theater][1] for theater in args.theaters)
    manifest = {name: field.digest(runtime / name) for name in sorted(inputs)}
    settings = inventory(runtime)
    probe = field.Probe(runtime, output, args.timeout)
    report = {"status": "FAIL", "runtime": str(runtime),
              "source": "actual captured installed-runtime pixels in private field fixtures",
              "generated_art": False, "stock_missions": False, "visible_window": False,
              "camera": [21, 34], "distance": 1200, "engine_tick_rate": 15,
              "start_tick_after_drive_order": args.start_tick, "step_ticks": args.step_ticks,
              "frame_count": args.frames, "installed_inputs_sha256": manifest,
              "background_process_samples": probe.background_samples, "results": {}}
    try:
        for theater in args.theaters:
            commands = field.SETUP + [f"lclickobj {field.SUBJECT} 0", "expectsel 1",
                                     "stop", "tick 4", "harvestfx reset",
                                     f"harvestfx drawmask {field.DRAW_ALL}",
                                     "cam 31 34", "order 34 34 a", f"tick {args.start_tick}",
                                     "cam 21 34", "dist 1200"]
            for i in range(args.frames):
                if i:
                    commands.append(f"tick {args.step_ticks}")
                commands += field.marked(f"frame-{i:03d}", ["drawpos HARV 0"])
                commands += [f"shot @OUT@/frame-{i:03d}.png"]
            folder, log, states = probe.run(theater, commands, theater=theater)
            assert len(states) == args.frames, "Missing preview state"
            positions = re.findall(r"^DRAWPOS\|HARV\|0\|ex=([^|]+)\|ez=([^|]+)", log, re.M)
            assert len(set(positions)) > 1, "Preview Harvester did not actually move"
            for kind in field.DRAW_BITS:
                live = max(field.number(state, "live_" + kind) for state in states.values())
                assert (live > 0) == (kind in field.THEATER_PARTICLES[theater]), (theater, kind, live)
            captures = [folder / f"frame-{i:03d}.png" for i in range(args.frames)]
            duration = round(1000 * args.step_ticks / 15)
            review = animation(captures, folder, theater, duration)
            report["results"][theater] = {**review, "pack": field.THEATERS[theater][1],
                "real_brain_movement": True, "frame_states": states,
                "source_frames": [{"path": str(path), "sha256": field.digest(path),
                                   "ticks_after_drive_order": args.start_tick + i * args.step_ticks,
                                   "particles": states[f"frame-{i:03d}"]}
                                  for i, path in enumerate(captures)]}
        report["status"] = "PASS"
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        after = inventory(runtime)
        changed_settings = sorted(name for name in set(settings) | set(after)
                                  if settings.get(name) != after.get(name))
        changed_inputs = [name for name, sha in manifest.items()
                          if not (runtime / name).is_file() or field.digest(runtime / name) != sha]
        report.update({"installed_settings_and_saves_preserved": not changed_settings,
                       "installed_inputs_preserved": not changed_inputs,
                       "changed_settings_or_saves": changed_settings,
                       "changed_installed_inputs": changed_inputs})
        if changed_settings or changed_inputs:
            report["status"] = "FAIL"
        (output / "theater-preview-manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    assert report["status"] == "PASS", "Installed files changed during preview"
    print("PASS: actual hidden theater previews " + str(output / "theater-preview-manifest.json"))


if __name__ == "__main__":
    main()
