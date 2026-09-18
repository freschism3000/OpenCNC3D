@echo off
rem ===========================================================================
rem  C&C 3D -- put a "C&C 3D (source)" shortcut on the Desktop. Run this ONCE
rem  per checkout.
rem
rem  There is nothing to copy and nothing to install. The shortcut points at
rem  PLAY-SOURCE.bat inside this checkout, and that script finds the compiler,
rem  the game data and the binaries for itself every time it runs. Move or
rem  rename the checkout and this shortcut stops working, which is the correct
rem  behaviour: it is a shortcut to THIS source tree and not to a copy of the
rem  game.
rem
rem  IT DOES NOT REPLACE THE OTHER SHORTCUTS. A machine can carry all three at
rem  once and they do not interfere: one plays the newest release, one follows
rem  main, and this one plays the working copy. Only this one changes when you
rem  edit code.
rem
rem  A SECOND CHECKOUT GETS A SECOND SHORTCUT, and it needs a different name or
rem  the two overwrite each other on the Desktop. Pass one:
rem
rem      MAKE-SOURCE-SHORTCUT.bat "C&C 3D (multiplayer branch)"
rem ===========================================================================
setlocal

pushd "%~dp0..\..\.." || (echo Could not reach the repository root.& pause & exit /b 1)
set "REPO=%CD%"
popd

set "LNKNAME=%~1"
if not defined LNKNAME set "LNKNAME=C&C 3D (source)"

set "TARGET=%~dp0PLAY-SOURCE.bat"
set "WORKDIR=%~dp0"
set "ICON=%REPO%\tools\launchers\cnc3d.ico,0"

if not exist "%TARGET%" (
  echo   PLAY-SOURCE.bat is not beside this file, so there is nothing to point at.
  pause
  exit /b 1
)

rem  The paths reach PowerShell through the environment rather than through the
rem  command text, because the application's own name contains an ampersand and
rem  every layer in between would want it escaped differently.
powershell -NoProfile -Command "$d=[Environment]::GetFolderPath('Desktop'); $p=Join-Path $d ($env:LNKNAME + '.lnk'); $w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut($p); $s.TargetPath=$env:TARGET; $s.WorkingDirectory=$env:WORKDIR; $s.IconLocation=$env:ICON; $s.Description='C&C 3D, compiled from this source checkout'; $s.Save(); Write-Output ('  created: ' + $p)"
if errorlevel 1 (
  echo   The shortcut could not be created.
  pause
  exit /b 1
)

echo.
echo   Source tree: %REPO%
echo.
echo   Double-click it and it compiles anything you have changed, then plays.
echo   An unchanged tree starts straight away.
echo.
echo   Two things have to be on the machine for the compiling half to work:
echo   Git Bash, and the MSYS2 32-bit toolchain at C:\msys64\mingw32. Without
echo   them the shortcut still plays whatever was built last, and says so.
echo.
pause
exit /b 0
