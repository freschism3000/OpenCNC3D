#!/usr/bin/env python3
"""Build an optional same-binary Windows rendering comparison, without game assets."""
import argparse
from pathlib import Path
import shutil
import hashlib
import json
import subprocess
import zipfile

CONTROLS = ('OPENCNC3D_MESH_FRAGMENT_PREPARE', 'OPENCNC3D_MESH_FRAGMENT_COALESCE',
            'OPENCNC3D_MESH_FRAGMENT_PACK',
            'OPENCNC3D_EBLD_PREPARE', 'OPENCNC3D_GENERATED_MODE_MASK',
            'OPENCNC3D_MESH_HOT_ADMISSION', 'OPENCNC3D_TEXENV_CACHE',
            'OPENCNC3D_FRAGMENT_POLYGON_DEFER')


def scripts():
    tail = ['ebldump', 'meshperfdump', 'resourceprepdump']
    active = ['gfxmode 2', 'gfx show_stats 0', 'cheatset fog 0', 'tick 3',
              'cam 12 16', 'dist 2700', 'echo BENCH|phase=intact', 'framebench 1',
              'echo BENCH|phase=first-damage', 'hurtcell 15 15', 'hurtcell 15 15',
              'tick 1', 'framebench 1', *tail]
    for phase, skip in [('moving', 0), ('settling', 30), ('sinking', 90), ('late', 120)]:
        active += [f'tick {skip}', f'echo BENCH|phase={phase}'] if skip else [f'echo BENCH|phase={phase}']
        for sample in range(32):
            active += ['tick 1', f'tickalpha {(sample % 4) * .25:g}', 'framebench 1']
        active += tail
    active += ['quit']
    siege = ['gfxmode 2', 'gfx show_stats 0', 'tick 30', 'reveal 1',
             'cam 28.286686 36.934425', 'dist 4200', 'echo BENCH|phase=warmup', 'framebench 3',
             'echo BENCH|phase=active-siege']
    for sample in range(80):
        siege += ['tick 1', f'tickalpha {(sample % 4) * .25:g}', 'framebench 1']
    siege += tail + ['quit']
    return {'active.txt': '\n'.join(active)+'\n', 'siege.txt': '\n'.join(siege)+'\n'}


