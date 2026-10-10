@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\SplitscreenSupport.ps1" -Action Uninstall -NoDialog
echo.
pause
