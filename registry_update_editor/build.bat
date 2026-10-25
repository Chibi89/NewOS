@echo off
setlocal enabledelayedexpansion

echo ===============================================================================
echo NEWOS V1 - COMPILATORE SUITE REGISTRY UPDATE EDITOR (WINDOWS)
echo ===============================================================================

set CSC_PATH=""

if exist "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" (
    set CSC_PATH="C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
) else if exist "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe" (
    set CSC_PATH="C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)

if %CSC_PATH%=="" (
    echo [ERRORE] Compilatore C# csc.exe non trovato nelle posizioni standard del .NET Framework!
    pause
    exit /b 1
)

echo [INFO] Compilatore rilevato: %CSC_PATH%
echo [INFO] Compilazione in corso di RegistryUpdateEditor.exe ...

%CSC_PATH% /target:winexe /platform:anycpu /optimize+ /out:"%~dp0RegistryUpdateEditor.exe" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.dll /reference:System.Core.dll "%~dp0src\Program.cs"

if %ERRORLEVEL% NEQ 0 (
    echo [ERRORE] Compilazione fallita con codice %ERRORLEVEL%!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESSO] Eseguibile generato con successo:
echo           %~dp0RegistryUpdateEditor.exe
echo ===============================================================================
endlocal