CMD = r'''@echo off
setlocal DisableDelayedExpansion
pushd "%~dp0" || exit /b 1
if not exist "cnc_eyes.exe" goto missing
if not exist "TiberianDawn.dll" goto missing
if not exist "SCB01EA.pack" goto missing
if not exist "SCB35EA.pack" goto missing
set "RUN=Logs\performance-comparison-%RANDOM%-%RANDOM%"
if exist "%RUN%" goto failed
mkdir "%RUN%\private\mission" || goto failed
mkdir "%RUN%\report" || goto failed
copy /y "Performance-Comparison\SCG94EA.INI" "%RUN%\private\mission\SCG94EA.INI" >nul || goto failed
copy /y "Performance-Comparison\SCG94EA.BIN" "%RUN%\private\mission\SCG94EA.BIN" >nul || goto failed
set "GFXARG="
if exist "opencnc3d-fx.cfg" (
  copy /y "opencnc3d-fx.cfg" "%RUN%\private\input.cfg" >nul || goto failed
)
if exist "%RUN%\private\input.cfg" set GFXARG=--gfx "%RUN%\private\input.cfg"
if exist "BUILD-ID.txt" (
  copy /y "BUILD-ID.txt" "%RUN%\report\installed-BUILD-ID.txt" >nul || goto failed
)
copy /y "Performance-Comparison\comparison-source.json" "%RUN%\report\comparison-source.json" >nul || goto failed
set "OPENCNC3D_BACKGROUND=1"
set "OPENCNC3D_FRAMEPROF=1"
set "OPENCNC3D_PERFDETAIL=1"
set "OPENCNC3D_PERFTRACE="
set "OPENCNC3D_GPUPROF="
set "OPENCNC3D_MESH_FRAGMENT_REUSE=1"
set "OPENCNC3D_MESH_PREPARED_BUDGET_FULL=1"
set "OPENCNC3D_MESH_PREPARED_ALLOC_OK=1"
set "OPENCNC3D_MESH_FRAGMENT_GPU_OK=1"
set "OPENCNC3D_MESH_FRAGMENT_PACK_OK=1"
set "OPENCNC3D_MESH_FRAGMENT_PACK_ALLOC_OK=1"
set "OPENCNC3D_MESH_FRAGMENT_BUFFER_OK=1"
set "OPENCNC3D_MESH_CACHE_FULL=1"
set "OPENCNC3D_MESH_CACHE_ALLOC_OK=1"
echo This renders hidden scripted scenes. It does not measure displayed FPS.
echo Close other game copies and leave this running until it finishes.
echo Graphics settings are identical between both variants; only optimization switches change.
echo Baseline, optimized, optimized, baseline order reduces order bias.
call :pair baseline-1 0 || goto failed
call :pair optimized-1 1 || goto failed
call :pair optimized-2 1 || goto failed
call :pair baseline-2 0 || goto failed
echo PASS: all eight scripted runs completed. > "%RUN%\report\status.txt" || goto failed
echo Same executable and settings; each measured active sample advances tick and interpolation. >> "%RUN%\report\status.txt" || goto failed
echo glFinish rendering completion times, not live gameplay or compositor FPS. >> "%RUN%\report\status.txt" || goto failed
echo Exact first-use work can move into loading; compare loading and demand separately. >> "%RUN%\report\status.txt" || goto failed
echo Read raw logs only locally: the report folder excludes source paths and raw configuration. >> "%RUN%\report\status.txt" || goto failed
echo.
echo Complete. Zip ONLY this report folder, review it, and send it with your observations:
echo "%CD%\%RUN%\report"
popd
pause
exit /b 0

:pair
echo Running %1...
__CONTROL_LINES__
"cnc_eyes.exe" --enhanced --scen SCG94EA --build 98 --pack SCB01EA.pack --dir "%RUN%/private/mission/" --content content/ --nosound --ticks 0 --w 1920 --h 1080 %GFXARG% --gfxsave "%RUN%/private/%1-fx.cfg" --savedir "%RUN%/private/saves/" --script Performance-Comparison/active.txt > "%RUN%\private\%1-active.log" 2>&1
if errorlevel 1 exit /b 1
findstr /r /c:", 0 failures$" "%RUN%\private\%1-active.log" >nul || exit /b 1
findstr /b /c:"FRAGMENTPREP|" "%RUN%\private\%1-active.log" >nul || exit /b 1
findstr /b /c:"ECHO|BENCH|" /c:"FRAMEBENCH|" /c:"FRAMEPROF|" /c:"GL_RENDERER" /c:"GL_VERSION" /c:"FRAGMENTPREP|" /c:"SUNSTATIC|" /c:"EBLDPREPARE|" /c:"MESHPERF|" /c:"MESHPOOL|" /c:"FRAGMENTREUSE|" /c:"EBLDOBJ|" "%RUN%\private\%1-active.log" > "%RUN%\report\%1-active.log" || exit /b 1
"cnc_eyes.exe" --enhanced --scen SCB35EA --build 98 --pack SCB35EA.pack --dir missions/ --content content/ --nosound --ticks 0 --w 1920 --h 1080 %GFXARG% --gfxsave "%RUN%/private/%1-fx.cfg" --savedir "%RUN%/private/saves/" --script Performance-Comparison/siege.txt > "%RUN%\private\%1-siege.log" 2>&1
if errorlevel 1 exit /b 1
findstr /r /c:", 0 failures$" "%RUN%\private\%1-siege.log" >nul || exit /b 1
findstr /b /c:"ECHO|BENCH|" /c:"FRAMEBENCH|" /c:"FRAMEPROF|" /c:"GL_RENDERER" /c:"GL_VERSION" /c:"FRAGMENTPREP|" /c:"SUNSTATIC|" /c:"EBLDPREPARE|" /c:"MESHPERF|" /c:"MESHPOOL|" /c:"FRAGMENTREUSE|" /c:"EBLDOBJ|" "%RUN%\private\%1-siege.log" > "%RUN%\report\%1-siege.log" || exit /b 1
exit /b 0

:missing
echo Extract this comparison package into the complete updated game folder, beside cnc_eyes.exe.
popd
pause
exit /b 1

:failed
echo Comparison failed. Keep the private logs locally; do not report a successful benchmark.
echo The supplied updated game is required. No installed saves or settings were replaced.
popd
pause
exit /b 1
'''


