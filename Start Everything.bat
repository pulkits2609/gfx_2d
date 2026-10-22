@echo off
setlocal
cd /d "%~dp0"
if exist "runtime\python\python.exe" (
    "runtime\python\python.exe" "tools\launch_everything.py" %*
) else (
    python "tools\launch_everything.py" %*
)
if errorlevel 1 pause
