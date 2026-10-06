@echo off
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
REMIGravity.exe --smoke --warp > "logs\Smoke-Gravity.log" 2>&1
if errorlevel 1 goto failed
REMIGravity.exe --smoke --warp --level 2 > "logs\Smoke-Gravity-Level2.log" 2>&1
if errorlevel 1 goto failed
REMIRelay.exe --smoke --warp > "logs\Smoke-Relay.log" 2>&1
if errorlevel 1 goto failed
REMISandbox.exe --smoke --warp --inspector --static-gltf "%~dp0samples\static_triangle.glb" > "logs\Smoke-Inspector.log" 2>&1
if errorlevel 1 goto failed
echo All four smoke checks passed. See logs for details.
pause
exit /b 0
:failed
echo A smoke check failed. See logs for details.
pause
exit /b 1
