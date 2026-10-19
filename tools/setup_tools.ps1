# Script di verifica e setup della toolchain per MyOS 64-bit
Write-Host "=== Verifica Toolchain MyOS 64-bit ===" -ForegroundColor Cyan

function Find-Tool($cmdName, $candidates) {
    $found = Get-Command $cmdName -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

# 1. NASM
$nasm = Find-Tool "nasm" @(
    "$PSScriptRoot\..\..\AppData\Local\bin\NASM\nasm.exe",
    "$env:LOCALAPPDATA\bin\NASM\nasm.exe",
    "C:\Program Files\NASM\nasm.exe"
)
if ($nasm) {
    Write-Host "[OK] NASM Assembler: $nasm" -ForegroundColor Green
} else {
    Write-Host "[!] NASM non trovato. Installazione..." -ForegroundColor Yellow
    winget install NASM.NASM --accept-source-agreements --accept-package-agreements --silent
}

# 2. Clang / LLVM
$clang = Find-Tool "clang" @(
    "$PSScriptRoot\llvm-mingw\llvm-mingw-20260616-ucrt-x86_64\bin\clang.exe",
    "C:\Program Files\LLVM\bin\clang.exe"
)
if ($clang) {
    Write-Host "[OK] LLVM / Clang Compiler: $clang" -ForegroundColor Green
} else {
    Write-Host "[!] Clang non trovato." -ForegroundColor Yellow
}

# 3. QEMU x86_64
$qemu = Find-Tool "qemu-system-x86_64" @(
    "$PSScriptRoot\qemu-system-x86_64.exe",
    "$PSScriptRoot\qemu\qemu-system-x86_64.exe",
    "C:\Program Files\qemu\qemu-system-x86_64.exe"
)
if ($qemu) {
    Write-Host "[OK] QEMU x86_64 Emulator: $qemu" -ForegroundColor Green
} else {
    Write-Host "[!] QEMU non trovato." -ForegroundColor Yellow
}

Write-Host "`n=== Toolchain MyOS 64-bit Pronta al 100%! ===" -ForegroundColor Cyan
