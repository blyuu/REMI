@echo off
setlocal
set "REMI_ROOT=%~dp0"
set "REMI_EXE=%REMI_ROOT%build\vs2022-x64\bin\Release\REMIRaven.exe"
set "REMI_MAP=%REMI_ROOT%build\local-assets\soul-city\soul_city_neighborhood.glb"
set "REMI_RAVEN=%REMI_ROOT%build\local-assets\raven\raven.glb"
pushd "%REMI_ROOT%"
if not exist "%REMI_MAP%" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%REMI_ROOT%tools\assets\export-soul-city.ps1"
    if errorlevel 1 goto fail
)
if not exist "%REMI_RAVEN%" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%REMI_ROOT%tools\assets\import-raven.ps1"
    if errorlevel 1 goto fail
)
cmake --preset vs2022-x64
if errorlevel 1 goto fail
cmake --build --preset release --target REMIRaven
if errorlevel 1 goto fail
"%REMI_EXE%" %*
set "REMI_RESULT=%ERRORLEVEL%"
popd
if not "%REMI_RESULT%"=="0" pause
exit /b %REMI_RESULT%
:fail
echo Raven setup failed. Check the messages above.
popd
pause
exit /b 1
