#!/usr/bin/env python3
"""Write OpenCNC3D-PerfProbe.bat: one file a Windows player drops into an installed
OpenCNC3D folder and double-clicks. It needs no new build: it drives the cnc_eyes.exe
that every Windows package already ships, through the same --script verbs the gates use
(framebench, gfx, gfxsave/gfxload, gfxmode, reveal, dist, blowup), and writes everything
to one text file, OpenCNC3D-perfprobe.txt, for the player to send back.

What it measures, and what it cannot:

  - framebench N draws the frame N times, each finished on the GPU, and prints the median
    and 90th percentile in milliseconds. That is the cost of DRAWING a frame: the scene
    submission on the CPU plus the GPU's work. It does NOT include the buffer swap, so a
    vsync or present stall does not show here; the tail of the player's own
    opencnc3d-log.txt (STATS| lines, written while the FPS counter is on) covers that.
  - Each Enhanced feature is priced by switching it off alone and measuring again, then
    putting the player's own settings back (gfxload of a copy taken at the start).
  - The same scene at 1920x1080 and at 960x540 separates fill rate from per-call cost: a
    frame that costs the same at a quarter of the pixels is not waiting on pixels.

The player's own opencnc3d-fx.cfg is read, never written: the probe saves its working copy
as perfprobe-fx.cfg and deletes it at the end.

The three scripts sit beside this file as plain text so they can be run on any build
machine with the Mac binary first (same verbs, same engine):

    ./cnc_eyes --scen SCG01EA --pack SCG01EA.pack --dylib TiberianDawn.dylib \
        --dir missions/ --content content/ --nosound --w 1920 --h 1080 \
        --enhanced --texset 3 --gfx opencnc3d-fx.cfg --script perfprobe-main.txt

Usage: make-perfprobe.py [--cache-ab | --placed-ab | --mission3] [OUT.bat]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VERSION = "2"

# (script file, mission, width, height, what the player is told, renderer). A renderer
# other than cnc_eyes.exe is optional: its runs are skipped when the file is not in the
# folder, so the same probe serves a plain install and a test build that ships a variant
# (cnc_eyes_sse2.exe: the renderer compiled for SSE2 floating point instead of x87).
RUNS = [
    ("perfprobe-main.txt",   "SCG01EA", 1920, 1080, "GDI mission 1, the shipped renderer", "cnc_eyes.exe"),
    ("perfprobe-main.txt",   "SCG01EA", 1920, 1080, "GDI mission 1, the SSE2 renderer", "cnc_eyes_sse2.exe"),
    ("perfprobe-bigmap.txt", "SCG10EA", 1920, 1080, "a large map and a battle, the shipped renderer", "cnc_eyes.exe"),
    ("perfprobe-bigmap.txt", "SCG10EA", 1920, 1080, "a large map and a battle, the SSE2 renderer", "cnc_eyes_sse2.exe"),
]
# --cache-ab: the same renderer with the ground cache (terrain_cache.h) on and then off,
# for a test build that carries it. A seventh field is an environment setting for that
# run alone.
RUNS_CACHE_AB = [
    ("perfprobe-main.txt",   "SCG01EA", 1920, 1080, "GDI mission 1, ground cache on", "cnc_eyes.exe", ""),
    ("perfprobe-main.txt",   "SCG01EA", 1920, 1080, "GDI mission 1, ground cache off", "cnc_eyes.exe", "OPENCNC3D_TERRAIN_CACHE=0"),
    ("perfprobe-bigmap.txt", "SCG10EA", 1920, 1080, "a large map and a battle, ground cache on", "cnc_eyes.exe", ""),
    ("perfprobe-bigmap.txt", "SCG10EA", 1920, 1080, "a large map and a battle, ground cache off", "cnc_eyes.exe", "OPENCNC3D_TERRAIN_CACHE=0"),
]
# --placed-ab: the heaviest base in the campaign (SCG32EA, 142 structures) with remastered
# buildings' still parts kept in the persistent cache and then rebuilt per frame
# (OPENCNC3D_MESH_PLACED=0), then GDI 1 and the big map for continuity.
RUNS_PLACED_AB = [
    ("perfprobe-main.txt",   "SCG01EA", 1920, 1080, "GDI mission 1", "cnc_eyes.exe", ""),
    ("perfprobe-base.txt",   "SCG32EA", 1920, 1080, "the biggest base, buildings cached", "cnc_eyes.exe", ""),
    ("perfprobe-base.txt",   "SCG32EA", 1920, 1080, "the biggest base, buildings rebuilt per frame", "cnc_eyes.exe", "OPENCNC3D_MESH_PLACED=0"),
    ("perfprobe-bigmap.txt", "SCG10EA", 1920, 1080, "a large map and a battle", "cnc_eyes.exe", ""),
]
# --mission3: Nod mission 3 (SCB03EB) at 1280x720 fresh, and the same mission entered
# straight from mission 2 in one process (remission), the way the campaign plays it, to
# tell a cost that mission 3 has from one carried over from the mission before.
RUNS_MISSION3 = [
    ("perfprobe-m3.txt",   "SCB03EB", 1280, 720, "Nod mission 3, fresh", "cnc_eyes.exe", ""),
    ("perfprobe-m2m3.txt", "SCB02EA", 1280, 720, "Nod mission 2, then mission 3 in the same run", "cnc_eyes.exe", ""),
    ("perfprobe-m3.txt",   "SCB03EB", 1280, 720, "Nod mission 3, fresh, again", "cnc_eyes.exe", ""),
]
if "--mission3" in sys.argv:
    RUNS = RUNS_MISSION3
    sys.argv.remove("--mission3")
if "--placed-ab" in sys.argv:
    RUNS = RUNS_PLACED_AB
    sys.argv.remove("--placed-ab")
if "--cache-ab" in sys.argv:
    RUNS = RUNS_CACHE_AB
    sys.argv.remove("--cache-ab")
RUNS = [r if len(r) == 7 else r + ("",) for r in RUNS]
SCRIPTS = sorted(set(r[0] for r in RUNS))


def bat_escape(line):
    """One script line as the text of a cmd `echo`: the metacharacters cmd would act on
    are escaped with a caret. Parentheses are avoided in the scripts themselves, because
    no echo line here sits inside a parenthesised block."""
    for ch in "^&|<>":
        line = line.replace(ch, "^" + ch)
    return line


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "OpenCNC3D-PerfProbe.bat")
    L = []
    a = L.append
    a("@echo off")
    a("rem OpenCNC3D performance probe v%s. Drop this file into your OpenCNC3D folder" % VERSION)
    a("rem (the one with OpenCNC3D.exe in it) and double-click it. It takes about five")
    a("rem minutes, opens the game window a few times, and writes OpenCNC3D-perfprobe.txt.")
    a("rem Generated by tools/perfprobe/make-perfprobe.py; edit that, not this.")
    a("setlocal")
    a("title OpenCNC3D performance probe")
    a('cd /d "%~dp0"')
    a('if exist "cnc_eyes.exe" goto found')
    a("echo.")
    a("echo This file has to sit in your OpenCNC3D folder, next to OpenCNC3D.exe and")
    a("echo cnc_eyes.exe. Move it there and double-click it again.")
    a("echo.")
    a("pause")
    a("exit /b 1")
    a(":found")
    a('set "LOG=OpenCNC3D-perfprobe.txt"')
    a('set "GFX=--gfx"')
    a('if exist "opencnc3d-fx.cfg" set "GFX=--gfx opencnc3d-fx.cfg"')
    a("echo.")
    a("echo  OpenCNC3D performance probe")
    a("echo  ---------------------------")
    a("echo  The game will open and close by itself a few times, switching its graphics")
    a("echo  around. Please do not click in it or use the PC until this window says Done.")
    a("echo  It takes about five minutes.")
    a("echo.")
    # the scripts, written next to the binary
    for script in SCRIPTS:
        with open(os.path.join(HERE, script)) as f:
            lines = [l.rstrip("\r\n") for l in f if l.strip()]
        a('type nul > "%s"' % script)
        for l in lines:
            a('>> "%s" echo %s' % (script, bat_escape(l)))
    # the header: build, system, settings
    a('> "%%LOG%%" echo ===== OPENCNC3D PERF PROBE v%s =====' % VERSION)
    a('>> "%LOG%" echo date: %DATE% %TIME%')
    a('if exist "BUILD-ID.txt" type "BUILD-ID.txt" >> "%LOG%"')
    a('>> "%LOG%" echo ===== SYSTEM =====')
    a('powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | Format-List Name,DriverVersion,'
      'VideoModeDescription,CurrentRefreshRate; Get-CimInstance Win32_Processor | Format-List Name,NumberOfCores,'
      'NumberOfLogicalProcessors,MaxClockSpeed; Get-CimInstance Win32_OperatingSystem | Format-List Caption,Version,'
      "OSArchitecture; 'RAM GB: {0:N1}' -f ((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB)\""
      ' >> "%LOG%" 2>&1')
    a('powercfg /getactivescheme >> "%LOG%" 2>&1')
    a('>> "%LOG%" echo ===== SETTINGS: opencnc3d-fx.cfg =====')
    a('if exist "opencnc3d-fx.cfg" type "opencnc3d-fx.cfg" >> "%LOG%"')
    a('if not exist "opencnc3d-fx.cfg" >> "%LOG%" echo no saved settings: the shipped defaults')
    # each pass's CPU time, printed by framebench (FRAMEPROF lines); an older renderer
    # ignores it, so the probe still runs on any installed version
    a('set "OPENCNC3D_FRAMEPROF=1"')
    for n, (script, scen, w, h, what, exe, env) in enumerate(RUNS, 1):
        skip = "skip%d" % n
        if exe != "cnc_eyes.exe":
            a('if not exist "%s" goto %s' % (exe, skip))
        a("echo  Run %d of %d: %s ..." % (n, len(RUNS), what))
        a('>> "%%LOG%%" echo ===== RUN %d of %d: %s [%s] =====' % (n, len(RUNS), what, exe))
        a('>> "%LOG%" echo start %TIME%')
        if env:
            a('set "%s"' % env)
        a("%s --scen %s --pack %s.pack --dylib TiberianDawn.dll --dir missions/ --content content/ "
          "--nosound --w %d --h %d --enhanced --texset 3 %%GFX%% --showwindow --script %s >> \"%%LOG%%\" 2>&1"
          % (exe, scen, scen, w, h, script))
        a('>> "%LOG%" echo exit code %ERRORLEVEL%, end %TIME%')
        if env:
            a('set "%s="' % env.split("=")[0])
        if exe != "cnc_eyes.exe":
            a(":%s" % skip)
    a('>> "%LOG%" echo ===== THE LAST GAME LOG: opencnc3d-log.txt, last 800 lines =====')
    a('powershell -NoProfile -Command "if (Test-Path \'opencnc3d-log.txt\') { Get-Content \'opencnc3d-log.txt\' -Tail 800 }"'
      ' >> "%LOG%" 2>&1')
    a('>> "%LOG%" echo ===== END =====')
    for script in SCRIPTS:
        a('del "%s" >nul 2>&1' % script)
    a('del "perfprobe-fx.cfg" >nul 2>&1')
    a("echo.")
    a("echo  Done. Please send this file back:")
    a("echo.")
    a('echo     %CD%\\%LOG%')
    a("echo.")
    a('explorer /select,"%CD%\\%LOG%"')
    a("pause")
    with open(out, "w", newline="\r\n") as f:
        f.write("\n".join(L) + "\n")
    print("wrote", out, len(L), "lines")


if __name__ == "__main__":
    main()
