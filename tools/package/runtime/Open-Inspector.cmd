@echo off
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
REMISandbox.exe --inspector --static-gltf "%~dp0samples\static_triangle.glb" > "logs\Inspector.log" 2>&1
if errorlevel 1 (
  type "logs\Inspector.log"
  pause
  exit /b 1
)