def build(repo, output):
    output.mkdir(parents=True, exist_ok=False)
    data=output/'Performance-Comparison';data.mkdir()
    fixture=repo/'tools/regressions/sidebar-potential/fixtures/SCG97EA.BIN'
    shutil.copy2(fixture,data/'SCG94EA.BIN')
    ini='''[Basic]
Player=GoodGuy
Action=x
Lose=x
Win=x
Brief=x
Intro=x
BuildLevel=98
Theme=No theme
Percent=0
CarryOverCap=-1
CarryOverMoney=0
[MAP]
Height=58
Width=57
Y=3
X=3
Theater=DESERT
[GoodGuy]
Allies=GoodGuy,Neutral
Credits=0
Edge=South
[BadGuy]
Allies=BadGuy
Credits=0
Edge=North
[Neutral]
Allies=Neutral,GoodGuy
Credits=0
Edge=North
[STRUCTURES]
000=GoodGuy,PYLE,100,966,0,None
001=GoodGuy,NUKE,256,975,0,None
[UNITS]
[INFANTRY]
[OVERLAY]
[TERRAIN]
[SMUDGE]
[Triggers]
[CellTriggers]
[Teams]
[Waypoints]
[Base]
Count=0
'''
    (data/'SCG94EA.INI').write_text(ini)
    for name,text in scripts().items():(data/name).write_text(text)
    controls='\n'.join('set "'+name+'=%2"' for name in CONTROLS)
    (output/'Run-Performance-Comparison.cmd').write_bytes(CMD.replace('__CONTROL_LINES__',controls).replace('\n','\r\n').encode('ascii'))
    (output/'README.txt').write_text('''Optional same-build rendering comparison

Extract beside cnc_eyes.exe in the supplied current Windows game update, then
double-click Run-Performance-Comparison.cmd. Close other game copies first.
No Python, PowerShell, account or administrator rights are needed.

The test uses the actual installed model assets and identical Enhanced settings
for both variants. It creates only its own Logs/performance-comparison-* folder.
Installed saves and settings are not replaced. It runs hidden scripted scenes
in baseline/optimized/optimized/baseline order. Active samples advance ticks and
interpolation; this is not a frozen paused-cache benchmark. The generated mesh census skips only empty passes; geometry,
shadows and material settings remain identical.

Completion times include explicit glFinish and are not displayed FPS. Frame
profiling adds overhead in both variants. This does not replace normal-play
diagnostic captures of the actual slowdown. Cold resource work may move into
loading; demand timing alone must not be called a total-work reduction.

Zip ONLY the report subfolder. Raw logs and the copied configuration in private
remain local. Nothing is uploaded automatically. Review reports before sharing.
''')
    identity={'format':1,'packet_source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
              'scope':'Comparison script identity; does not verify the installed executable.',
              'controls':list(CONTROLS),'fixture_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(data.iterdir())}}
    (data/'comparison-source.json').write_text(json.dumps(identity,indent=2)+'\n')
    archive=output.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for f in sorted(output.rglob('*')):
            if f.is_file():z.write(f,str(f.relative_to(output)).replace('\\','/'))
    return archive


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    print(build(a.repo.resolve(),a.output.resolve()))
