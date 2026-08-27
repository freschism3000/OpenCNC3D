@echo off
REM ===========================================================================
REM  CNC3D mission editor, Windows launcher.
REM
REM  It does four things and nothing else:
REM
REM    1. Makes THIS folder the working directory. The editor finds its packs,
REM       its missions folder and its content folder by relative path, so a
REM       shortcut or an admin prompt that started somewhere else would leave
REM       it looking for maps in C:\Windows\System32.
REM    2. Starts the editor on one map, SCG01EA, the temperate one.
REM    3. Sends everything it prints to Editor.log beside this file. A console
REM       window that closes on its own takes the reason with it, and a bug
REM       report without that text is one nobody can act on.
REM    4. Stops and waits if the editor exits badly, so the error is readable
REM       instead of flashing past.
REM
REM  There is no map picker here on purpose. To open a different map, open it
REM  INSIDE the editor: the MAP menu has OPEN MAP and NEW MAP.
REM ===========================================================================
setlocal
cd /d "%~dp0"

set "SCEN=SCG01EA"
set "LOG=%~dp0Editor.log"

echo Starting the CNC3D editor on %SCEN%.
echo Output goes to Editor.log in this folder.
echo.

REM The header is written in one redirected block. No round brackets in the
REM echoed text: inside a block cmd reads the first one as the end of the block.
(
    echo ==============================================================
    echo editor  %DATE% %TIME%
    echo   map      %SCEN%
    echo   folder   %CD%
    echo   build    @BUILD_ID@   Windows, 32-bit
    echo ==============================================================
) > "%LOG%"

REM ONE LINE ON PURPOSE. A caret continuation is one stray trailing space away
REM from silently splitting the command in half, and the argument list is well
REM inside what cmd will take.
cnc_eyes.exe --edit --scen %SCEN% --pack %SCEN%.pack --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dll --dir missions/ --content content/ >> "%LOG%" 2>&1

set RC=%ERRORLEVEL%
if not "%RC%"=="0" (
    echo.
    echo ---------------------------------------------------------------
    echo The editor stopped with exit code %RC%.
    echo.
    echo The end of Editor.log:
    echo.
    powershell -NoProfile -Command "Get-Content -Tail 20 -LiteralPath '%LOG%'"
    if errorlevel 1 type "%LOG%"
    echo.
    echo Full log: %LOG%
    echo Send that file back with the report.
    echo ---------------------------------------------------------------
    echo.
    pause
)
endlocal
