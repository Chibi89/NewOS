@echo off
cd /d "%~dp0"
powershell -ExecutionPolicy Bypass -File "%~dp0tools\run.ps1"
pause
