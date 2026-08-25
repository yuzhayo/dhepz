@echo off
setlocal
cd /d "%~dp0"

where pwsh >nul 2>&1
if errorlevel 1 echo ERROR: PowerShell 7 ^(pwsh^) tidak ditemukan di PATH.
if errorlevel 1 pause
if errorlevel 1 exit /b 1

pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Create-PR.ps1"
if errorlevel 1 echo.
if errorlevel 1 echo Proses gagal. Repository tidak di-reset.

echo.
pause
endlocal
