@echo off
title NewOS V1 - SYSTEM.PAK Manager & Security Audit
cd /d "%~dp0"

cls
echo ===============================================================================
echo            NewOS V1 - SYSTEM.PAK Package & Security Manager
echo ===============================================================================
echo.
echo  Cartella Staging:  SystemPackFileWach\
echo  Chiave Crittografica: AES-256-CBC (Rijndael FIPS-197)
echo.
echo  [1] Estrai SYSTEM.PAK in SystemPackFileWach (Unpack & Decrypt)
echo  [2] Verifica Integrita' & Checksum (Audit Anti-Manomissione)
echo  [3] Re-Impacchetta e Cifra in AES-256 (Pack & Encrypt)
echo  [4] Avvia Modalita' Watcher (Monitoraggio modifiche in tempo reale)
echo  [5] Apri Interfaccia Grafica Web (GUI nel Browser)
echo  [0] Esci
echo.
set /p opt="Seleziona opzione [1-5]: "

if "%opt%"=="1" (
    cls
    node tools\system_pack_mgr.js unpack
    echo.
    pause
    goto end
)
if "%opt%"=="2" (
    cls
    node tools\system_pack_mgr.js verify
    echo.
    pause
    goto end
)
if "%opt%"=="3" (
    cls
    node tools\system_pack_mgr.js pack
    echo.
    pause
    goto end
)
if "%opt%"=="4" (
    cls
    node tools\system_pack_mgr.js watch
    goto end
)
if "%opt%"=="5" (
    cls
    node tools\system_pack_mgr.js gui
    goto end
)

:end
