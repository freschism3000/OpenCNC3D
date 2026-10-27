#!/usr/bin/env python3
"""Generate a hidden Windows growth/recovery probe with tick-advancing samples.

Rendering quality is unchanged. Normal construction runs through the sidebar;
diagnostic immunity keeps an AI raid from invalidating the 20-building fixture.
Render throughput and an optional real-loop soak are reported separately.
"""
import argparse
import math
from pathlib import Path


def make_script(result_dir="performance-growth-results", live_minutes=0, samples=90):
    quality = result_dir + "/quality.cfg"
    lines = ["gfxsave " + quality, "gfx show_stats 0", "cheatset invuln 1", "tick 5",
             "look MCV:@me", "dist 3000", "count MCV:@me 1"]

    def measure(label):
        lines.extend(["echo STEP|" + label, "player", "meshperfdump"])
        for _ in range(samples):
            lines.extend(["tick 1", "framebench 1"])
        lines.append("meshperfdump")

    def construct(code, expected, delay=60):
        lines.extend(["build " + code, "ready " + code + " 1500",
                      "startplace " + code, "place auto", "tick " + str(delay),
                      "count " + code + ":@me " + str(expected)])

    measure("MCV")
    lines.extend(["lclickobj MCV:@me", "actclickobj MCV:@me", "tick 200",
                  "count MCV:@me 0", "count FACT:@me 1", "look FACT:@me"])
    measure("yard_1_building")
    construct("NUKE", 1, 150)
    measure("power_2_buildings")
    construct("PROC", 1, 300)
    measure("refinery_3_buildings")
    construct("PYLE", 1, 150)
    measure("barracks_4_buildings")
    # Counts include the yard, refinery and barracks. No failed build is omitted.
    for total in range(5, 21):
        construct("NUKE", total - 3)
        if total in (5, 10, 20):
            lines.extend(["count FACT:@me 1", "count PROC:@me 1", "count PYLE:@me 1"])
            measure(str(total) + "_buildings")
    lines.extend(["reveal 1", "cam 32 32", "dist 4200"])
    measure("revealed")
    if live_minutes:
        lines.extend(["echo STEP|live_soak", "livebench " + f"{live_minutes * 60:.1f}",
                      "count FACT:@me 1", "count NUKE:@me 17", "count PROC:@me 1",
                      "count PYLE:@me 1"])
    lines.extend(["echo STEP|burst", "blowup 1", "tick 15"])
    measure("flight")
    lines.append("tick 300")
    measure("settled")
    lines.append("tick 900")
    measure("recovered")
    lines.extend(["cheatset invuln 0", "gfxload " + quality, "quit"])
    return "\n".join(lines) + "\n"


def make_batch(script):
    result = "performance-growth-results"
    lines = ["@echo off", "setlocal EnableExtensions DisableDelayedExpansion", 'cd /d "%~dp0"',
             'if not exist cnc_eyes.exe (echo Place this probe beside cnc_eyes.exe. & exit /b 2)',
             'if not exist opencnc3d-fx.cfg (echo Missing opencnc3d-fx.cfg. & exit /b 2)',
             'if not exist SCM01EA.pack (echo Missing SCM01EA.pack. & exit /b 2)',
             f'if not exist "{result}" mkdir "{result}"',
             "set OPENCNC3D_BACKGROUND=1", "set OPENCNC3D_FRAMEPROF=1",
             "set OPENCNC3D_GPUPROF=1", "set OPENCNC3D_PERFDETAIL=1",
             f'set "LOG={result}\\growth-performance.txt"',
             '> "%LOG%" echo OpenCNC3D growth probe: tick-advancing render throughput; separate live soak',
             '>> "%LOG%" echo Diagnostic immunity protects the player; normal build times and graphics settings remain active.',
             'powershell -NoProfile -Command "Get-FileHash cnc_eyes.exe,opencnc3d-fx.cfg -Algorithm SHA256 | Format-List" >> "%LOG%" 2>&1',
             f'type nul > "{result}\\script.txt"']
    for line in script.splitlines():
        for char in "^&<>|":
            line = line.replace(char, "^" + char)
        lines.append(f'>> "{result}\\script.txt" echo ' + line)
    lines.extend([
        f'cnc_eyes.exe --scen SCM01EA --pack SCM01EA.pack --skirmish --side gdi --credits 99999 --starts 1,6 --dylib TiberianDawn.dll --dir missions/ --content content/ --nosound --w 1920 --h 1080 --enhanced --gfx opencnc3d-fx.cfg --savedir "{result}\\saves" --script "{result}\\script.txt" >> "%LOG%" 2>&1',
        'set "RESULT=%ERRORLEVEL%"', '>> "%LOG%" echo exit code %RESULT%',
        'if not "%RESULT%"=="0" (echo Probe FAILED. Review %LOG% & exit /b %RESULT%)'])
    if any(line.startswith("livebench ") for line in script.splitlines()):
        # A game-over return is not a completed soak, even when the remaining script
        # commands succeed. Require the real loop to last for the requested interval.
        lines.extend([
            'powershell -NoProfile -Command "$s=Get-Content -Raw $env:LOG; $m=[regex]::Match($s,\'LIVEBENCH\\|requested=([0-9.]+)\\|elapsed=([0-9.]+)\\|ticks=([0-9]+)\\|result=([0-9]+)\'); if(-not $m.Success){exit 3}; $c=[Globalization.CultureInfo]::InvariantCulture; $r=[double]::Parse($m.Groups[1].Value,$c); $e=[double]::Parse($m.Groups[2].Value,$c); if($e+0.1 -lt $r -or [int]$m.Groups[3].Value -le 0 -or [int]$m.Groups[4].Value -ne 0){exit 3}" >> "%LOG%" 2>&1',
            'if errorlevel 1 (echo Probe FAILED: live soak ended early. Review %LOG% & exit /b 3)'])
    lines.extend(['echo Probe finished. Results: %LOG%', 'exit /b %RESULT%'])
    return "\r\n".join(lines) + "\r\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--live-minutes", type=float, default=0,
                        help="Optional real-loop soak after constructing 20 buildings (maximum 30)")
    parser.add_argument("--script-output", type=Path,
                        help="Also write the renderer script for native diagnostic validation")
    args = parser.parse_args()
    if not math.isfinite(args.live_minutes) or not 0 <= args.live_minutes <= 30:
        parser.error("--live-minutes must be between 0 and 30")
    script = make_script(live_minutes=args.live_minutes)
    args.output.write_bytes(make_batch(script).encode("utf-8"))
    if args.script_output:
        args.script_output.write_text(script)
    print(args.output)


if __name__ == "__main__":
    main()
