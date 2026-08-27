@echo off
rem ---------------------------------------------------------------------------
rem  TUNE THE PRESENTATION -- the Windows half of TUNE-PRESENTATION.command.
rem
rem  Boots the DOS main menu with the Tier 2 presentation chain ON and its panel
rem  already open. F5 opens and closes it, SAVE writes cnc3d-fx.cfg into the
rem  build folder, and THAT is the file to send back.
rem
rem  Same "find the newest build on the Desktop" rule as PLAY-LATEST-NEWHUD.bat,
rem  for the same reason: no build path is written down in here, so dropping in
rem  a new build needs no edit.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "DESK=%USERPROFILE%\Desktop"
set "GAME="
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "Get-ChildItem -LiteralPath '%DESK%' -Directory -Filter 'CNC3D-windows-*' -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName 'cnc3d.exe') } | Sort-Object { (Get-Item (Join-Path $_.FullName 'cnc3d.exe')).LastWriteTime } -Descending | Select-Object -First 1 -ExpandProperty FullName"`) do set "GAME=%%D"

if not defined GAME (
  echo.
  echo Could not find a C^&C 3D build on the Desktop.
  echo Looked for folders named CNC3D-windows-* containing cnc3d.exe in:
  echo   %DESK%
  echo.
  pause
  exit /b 1
)

cd /d "%GAME%"

echo.
echo   F5 opens and closes the tuning panel
echo   SAVE writes cnc3d-fx.cfg into %GAME%. Send that file back.
echo.

cnc3d.exe --menupack dosmenu.pack --cameos cameos.pack --dospack dossidebar.pack ^
  --dosinf dosinfantry.pack --dylib TiberianDawn.dll --dir .\missions\ --content .\content\ ^
  --w 1280 --h 720 --gfx --gfxpanel --gfxsave cnc3d-fx.cfg > cnc3d-log.txt 2>&1

echo.
echo ---------- the FX block, from cnc3d-log.txt ----------
powershell -NoProfile -Command "Select-String -Path cnc3d-log.txt -Pattern '^(FX\||GL_)' | Select-Object -First 12 | ForEach-Object { $_.Line }"
echo -----------------------------------------------------
echo Full log: %GAME%\cnc3d-log.txt
echo If the panel said SHADERS UNAVAILABLE, send that file.
pause
