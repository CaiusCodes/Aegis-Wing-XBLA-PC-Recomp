@echo off
setlocal

set "installer=%~dp0tools\Install-AegisWing.ps1"

if not exist "%installer%" (
    echo The Aegis Wing installer files could not be found.
    pause
    exit /b 1
)

start "" powershell.exe -NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File "%installer%"
exit /b 0
