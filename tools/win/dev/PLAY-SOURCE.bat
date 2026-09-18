@echo off
rem ===========================================================================
rem  C&C 3D -- play the SOURCE TREE this file sits in.
rem
rem  The two launchers beside this one play a build made somewhere else: one
rem  follows the newest release, the other downloads the newest green CI build
rem  from main. This one COMPILES THE WORKING COPY IT LIVES IN and plays that,
rem  so an edit that has not been committed, pushed or built by anyone else is
rem  in the game the moment it starts. That is its whole reason to exist.
rem
rem  THE GAME DATA IS NOT BUILT HERE AND IS NOT IN THE REPOSITORY. The packs,
rem  the missions, content and movies are extracted assets that no step in the
rem  tree produces, so this borrows them from any installed or unzipped copy of
rem  the game and supplies only the binaries itself. Nothing is copied either
rem  way: the game runs with the DATA folder as its working directory and the
rem  executables named by absolute path out of build\win. A source build and a
rem  release can therefore sit on one machine without either overwriting the
rem  other, which they would do if this staged a folder of its own.
rem
rem  IT REBUILDS ONLY WHEN A SOURCE FILE IS NEWER than the binaries, so an
rem  unchanged tree starts at once. The brain is asked about separately because
rem  it is the slow half of the build and changes rarely.
rem
rem  IF IT CANNOT BUILD, IT STILL PLAYS what was built last, and says so. Being
rem  unable to pick up new work is not a reason to be unable to start the game.
rem
rem  TWO COPIES CAN RUN AT ONCE, which is what testing a host and a joiner on
rem  one machine needs: each names its log after its own process, so the second
rem  to start does not truncate the first one's evidence.
rem
rem  Arguments, all optional:
rem      norebuild   play what is there, do not compile even if it is stale
rem      mission     go straight into GDI mission 1 instead of the menu
rem ===========================================================================
setlocal enabledelayedexpansion

rem  The repository root is three levels up from tools\win\dev, and is derived
rem  rather than written down so that a second checkout gets its own game.
pushd "%~dp0..\..\.." || (echo Could not reach the repository root.& pause & exit /b 1)
set "REPO=%CD%"
popd

set "BIN=%REPO%\build\win"
set "REBUILD=1"
set "MODE=menu"
for %%A in (%*) do (
  if /i "%%~A"=="norebuild" set "REBUILD=0"
  if /i "%%~A"=="mission"   set "MODE=mission"
)

echo.
echo   C^&C 3D, built from source
echo   source:  %REPO%

rem ------------------------------------------------------------------ the data
rem  What is wanted is the DATA, not the program, so the test is the missions
rem  folder. A folder holding only binaries would pass a test for cnc3d.exe and
rem  then produce a game that cannot start a match. The Desktop is searched last
rem  and by newest, because a release also ships as a plain zip and an unzipped
rem  folder is as good a source of data as an install.
rem
rem  SEVERAL DESKTOP CANDIDATES ARE OFFERED, not just the newest. A folder can
rem  hold a missions folder and still not be a playable copy, and asking for a
rem  single best guess means one near miss leaves this with nothing while a good
rem  folder sits beside it. :consider takes the first that passes.

rem  TWICE, AND A COMPLETE COPY WINS. The first pass will only take a folder that
rem  also carries multiplayer map packs; the second takes anything with data at all
rem  and says out loud what will not work. Without the first pass the newest folder
rem  wins on date alone, and a folder can carry missions, the menu pack and the whole
rem  campaign while carrying no SCM packs -- at which point the game finds no
rem  multiplayer maps, HOST refuses, and nothing on screen says why. That is not
rem  hypothetical: a folder kept for testing sorted to the top merely by being written
rem  to, and multiplayer stopped working with an error about picking a map.
set "DATA="
set "STRICT=1"
call :search
if defined DATA goto :gotdata
set "STRICT=0"
call :search
if not defined DATA goto :nodata
echo   [warn] The newest copy of the data has no multiplayer map packs, so this
echo          one was taken instead. If HOST still refuses, unzip a full release
echo          onto the Desktop: SCM*.pack is what a multiplayer match is made of.
:gotdata

:nodata
if not defined DATA (
  echo.
  echo   Could not find the game DATA anywhere. This launcher builds the code
  echo   but the packs, missions, content and movies are extracted assets that
  echo   no build step produces and the repository does not carry.
  echo.
  echo   Looked for a folder holding a missions folder, in:
  echo     the install path recorded by the installer
  echo     %LOCALAPPDATA%\Programs\CNC3D
  echo     %LOCALAPPDATA%\Programs\CNC3D-dev
  echo     any folder directly on the Desktop
  echo.
  echo   Install a release, or unzip one onto the Desktop. Any recent version
  echo   will do: only its data is used and the binaries come from the source.
  echo.
  pause
  exit /b 1
)
echo   data:    %DATA%

rem --------------------------------------------------------------- the rebuild
rem  Every failure in this section is a warning and not an exit, and there is
rem  exactly one way past it, so a new check cannot become a new way to refuse
rem  to start the game.

if "%REBUILD%"=="0" (
  echo   build:   not checked, asked to skip
  goto play
)

set "GITBASH="
if exist "%ProgramFiles%\Git\bin\bash.exe"      set "GITBASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined GITBASH if exist "%ProgramW6432%\Git\bin\bash.exe"  set "GITBASH=%ProgramW6432%\Git\bin\bash.exe"
if not defined GITBASH if exist "%LOCALAPPDATA%\Programs\Git\bin\bash.exe" set "GITBASH=%LOCALAPPDATA%\Programs\Git\bin\bash.exe"

if not defined GITBASH (
  echo   [skip] Git Bash was not found, and every build script in this tree is
  echo          a shell script. Playing the binaries that are already built.
  goto play
)

