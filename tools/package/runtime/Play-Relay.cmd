@echo off
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
REMIRelay.exe > "logs\Relay.log" 2>&1
if errorlevel 1 (
  type "logs\Relay.log"
  pause
  exit /b 1
)
