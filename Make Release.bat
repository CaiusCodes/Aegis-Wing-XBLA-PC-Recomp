@echo off
setlocal
rem The presets build the SDK in-tree (rexglue-sdk\). If it is missing, run tools\Get-Sdk.ps1.
if not exist "%~dp0rexglue-sdk\CMakeLists.txt" (
    echo.
    echo rexglue-sdk is empty. Run tools\Get-Sdk.ps1 first, then run this file again.
    pause
    exit /b 1
)
cmake --preset win-amd64-release
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