rem  Is anything newer than what was built? Asked of the code the game is made
rem  of, and of the brain separately: the brain is most of the build time and a
rem  renderer edit must not pay for it.
set "STALE=0"
set "BRAINSTALE=0"
if not exist "%BIN%\cnc_eyes.exe" set "STALE=1"
if not exist "%BIN%\TiberianDawn.dll" set "BRAINSTALE=1"

if exist "%BIN%\cnc_eyes.exe" (
  for /f "usebackq delims=" %%N in (`powershell -NoProfile -Command "$b=(Get-Item -LiteralPath (Join-Path $env:BIN 'cnc_eyes.exe')).LastWriteTime; $e='.c','.cpp','.h','.hpp'; $d='game','app','menu','net','video','audio','compat' | ForEach-Object { Join-Path $env:REPO $_ } | Where-Object { Test-Path $_ }; $n=Get-ChildItem -LiteralPath $d -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $e -contains $_.Extension -and $_.LastWriteTime -gt $b } | Select-Object -First 1; if ($n) { 'yes' } else { 'no' }" 2^>nul`) do if "%%N"=="yes" set "STALE=1"
)
if exist "%BIN%\TiberianDawn.dll" (
  for /f "usebackq delims=" %%N in (`powershell -NoProfile -Command "$b=(Get-Item -LiteralPath (Join-Path $env:BIN 'TiberianDawn.dll')).LastWriteTime; $e='.c','.cpp','.h','.patch'; $d=Join-Path $env:REPO 'brain'; if (Test-Path $d) { $n=Get-ChildItem -LiteralPath $d -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $e -contains $_.Extension -and $_.LastWriteTime -gt $b } | Select-Object -First 1; if ($n) { 'yes' } else { 'no' } } else { 'no' }" 2^>nul`) do if "%%N"=="yes" set "BRAINSTALE=1"
)

if "%STALE%"=="0" if "%BRAINSTALE%"=="0" (
  echo   build:   up to date
  goto play
)

set "BRAINARG=--no-brain"
if "%BRAINSTALE%"=="1" set "BRAINARG="
if "%BRAINSTALE%"=="1" (
  echo   build:   the engine changed, so this rebuilds it too and takes longer
) else (
  echo   build:   sources changed, compiling
)
echo.

rem  The compiler lives in MSYS2 and is not on the PATH a shortcut inherits.
"%GITBASH%" -c "export PATH=/c/msys64/mingw32/bin:$PATH; cd \"$(cygpath -u '%REPO%')\" && tools/win/build-win.sh %BRAINARG%"
if errorlevel 1 (
  echo.
  echo   [warn] The build failed. Nothing was replaced, so what follows is the
  echo          last build that worked. The compiler's own output is above.
  echo.
  pause
)

rem ------------------------------------------------------------------- the play
:play
if not exist "%BIN%\cnc3d.exe" (
  echo.
  echo   There is no cnc3d.exe in %BIN% and it could not be built, so there is
  echo   nothing to start.
  echo.
  pause
  exit /b 1
)

rem  The log is named after this process so that a host and a joiner started
rem  from two copies of this shortcut do not overwrite each other's evidence.
set "LOG=%DATA%\cnc3d-source-log-%RANDOM%.txt"

echo   log:     %LOG%
echo.

set CNC3D_HUD=new
cd /d "%DATA%"
if /i "%MODE%"=="mission" (
  "%BIN%\cnc_eyes.exe" --scen SCG01EC --pack SCG01EA.pack --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib "%BIN%\TiberianDawn.dll" --dir .\missions\ --content .\content\ --w 1600 --h 960 > "%LOG%" 2>&1
) else (
  "%BIN%\cnc3d.exe" --menupack dosmenu.pack --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib "%BIN%\TiberianDawn.dll" --dir .\missions\ --content .\content\ --w 1600 --h 960 > "%LOG%" 2>&1
)
set "RC=%ERRORLEVEL%"

echo.
echo   ---------- the GL block, from the log ----------
powershell -NoProfile -Command "Get-Content -LiteralPath $env:LOG -TotalCount 12"
echo   -----------------------------------------------
echo   exit code %RC%
echo   Full log: %LOG%
echo   If anything looks wrong, send that file back with the report.
echo.
pause
endlocal
exit /b 0

rem ---------------------------------------------------------------------------
rem  Take the first candidate that actually carries game data. Called rather
rem  than repeated so that "what counts as a copy of the data" is written once:
rem  four places to look and four copies of the test is how the fourth one ends
rem  up subtly different from the other three.
:search
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\CNC3D" /v InstallDir 2^>nul ^| findstr /i "InstallDir"') do call :consider "%%B"
call :consider "%LOCALAPPDATA%\Programs\CNC3D"
call :consider "%LOCALAPPDATA%\Programs\CNC3D-dev"
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "Get-ChildItem -LiteralPath ([Environment]::GetFolderPath('Desktop')) -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName 'missions') } | Sort-Object LastWriteTime -Descending | Select-Object -First 8 -ExpandProperty FullName" 2^>nul`) do call :consider "%%D"
goto :eof

:consider
if defined DATA goto :eof
if "%~1"=="" goto :eof
if not exist "%~1\missions\" goto :eof
if not exist "%~1\dosmenu.pack" goto :eof
rem  ONE MULTIPLAYER MAP IS THE TEST FOR A COMPLETE COPY, on the strict pass. The
rem  game builds its multiplayer map list from SCM*.pack beside the executable's
rem  working directory, so a folder without them can start and can play the
rem  campaign, and can do nothing at all in multiplayer.
if "%STRICT%"=="1" if not exist "%~1\SCM*.pack" goto :eof
set "DATA=%~1"
goto :eof
