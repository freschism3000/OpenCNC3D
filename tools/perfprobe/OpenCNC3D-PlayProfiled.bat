@echo off
rem OpenCNC3D with its frame profiler on. Put this file in the OpenCNC3D folder, next to
rem OpenCNC3D.exe, and double-click it instead of the game. Play as you normally would:
rem every five seconds of play the game's log (opencnc3d-log.txt, in the same folder) gains
rem a FRAMEPROF line naming what each part of the frame cost, in milliseconds. Quit the
rem game, then send opencnc3d-log.txt back.
set OPENCNC3D_FRAMEPROF=1
start "" "%~dp0OpenCNC3D.exe"
