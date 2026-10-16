@echo off
setlocal

cd /d "%~dp0"

where pwsh.exe >nul 2>nul
if errorlevel 1 (
  echo [dhepz] FAILED: pwsh.exe tidak ditemukan.
  pause
  exit /b 1
)

pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Bump-Build-Run.ps1"
if errorlevel 1 (
  echo [dhepz] FAILED. Lihat error di atas.
  pause
  exit /b 1
)

echo [dhepz] Done.
exit /b 0
