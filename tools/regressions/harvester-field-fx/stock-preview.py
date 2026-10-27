#!/usr/bin/env python3
"""Capture an actual, hidden stock-mission animation of Harvester wheel debris.

The preview uses captured game pixels only. The full frames remain as PNGs, with
lossless animated WebP and a cropped GIF for convenient review.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import sys

from PIL import Image

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("stock_probe", Path(__file__).with_name("stock-probe.py"))
stock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stock)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--start-tick", type=int, default=260)
    parser.add_argument("--frames", type=int, default=24)
    parser.add_argument("--step-ticks", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.out.resolve()
    stock.require(runtime.is_dir(), "Missing installed runtime")
    stock.require(not output.is_relative_to(runtime), "Keep captures outside the installation")
    stock.require(not output.exists() or not any(output.iterdir()), "Use an empty preview directory")
    stock.require(args.frames > 1 and args.start_tick >= 0 and args.step_ticks > 0, "Invalid frame range")
    output.mkdir(parents=True, exist_ok=True)
    executable = runtime / "cnc_eyes"
    inputs = ["cnc_eyes", "TiberianDawn.dylib", "SCB13EC.pack", "tib3d_rings.pack",
              "missions/SCB13EC.INI", "missions/SCB13EC.BIN"]
    manifest = {name: stock.digest(runtime / name) for name in inputs}
    settings = stock.settings_inventory(runtime)
    probe = stock.StockProbe(runtime, output, executable, args.timeout)
    report = {"status": "FAIL", "source": "actual captured stock runtime frames",
              "generated_art": False, "visible_window": False, "scenario": "SCB13EC",
              "camera": [40, 55], "distance": 1200, "engine_tick_rate": 15,
              "start_tick": args.start_tick, "step_ticks": args.step_ticks,
              "frame_count": args.frames, "installed_inputs_sha256": manifest,
              "background_process_samples": probe.background, "runs": probe.runs}
    try:
        commands = stock.setup(stock.SCENARIOS["desert"]) + ["dist 1200", f"tick {args.start_tick}"]
        for i in range(args.frames):
            if i:
                commands.append(f"tick {args.step_ticks}")
            commands += stock.marked(f"frame-{i:03d}")
            commands += ["drawpos HARV 1", f"shot @OUT@/frame-{i:03d}.png"]
        folder, log, states = probe.run("frames", stock.SCENARIOS["desert"], commands)
        captures = [folder / f"frame-{i:03d}.png" for i in range(args.frames)]
        frames = [Image.open(path).convert("RGB") for path in captures]
        stock.require(all(frame.size == (1280, 720) for frame in frames), "Unexpected capture dimensions")
        stock.require(any(int(state["active"]) > 0 for state in states.values()), "No active debris was captured")
        duration = round(1000 * args.step_ticks / 15)
        full = output / "harvester-desert-actual.webp"
        frames[0].save(full, save_all=True, append_images=frames[1:], duration=duration,
                       loop=0, lossless=True, method=4)
        # The full originals and full-frame animation are retained; this crop only
        # enlarges the wheel contact region in the conversation preview.
        crop = (280, 240, 920, 600)
        closeups = [frame.crop(crop) for frame in frames]
        gif = output / "harvester-desert-closeup.gif"
        closeups[0].save(gif, save_all=True, append_images=closeups[1:], duration=duration,
                          loop=0, disposal=2, optimize=False)
        closeups[len(closeups) // 2].save(output / "poster.png")
        sheet = Image.new("RGB", (4 * 320, ((len(frames) + 3) // 4) * 180))
        for i, frame in enumerate(closeups):
            sheet.paste(frame.resize((320, 180)), ((i % 4) * 320, (i // 4) * 180))
        sheet.save(output / "contact-sheet.jpg", quality=92)
        with Image.open(full) as animation:
            stock.require(animation.n_frames == args.frames, "Full animation dropped captured frames")
        with Image.open(gif) as animation:
            stock.require(animation.n_frames == args.frames, "GIF dropped captured frames")
        report.update({"status": "PASS", "crop_xyxy": list(crop),
                       "display_duration_ms_per_frame": duration,
                       "full_frame_animation": str(full), "closeup_animation": str(gif),
                       "source_frames": [{"path": str(path), "sha256": stock.digest(path),
                                          "tick": args.start_tick + i * args.step_ticks,
                                          "particles": states[f"frame-{i:03d}"]}
                                         for i, path in enumerate(captures)],
                       "max_active": max(int(state["active"]) for state in states.values()),
                       "frame_states": states})
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        after = stock.settings_inventory(runtime)
        changed_settings = sorted(name for name in set(settings) | set(after)
                                  if settings.get(name) != after.get(name))
        changed_inputs = [name for name, sha in manifest.items()
                          if not (runtime / name).is_file() or stock.digest(runtime / name) != sha]
        report["installed_settings_and_saves_preserved"] = not changed_settings
        report["installed_inputs_preserved"] = not changed_inputs
        report["changed_settings_or_saves"] = changed_settings
        report["changed_installed_inputs"] = changed_inputs
        if changed_settings or changed_inputs:
            report["status"] = "FAIL"
        (output / "preview-manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    stock.require(report["status"] == "PASS", "Installed files changed during preview")
    print("PASS: actual hidden runtime preview " + str(output / "preview-manifest.json"))


if __name__ == "__main__":
    main()
