@echo off
setlocal
set "REMI_ROOT=%~dp0"
set "REMI_EXE=%REMI_ROOT%build\vs2022-x64\bin\Release\REMIVillage.exe"
set "REMI_ASSETS=%REMI_ROOT%build\local-assets\quarry"
pushd "%REMI_ROOT%"
if not exist "%REMI_ASSETS%\quarry_map.glb" goto build_assets
if not exist "%REMI_ASSETS%\survival_character.glb" goto build_assets
goto launch
:build_assets
powershell -NoProfile -ExecutionPolicy Bypass -File "%REMI_ROOT%tools\assets\build-quarry-assets.ps1"
if errorlevel 1 goto fail
:launch
if not exist "%REMI_EXE%" (
    echo REMIVillage.exe was not found. Building the Release game...
    cmake --preset vs2022-x64
    if errorlevel 1 goto fail
)
cmake --build --preset release --target REMIVillage
if errorlevel 1 goto fail
"%REMI_EXE%" --quarry %*
set "REMI_RESULT=%ERRORLEVEL%"
popd
if not "%REMI_RESULT%"=="0" pause
exit /b %REMI_RESULT%
:fail
echo Quarry setup failed. Check the messages above.
popd
pause
exit /b 1
