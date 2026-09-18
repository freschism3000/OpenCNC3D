@echo off
rem ===========================================================================
rem  C&C 3D -- put a "C&C 3D (dev)" shortcut on the Desktop. Run this ONCE.
rem
rem  It builds a second, separate copy of the game that follows main instead of
rem  following releases, and puts a shortcut to it on the Desktop. After this,
rem  double-clicking that shortcut fetches whatever was last pushed and green,
rem  and plays it. There is nothing else to run and nothing to keep in step.
rem
rem  THE EXISTING COPY IS NOT TOUCHED, and that is the whole reason there are
rem  two folders rather than one. An installed release has a launcher that
rem  updates it to the newest RELEASE; a development folder follows main. Point
rem  both at one folder and each undoes the other on alternate launches. Keeping
rem  them apart also means a broken commit costs nothing: the shortcut beside it
rem  still starts a build that is known to work.
rem
rem  THE DATA IS COPIED, NOT DOWNLOADED. About half a gigabyte of packs,
rem  missions, content, movies and dosdata moves once, here, because CI builds
rem  only the four binaries and the repository deliberately carries no baked
rem  data at all. Afterwards a dev update is roughly twenty megabytes.
rem
rem  Double-click it, or run it from PowerShell or cmd. It needs no arguments
rem  and no administrator rights.
rem ===========================================================================
setlocal enabledelayedexpansion

set "DEST=%LOCALAPPDATA%\Programs\CNC3D-dev"
set "DESK=%USERPROFILE%\Desktop"
set "LNKNAME=C&C 3D (dev).lnk"
set "LNK=%DESK%\%LNKNAME%"

echo.
echo   Setting up the C^&C 3D development shortcut.
echo.

rem ---------------------------------------------------- find a copy of the game
rem  Three places, in order of how much each can be trusted. The installer
rem  records where it put itself, so it is asked first; then the per-user path
rem  the installer proposes; then the Desktop, because a release also ships as a
rem  plain zip and an unzipped build folder is just as good a source of data as
rem  an install is.
rem
rem  WHAT IS WANTED IS THE DATA, NOT THE PROGRAM. A folder holding only binaries
rem  would satisfy a test for cnc3d.exe and then produce a development folder
rem  that cannot start a mission, so the missions folder is what decides.

set "SRC="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\CNC3D" /v InstallDir 2^>nul ^| find "InstallDir"') do call :consider "%%B"
call :consider "%LOCALAPPDATA%\Programs\CNC3D"
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "Get-ChildItem -LiteralPath $env:DESK -Directory -ErrorAction SilentlyContinue | Where-Object { (Test-Path (Join-Path $_.FullName 'missions')) -and (Test-Path (Join-Path $_.FullName 'cnc3d.exe')) } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName" 2^>nul`) do call :consider "%%D"

if not defined SRC (
  echo   Could not find a copy of C^&C 3D to take the game data from.
  echo.
  echo   Looked for a folder holding both cnc3d.exe and a missions folder, in:
  echo     the install path recorded by the installer
  echo     %LOCALAPPDATA%\Programs\CNC3D
  echo     any folder directly on the Desktop
  echo.
  echo   Either install a release, or unzip one onto the Desktop. Any recent
  echo   version will do: only its data is used, and the binaries are replaced
  echo   on the first launch anyway.
  echo.
  pause
  exit /b 1
)

echo   Game data will be taken from:
echo     %SRC%
echo   Development folder:
echo     %DEST%
echo.

rem ------------------------------------------------------------- copy the data
rem  /XF drops the two files that must not be duplicated. A release launcher
rem  would update this folder back to a release, which is exactly what it must
rem  not do here, and an uninstaller in a folder Add/Remove Programs knows
rem  nothing about would remove the game while leaving the entry behind.

if exist "%DEST%\cnc3d.exe" (
  echo   The development folder already exists, so its data is left as it is.
) else (
  echo   Copying the game data. This is around half a gigabyte and happens once.
  echo.
  robocopy "%SRC%" "%DEST%" /E /NFL /NDL /NJH /NJS /XF "uninstall.exe" "C&C3D.exe" >nul
  rem  Robocopy reports what it did as a bit field. Anything under 8 is success:
  rem  1 is "files copied", 2 is "extra files present", 3 is both. 8 and above
  rem  are real failures, so a plain errorlevel test would reject every good run.
  if errorlevel 8 (
    echo   The copy failed. Nothing has been changed.
    pause
    exit /b 1
  )
  echo   Copied.
)

rem ------------------------------------------------------- install the launcher
copy /y "%~dp0PLAY-DEV.bat" "%DEST%\PLAY-DEV.bat" >nul
if errorlevel 1 (
  echo   Could not copy PLAY-DEV.bat, which should be sitting beside this file.
  pause
  exit /b 1
)

rem ------------------------------------------------------------- the shortcut
rem  The paths reach PowerShell through the environment rather than through the
rem  command text, because the application's own name contains an ampersand and
rem  every layer in between would want it escaped differently.

set "TARGET=%DEST%\PLAY-DEV.bat"
set "ICON=%DEST%\cnc3d.exe,0"
powershell -NoProfile -Command "$w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut($env:LNK); $s.TargetPath=$env:TARGET; $s.WorkingDirectory=$env:DEST; $s.IconLocation=$env:ICON; $s.Description='C&C 3D, newest build from main'; $s.Save()"
if errorlevel 1 (
  echo   The shortcut could not be created.
  pause
  exit /b 1
)

echo.
echo   Done.
echo.
echo   "C^&C 3D (dev)" is on the Desktop. Double-click it and it will fetch the
echo   newest green build from main and start it. Any shortcut you already had
echo   is untouched and still plays the version it played before.
echo.
echo   One thing is needed before it can fetch anything: the GitHub CLI, signed
echo   in.  https://cli.github.com  then  gh auth login
echo   Without it the shortcut still plays, it just cannot pick up new builds.
echo.
pause
exit /b 0

rem ---------------------------------------------------------------------------
rem  Take the first candidate that is a real, playable folder. Called rather
rem  than repeated so that "what counts as a copy of the game" is written once:
rem  three places to look and three copies of the test is how the third one ends
rem  up subtly different from the other two.
:consider
if defined SRC goto :eof
if "%~1"=="" goto :eof
if not exist "%~1\cnc3d.exe" goto :eof
if not exist "%~1\missions\" goto :eof
set "SRC=%~1"
goto :eof
