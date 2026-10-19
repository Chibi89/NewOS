# Script per compilare, aggiornare il disco VDI e avviare NewOS V1 su Oracle VirtualBox
$ErrorActionPreference = "Continue"
$ROOT = Split-Path -Parent $PSScriptRoot
$IMG = if (Test-Path "$ROOT\build\myos.img") { "$ROOT\build\myos.img" } else { Join-Path $ROOT "myos.img" }
$VDI = if (Test-Path "$ROOT\build\myos.vdi") { "$ROOT\build\myos.vdi" } else { Join-Path $ROOT "myos.vdi" }
$ISO = if (Test-Path "$ROOT\build\myos.iso") { "$ROOT\build\myos.iso" } else { Join-Path $ROOT "myos.iso" }
$VBOXMANAGE = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"

if (-not (Test-Path $VBOXMANAGE)) {
    $vboxCmd = Get-Command "VBoxManage" -ErrorAction SilentlyContinue
    if ($vboxCmd) {
        $VBOXMANAGE = $vboxCmd.Source
    } else {
        Write-Host "[ERRORE] Oracle VirtualBox non trovato in C:\Program Files\Oracle\VirtualBox." -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== 1. Compilazione NewOS V1 64-bit ===" -ForegroundColor Cyan
node "$PSScriptRoot\build.js"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERRORE] Compilazione fallita." -ForegroundColor Red
    exit 1
}

# Verifica esistenza VM "myos"
$vmList = & "$VBOXMANAGE" list vms
$vmExists = $vmList -match '"myos"'

Write-Host "=== 2. Aggiornamento supporti per VirtualBox ===" -ForegroundColor Cyan
if ($vmExists) {
    $info = & "$VBOXMANAGE" showvminfo "myos" --machinereadable
    if ($info -match 'VMState="running"') {
        Write-Host "Spegnimento della VM in corso..."
        & "$VBOXMANAGE" controlvm "myos" poweroff 2>$null
        Start-Sleep -Seconds 1
    }
    # Scollega le unità per consentire la ri-registrazione pulita
    & "$VBOXMANAGE" storageattach "myos" --storagectl "IDE" --port 0 --device 0 --medium none 2>$null
    & "$VBOXMANAGE" storageattach "myos" --storagectl "IDE" --port 1 --device 0 --medium none 2>$null
}

# Rimuovi e deregistra il vecchio VDI dal database VirtualBox se registrato
& "$VBOXMANAGE" closemedium disk "$VDI" 2>$null
& "$VBOXMANAGE" closemedium disk "$ROOT\build\myos.vdi" 2>$null
& "$VBOXMANAGE" closemedium disk "$ROOT\myos.vdi" 2>$null

# Se la VM non esiste ancora, creala
if (-not $vmExists) {
    Write-Host "=== 3. Creazione VM 'myos' in VirtualBox ===" -ForegroundColor Cyan
    & "$VBOXMANAGE" createvm --name "myos" --ostype "Other_64" --register
    & "$VBOXMANAGE" storagectl "myos" --name "IDE" --add ide --controller PIIX4
}

# Configura impostazioni hardware ottimali per NewOS V1 64-bit
Write-Host "=== 4. Configurazione parametri VM (64-bit, 256MB RAM, VBoxVGA, PS/2 Mouse) ===" -ForegroundColor Cyan
& "$VBOXMANAGE" modifyvm "myos" `
    --memory 256 `
    --vram 32 `
    --boot1 disk --boot2 dvd --boot3 none --boot4 none `
    --graphicscontroller vboxvga `
    --mouse ps2 `
    --keyboard ps2 `
    --pae on `
    --longmode on `
    --audio-controller ac97 `
    --audio-enabled on `
    --audio-out on

# Collega il disco fisso al canale IDE Master (Port 0, Device 0)
& "$VBOXMANAGE" storageattach "myos" --storagectl "IDE" --port 0 --device 0 --type hdd --medium "$VDI"

# Se presente, collega anche l'immagine ISO al lettore CD/DVD (Port 1, Device 0)
if (Test-Path $ISO) {
    & "$VBOXMANAGE" storageattach "myos" --storagectl "IDE" --port 1 --device 0 --type dvddrive --medium "$ISO"
}

Write-Host "=== 5. Avvio NewOS V1 su Oracle VirtualBox ===" -ForegroundColor Green
& "$VBOXMANAGE" startvm "myos"
