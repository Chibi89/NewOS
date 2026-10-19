@echo off
cd /d "%~dp0"
powershell -ExecutionPolicy Bypass -File "%~dp0tools\run_vbox.ps1"
pause
