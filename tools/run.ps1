# Script per avviare MyOS 64-bit su QEMU
$ROOT = Split-Path -Parent $PSScriptRoot
$IMG = if (Test-Path "$ROOT\build\myos.img") { "$ROOT\build\myos.img" } else { Join-Path $ROOT "myos.img" }

if (-not (Test-Path $IMG)) {
    Write-Host "[!] myos.img non trovato. Esecuzione build..." -ForegroundColor Yellow
    node "$PSScriptRoot\build.js"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERRORE] Compilazione fallita." -ForegroundColor Red
        exit 1
    }
}

# Ricerca binario QEMU
$qemuCmd = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
$qemu = $null
if ($qemuCmd) {
    $qemu = $qemuCmd.Source
} else {
    $candidates = @(
        "$PSScriptRoot\qemu-system-x86_64.exe",
        "$PSScriptRoot\qemu\qemu-system-x86_64.exe",
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Microsoft\WinGet\Links\qemu-system-x86_64.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $qemu = $c
            break
        }
    }
}

if (-not $qemu) {
    Write-Host "[!] QEMU non trovato. Installazione via winget..." -ForegroundColor Yellow
    winget install SoftwareFreedomConservancy.QEMU --accept-source-agreements --accept-package-agreements --silent
    if (Test-Path "C:\Program Files\qemu\qemu-system-x86_64.exe") {
        $qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
    }
}

if (-not $qemu) {
    Write-Host "[ERRORE] Impossibile trovare qemu-system-x86_64." -ForegroundColor Red
    exit 1
}

Write-Host "=== Avvio NewOS V1 64-bit su QEMU ($qemu) ===" -ForegroundColor Cyan
& $qemu -drive format=raw,file="$IMG" -m 256M -vga std -audiodev dsound,id=snd0 -device AC97,audiodev=snd0 -boot c
