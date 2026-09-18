@echo off
rem ===========================================================================
rem  C&C 3D -- play the newest build from main, rather than the newest RELEASE.
rem
rem  This is the file the "C&C 3D (dev)" Desktop shortcut runs. On every launch
rem  it asks GitHub for the newest commit on main whose CI run went green. If
rem  that is not what is already sitting in this folder it downloads the four
rem  Windows binaries from that run and drops them in beside the data. Then it
rem  starts the game.
rem
rem  ONLY THE BINARIES EVER MOVE. The game data (the packs, missions, content,
rem  movies and dosdata) is a build product that CI does not produce and the
rem  repository does not carry, so this folder is seeded once from an installed
rem  release and its data is then left alone forever. That is why there is a
rem  setup script beside this one, and why this file cannot create the folder
rem  itself.
rem
rem  A CI BUILD IS NOT THE BUILD A RELEASE SHIPS, and the difference is written
rem  down rather than hidden: CI cross compiles on Linux against the MSVCRT
rem  mingw runtime, while a release is cross compiled against the UCRT one. Both
rem  run on Windows 11 and both come from identical sources, but the release is
rem  still the artefact that gets tested properly. This is for looking at work
rem  in progress between releases.
rem
rem  IF THE UPDATE CANNOT HAPPEN, THE GAME STILL STARTS. No network, no GitHub
rem  CLI, an expired artifact or a red run all leave the previous build in place
rem  and say so. Being unable to fetch a newer build is not a reason to be
rem  unable to play the one already here.
rem ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "REPO=freschism3000/cnc3d"
set "STAMP=DEV-BUILD-ID.txt"

set "HAVE=none"
if exist "%STAMP%" set /p HAVE=<"%STAMP%"

echo.
echo   C^&C 3D, development build
echo   folder:    %CD%
echo   installed: %HAVE%
echo.

rem ---------------------------------------------------------------- update
rem
rem  Every failure below is a warning and not an exit. The single "goto play"
rem  target is deliberate: there is exactly one way past this section, so a new
rem  check cannot accidentally become a new way to refuse to start the game.

where gh >nul 2>&1
if errorlevel 1 (
  echo   [skip] The GitHub CLI is not on PATH, so this cannot check for a newer
  echo          build. Install it from https://cli.github.com and run "gh auth login".
  goto play
)

echo   Asking GitHub for the newest green build on main...

set "WANT="
for /f "usebackq delims=" %%S in (`gh run list --repo %REPO% --branch main --workflow build.yml --status success --limit 1 --json headSha --jq ".[0].headSha" 2^>nul`) do set "WANT=%%S"

if not defined WANT (
  echo   [skip] Could not read the run list. Either the network is down, or
  echo          "gh auth login" has not been done on this machine.
  goto play
)

set "RUNID="
for /f "usebackq delims=" %%R in (`gh run list --repo %REPO% --branch main --workflow build.yml --status success --limit 1 --json databaseId --jq ".[0].databaseId" 2^>nul`) do set "RUNID=%%R"

if not defined RUNID (
  echo   [skip] Found commit %WANT% but not the run that built it.
  goto play
)

if /i "%WANT%"=="%HAVE%" (
  echo   Already on the newest green build.
  goto play
)

echo   Newer build available: %WANT%
echo   Downloading the Windows binaries from run %RUNID%...

set "DROP=%TEMP%\cnc3d-dev-%WANT%"
if exist "%DROP%" rd /s /q "%DROP%"

gh run download %RUNID% --repo %REPO% -n cnc3d-windows-%WANT% -D "%DROP%" >nul 2>&1
if errorlevel 1 (
  echo   [skip] The download failed. A CI artifact expires after ninety days,
  echo          which is the usual reason an older commit cannot be fetched.
  goto play
)

rem  All four files are checked BEFORE any of them is copied. A half applied
rem  update is the one outcome worth real trouble to avoid: a new cnc3d.exe
rem  against an old TiberianDawn.dll is a crash with no obvious cause.
for %%F in (cnc3d.exe cnc_eyes.exe SDL2.dll TiberianDawn.dll) do (
  if not exist "%DROP%\%%F" (
    echo   [skip] The artifact is missing %%F, so nothing was replaced.
    goto play
  )
)

copy /y "%DROP%\cnc3d.exe"        "%CD%\cnc3d.exe"        >nul || goto copyfail
copy /y "%DROP%\cnc_eyes.exe"     "%CD%\cnc_eyes.exe"     >nul || goto copyfail
copy /y "%DROP%\SDL2.dll"         "%CD%\SDL2.dll"         >nul || goto copyfail
copy /y "%DROP%\TiberianDawn.dll" "%CD%\TiberianDawn.dll" >nul || goto copyfail

> "%STAMP%" echo %WANT%
set "HAVE=%WANT%"
rd /s /q "%DROP%" 2>nul
echo   Updated to %WANT%.
goto play

:copyfail
echo   [warn] A binary could not be replaced, most likely because the game is
echo          already running from this folder. Close it and launch again.

rem ---------------------------------------------------------------- play
:play
echo.
echo   Starting on the 640x480 HUD. Build: %HAVE%
echo.

set CNC3D_HUD=new
cnc3d.exe --menupack dosmenu.pack --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dll --dir .\missions\ --content .\content\ --w 1600 --h 960 > cnc3d-log.txt 2>&1
set "RC=%ERRORLEVEL%"

echo.
echo   ---------- the GL block, from cnc3d-log.txt ----------
powershell -NoProfile -Command "Get-Content cnc3d-log.txt -TotalCount 12"
echo   -----------------------------------------------------
echo   build %HAVE%, exit code %RC%
echo   Full log: %CD%\cnc3d-log.txt
echo   If anything looks wrong, send that file back with the report.
echo.
pause
endlocal
