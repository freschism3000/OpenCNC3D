@echo off
rem ---------------------------------------------------------------------------
rem  C&C 3D -- launch the NEWEST build on the Desktop, on the new 640x480 HUD.
rem
rem  There is no build path written down in here on purpose. Every time it runs
rem  it looks at every CNC3D-windows-* folder on the Desktop, picks the one whose
rem  cnc_eyes.exe is newest, and launches that. Drop in a new build, unzip one
rem  from the Mac, or rebuild in place, and this follows it with no edits.
rem
rem  Pass "mission" to go straight into GDI mission 1 instead of the menu:
rem      play-latest-newhud.bat mission
rem
rem  A --vsync 0 workaround used to be forced here. It is gone on purpose: the
rem  stutter it papered over was never vsync, it was the brain's world dump
rem  leaving one WriteFile syscall per character (known-gap notes, 20 Aug 2026).
rem  With that fixed, vsync holds 60 fps at a full 15 Hz tick on every map size
rem  measured. If stutter returns, measure the tick cost before blaming the swap.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "MODE=%~1"
if "%MODE%"=="" set "MODE=menu"

set "DESK=%USERPROFILE%\Desktop"
set "GAME="
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "Get-ChildItem -LiteralPath '%DESK%' -Directory -Filter 'CNC3D-windows-*' -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName 'cnc_eyes.exe') } | Sort-Object { (Get-Item (Join-Path $_.FullName 'cnc_eyes.exe')).LastWriteTime } -Descending | Select-Object -First 1 -ExpandProperty FullName"`) do set "GAME=%%D"

if not defined GAME (
  echo.
  echo Could not find a C^&C 3D build on the Desktop.
  echo Looked for folders named CNC3D-windows-* containing cnc_eyes.exe in:
  echo   %DESK%
  echo.
  pause
  exit /b 1
)

cd /d "%GAME%"

echo.
echo Launching the newest build found:
echo   %GAME%
if exist BUILD-ID.txt (
  set /p BID=<BUILD-ID.txt
  echo   build: !BID!
)
echo   HUD:   new 640x480      mode: %MODE%
echo.

set CNC3D_HUD=new

if /i "%MODE%"=="mission" (
  cnc_eyes.exe --scen SCG01EC --pack SCG01EA.pack --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dll --dir missions\ --content content\ --w 1600 --h 960 > cnc3d-log.txt 2>&1
) else (
  cnc3d.exe --menupack dosmenu.pack --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dll --dir .\missions\ --content .\content\ --w 1600 --h 960 > cnc3d-log.txt 2>&1
)

echo.
echo ---------- the GL block, from cnc3d-log.txt ----------
powershell -NoProfile -Command "Get-Content cnc3d-log.txt -TotalCount 12"
echo -----------------------------------------------------
echo Full log: %GAME%\cnc3d-log.txt
echo If anything looks wrong, send that file.
pause
