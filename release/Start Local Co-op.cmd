@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\SplitscreenSupport.ps1" -Action Start -NoDialog
if errorlevel 1 pause
