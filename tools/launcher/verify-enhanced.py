#!/usr/bin/env python3
"""Render launcher states through its CPU-only --shot path and verify evidence.

Usage: python3 verify_launcher.py LAUNCHER INSTALL OUTPUT_DIRECTORY
The launcher performs its normal bounded update check; no update, media job,
game, SDL window or OpenGL context is requested. Use a fresh output directory.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zlib


ENHANCED_SIZE = (1016, 636)
CLASSIC_SIZE = (960, 600)
RECT_TAGS = {"LBTN", "LMOD", "LCHK", "LGEAR", "LWINDOW", "LREMOVE"}
LONG_FAILURE = (
    "The media download stopped because the selected mirror could not provide "
    "the requested file. Your existing installation and saved games are safe. "
    "Check the connection and try again, or return to Settings to change the "
    "movies and music selection. This deliberately long diagnostic exercises "
    "wrapping and clipping near the progress controls."
)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def protected_snapshot(install):
    """Read config/save/bundle bytes without following directory symlinks."""
    result = {}
    special = {
        "cnc3d-media.txt", "opencnc3d-media.txt", "cnc3d-install.txt",
        "opencnc3d-install.txt", "BUILD-ID", "BUILD-ID.txt", "CHANGELOG.txt",
    }
    for directory, names, files in os.walk(install, followlinks=False):
        names[:] = [name for name in names if name not in {".git", "performance-trace"}]
        parent = Path(directory)
        for name in files:
            path = parent / name
            relative = path.relative_to(install)
            parts = relative.parts
            protected = (
                path.suffix.lower() in {".cfg", ".ini", ".sav"}
                or name in special
                or any(part.lower() in {"saves", "settings"} for part in parts)
                or any(part.endswith(".app") for part in parts)
            )
            if not protected:
                continue
            if path.is_symlink():
                result[str(relative)] = {"symlink": os.readlink(path)}
            elif path.is_file():
                result[str(relative)] = {"size": path.stat().st_size, "sha256": digest(path)}
    return result


def png_size(path):
    """Validate PNG chunk checksums and return the actual IHDR dimensions."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    position, size, seen_data, ended = 8, None, False, False
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4:position + 8]
        end = position + length + 12
        if end > len(data):
            raise ValueError("truncated PNG chunk")
        payload = data[position + 8:position + 8 + length]
        crc = struct.unpack_from(">I", data, position + 8 + length)[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != crc:
            raise ValueError("PNG checksum mismatch")
        if kind == b"IHDR":
            if size is not None or length != 13:
                raise ValueError("invalid PNG header")
            size = struct.unpack_from(">II", payload)
        if kind == b"IDAT":
            seen_data = True
        if kind == b"IEND":
            ended = True
            break
        position = end
    if not size or not seen_data or not ended:
        raise ValueError("incomplete PNG")
    return size


def records(log):
    rows = []
    for line in log.splitlines():
        tag, *fields = line.split("|")
        if tag not in RECT_TAGS | {"LSTATUS", "LAPPLY"}:
            continue
        record = {"tag": tag}
        for field in fields:
            if "=" in field:
                key, value = field.split("=", 1)
                record[key] = value
        rows.append(record)
    return rows


def visible(record):
    if record["tag"] == "LBTN":
        return int(record.get("hidden", "0")) == 0
    if record["tag"] == "LMOD":
        return int(record.get("drawn", "0")) == 1
    return record["tag"] in {"LCHK", "LGEAR", "LWINDOW", "LREMOVE"}


def verify_records(rows, size, view, choice=None):
    errors = []
    grouped = {tag: [row for row in rows if row["tag"] == tag]
               for tag in RECT_TAGS | {"LSTATUS", "LAPPLY"}}
    indices = [row.get("i", "") for row in grouped["LBTN"]]
    if len(indices) != len(set(indices)) or not {"0", "1", "2", "3"}.issubset(indices):
        errors.append("button indices must be unique and include all bottom-row buttons 0..3")
    extra_buttons = [row for row in grouped["LBTN"] if row.get("i") not in {"0", "1", "2", "3"}]
    if any(not row.get("i", "").isdigit() or int(row["i"]) < 4 for row in extra_buttons):
        errors.append("additional buttons must use indices >=4")
    window_close = grouped["LWINDOW"]
    if len(window_close) != 1 or window_close[0].get("label") != "Close" or not visible(window_close[0]):
        errors.append("own window close must be visible on every screen")
    browser_close = extra_buttons
    if len(browser_close) != int(view == "browser") or any(row.get("label") != "Back" or not visible(row) for row in browser_close):
        errors.append("browser Back must be logged only while visible on the browser screen")
    if view in {"browser", "changelog"}:
        for row in grouped["LBTN"]:
            if row.get("i") in {"0", "2"} and visible(row):
                errors.append("Launch and Editor must be hidden in submenus")
    if len(grouped["LMOD"]) != 4:
        errors.append("expected four module records")
    if {row.get("i") for row in grouped["LMOD"]} != {"0", "1", "2", "3"}:
        errors.append("module indices must be unique and cover 0..3")
    if len(grouped["LSTATUS"]) != 1:
        errors.append("expected exactly one status record")
    form = view in {"settings", "setup", "install"}
    media_checks = [row for row in grouped["LCHK"] if row.get("label") in {"MOVIES", "MUSIC", "DOWNLOAD MOVIES", "DOWNLOAD MUSIC"}]
    language_checks = [row for row in grouped["LCHK"] if row not in media_checks]
    if len(media_checks) != (2 if form else 0) or (language_checks and view != "settings") or len(language_checks) > 2:
        errors.append("checkboxes must be logged only on media form screens")
    if len(grouped["LGEAR"]) != (1 if view == "modules" else 0):
        errors.append("settings gear must be logged only on the module grid")
    if len(grouped["LAPPLY"]) != (1 if form else 0):
        errors.append("apply state must be logged exactly on media form screens")
    for row in grouped["LMOD"]:
        if int(row.get("drawn", "0")) != int(view == "modules"):
            errors.append("module visibility does not match selected view")
    if choice is not None and len(grouped["LAPPLY"]) == 1:
        state = grouped["LAPPLY"][0]
        if (state.get("movies"), state.get("music")) != tuple(map(str, choice)):
            errors.append("synthetic media selection did not reach the apply state")
        if sorted(row.get("checked") for row in media_checks) != sorted(map(str, choice)):
            errors.append("synthetic media selection did not reach checkbox state")

    rectangles = []
    for row in rows:
        if row["tag"] not in RECT_TAGS or not visible(row):
            continue  # Hidden records are metadata, never visible geometry.
        name = row["tag"] + ":" + row.get("i", row.get("label", ""))
        try:
            x, y, width, height = [int(row[field]) for field in ("x", "y", "w", "h")]
        except (KeyError, ValueError):
            errors.append(name + " has an invalid logged rectangle")
            continue
        if width <= 0 or height <= 0 or x < 0 or y < 0 or x + width > size[0] or y + height > size[1]:
            errors.append(name + " extends outside the screenshot")
        rectangles.append({"name": name, "x": x, "y": y, "w": width, "h": height})
    for index, a in enumerate(rectangles):
        for b in rectangles[index + 1:]:
            if max(a["x"], b["x"]) < min(a["x"] + a["w"], b["x"] + b["w"]) and max(a["y"], b["y"]) < min(a["y"] + a["h"], b["y"] + b["h"]):
                errors.append(a["name"] + " overlaps " + b["name"])
    return errors, rectangles


def shot(launcher, install, output, name, view, extra=(), expected=ENHANCED_SIZE, choice=None, timeout=45):
    picture, log_path = output / (name + ".png"), output / (name + ".log")
    command = [str(launcher), "--dir", str(install), "--shot", str(picture),
               "--scale", "3", "--view", view, *extra]
    environment = dict(os.environ, OPENCNC3D_BACKGROUND="1", SDL_MAC_BACKGROUND_APP="1")
    started = time.monotonic()
    errors, log, returncode = [], "", None
    try:
        process = subprocess.run(command, cwd=install, env=environment, capture_output=True,
                                 text=True, errors="replace", timeout=timeout)
        log, returncode = process.stdout + process.stderr, process.returncode
        if returncode:
            errors.append("launcher exited with status " + str(returncode))
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            return value.decode(errors="replace") if isinstance(value, bytes) else (value or "")
        log = decoded(error.stdout) + decoded(error.stderr)
        errors.append("launcher screenshot exceeded timeout")
    log_path.write_text(log)
    size = None
    if picture.is_file():
        try:
            size = png_size(picture)
            if size != expected:
                errors.append("PNG size %s differs from expected %s" % (size, expected))
        except ValueError as error:
            errors.append(str(error))
    else:
        errors.append("launcher did not write screenshot")
    rows = records(log)
    rectangles = []
    if size:
        record_errors, rectangles = verify_records(rows, size, view, choice)
        errors.extend(record_errors)
    result = {"name": name, "view": view, "passed": not errors, "errors": errors,
              "command": command, "returncode": returncode, "elapsed_seconds": round(time.monotonic() - started, 3),
              "size": size, "expected_size": expected, "records": rows, "visible_rectangles": rectangles,
              "png_sha256": digest(picture) if picture.is_file() else None,
              "screenshot": str(picture), "log": str(log_path)}
    print(json.dumps({"case": name, "passed": result["passed"], "errors": errors}), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("launcher", type=Path)
    parser.add_argument("install", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--skip-fallback", action="store_true", help="Skip the private installation without Enhanced art.")
    parser.add_argument("--timeout", type=int, default=45, help="Per-shot timeout, including the normal update check.")
    args = parser.parse_args()
    launcher, install, output = args.launcher.resolve(), args.install.resolve(), args.output.resolve()
    if not launcher.is_file() or not os.access(launcher, os.X_OK):
        parser.error("launcher must be an executable file")
    if not (install / "dosmenu.pack").is_file():
        parser.error("installation must contain dosmenu.pack")
    if output == install or install in output.parents:
        parser.error("output must be outside the installation")
    if output.exists() and any(output.iterdir()):
        parser.error("use an empty or new output directory")
    pending = [str(path) for pattern in ("*update.journal", "*update.done") for path in install.glob(pattern)]
    if pending:
        parser.error("finish the existing update before preview checks: " + ", ".join(pending))
    output.mkdir(parents=True, exist_ok=True)
    before = protected_snapshot(install)
    (output / "protected-before.json").write_text(json.dumps(before, indent=2) + "\n")
    cases = [
        ("modules", "modules", (), None),
        ("changelog", "changelog", (), None),
        ("browser", "browser", (), None),
        ("settings", "settings", (), None),
        ("setup", "setup", (), None),
        ("setup-progress", "setup-progress", ("--media-shot-progress", "3145728/10485760"), None),
        ("setup-failed-long", "setup-progress", ("--media-shot-failed", LONG_FAILURE), None),
        ("setup-movies-only", "setup", ("--media-shot-choice", "movies"), (1, 0)),
        ("settings-none", "settings", ("--media-shot-choice", "none"), (0, 0)),
        ("settings-tick-music", "settings", ("--media-shot-choice", "none", "--media-shot-tick", "music"), (0, 1)),
    ]
    results = []
    expected_case_count = len(cases) + int(not args.skip_fallback)
    try:
        for name, view, extra, choice in cases:
            results.append(shot(launcher, install, output, name, view, extra, choice=choice, timeout=args.timeout))
        if not args.skip_fallback:
            fallback = output / "classic-fallback-install"
            fallback.mkdir()
            shutil.copy2(install / "dosmenu.pack", fallback / "dosmenu.pack")
            game = next((install / name for name in ("opencnc3d", "cnc3d") if (install / name).is_file()), None)
            if game:
                shutil.copy2(game, fallback / "opencnc3d")
            else:
                # A --shot never starts the game; presence avoids the fresh-install screen.
                (fallback / "opencnc3d").write_text("private screenshot-only game placeholder\n")
            (fallback / "opencnc3d-install.txt").write_text("version 0.8.3\ndata_id unknown\n")
            results.append(shot(launcher, fallback, output, "classic-fallback", "modules", expected=CLASSIC_SIZE, timeout=args.timeout))
    finally:
        after = protected_snapshot(install)
        changed = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
        (output / "protected-after.json").write_text(json.dumps(after, indent=2) + "\n")
        verification = {
            "passed": len(results) == expected_case_count and all(case["passed"] for case in results) and not changed,
            "expected_case_count": expected_case_count,
            "launcher": str(launcher), "launcher_sha256": digest(launcher), "installation": str(install),
            "cpu_shot_only": True, "visible_window_requested": False, "scale": 3,
            "network": "launcher normal bounded update check; no update or media installation requested",
            "protected_file_count": len(before), "protected_files_changed": changed, "cases": results,
        }
        (output / "verification.json").write_text(json.dumps(verification, indent=2) + "\n")
    print(json.dumps({"passed": verification["passed"], "cases": len(results),
                      "protected_files_changed": changed, "evidence": str(output)}))
    return 0 if verification["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
