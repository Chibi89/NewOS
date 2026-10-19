@echo off
setlocal
cd /d "%~dp0"
title NewOS V1 - Visualizzatore Ufficiale Regole (.NOR)

if "%~1"=="" (
    start "" "%~dp0NewOS_Rules_Viewer.exe"
) else (
    start "" "%~dp0NewOS_Rules_Viewer.exe" "%~1"
)
