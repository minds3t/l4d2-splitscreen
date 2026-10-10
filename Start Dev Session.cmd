@echo off
rem Development: puts the working build into build\dev as a release folder and starts it.
python "%~dp0tools\packaging\build_release.py" --dev
if errorlevel 1 (pause & exit /b 1)
call "%~dp0build\dev\Start Local Co-op.cmd"
