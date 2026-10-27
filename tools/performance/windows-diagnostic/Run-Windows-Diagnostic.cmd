@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-Windows-Diagnostic.ps1" %*
if errorlevel 1 (
  echo.
  echo The diagnostic did not complete. Read the message above; your original installation was not replaced.
)
pause
