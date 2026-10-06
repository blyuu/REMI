@echo off
setlocal
set "REMI_ROOT=%~dp0"
set "REMI_EXE=%REMI_ROOT%build\vs2022-x64\bin\Release\REMIVillage.exe"
set "REMI_ASSETS=%REMI_ROOT%build\local-assets\apocalypse"
if not exist "%REMI_ASSETS%\apocalypse_building.glb" goto build_assets
if not exist "%REMI_ASSETS%\survival_character.glb" goto build_assets
goto launch
:build_assets
powershell -NoProfile -ExecutionPolicy Bypass -File "%REMI_ROOT%tools\build-apocalypse-assets.ps1"
if errorlevel 1 goto fail
:launch
if not exist "%REMI_EXE%" (
    echo REMIVillage.exe was not found. Building the Release game...
    cmake --preset vs2022-x64
    if errorlevel 1 goto fail
    cmake --build --preset release --target REMIVillage
    if errorlevel 1 goto fail
)
pushd "%REMI_ROOT%"
"%REMI_EXE%" %*
set "REMI_RESULT=%ERRORLEVEL%"
popd
if not "%REMI_RESULT%"=="0" pause
exit /b %REMI_RESULT%
:fail
echo Apocalypse setup failed. Check the messages above.
pause
exit /b 1
