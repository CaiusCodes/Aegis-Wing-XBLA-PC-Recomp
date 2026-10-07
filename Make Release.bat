@echo off
setlocal
set "AEGIS_REXSDK_DIR=%REXSDK_DIR%"
if not defined AEGIS_REXSDK_DIR if exist "%~dp0..\rexglue-sdk-0.9.0\CMakeLists.txt" set "AEGIS_REXSDK_DIR=%~dp0..\rexglue-sdk-0.9.0"
if not defined AEGIS_REXSDK_DIR (
    echo.
    echo The patched ReXGlue SDK source folder could not be found.
    echo Set REXSDK_DIR to that folder, then run this file again.
    pause
    exit /b 1
)
cmake --preset win-amd64-release -DREXSDK_DIR="%AEGIS_REXSDK_DIR%"
if errorlevel 1 (
    echo.
    echo Release configuration failed. No release ZIP was created.
    pause
    exit /b 1
)
cmake --build --preset win-amd64-release --target aegis_wing aegis_wing_installer
if errorlevel 1 (
    echo.
    echo Build failed. No release ZIP was created.
    pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Make-Release.ps1" -BuildPreset win-amd64-release
pause
