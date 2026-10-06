@echo off
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
REMIGravity.exe > "logs\Gravity.log" 2>&1
if errorlevel 1 (
  type "logs\Gravity.log"
  pause
  exit /b 1
)
