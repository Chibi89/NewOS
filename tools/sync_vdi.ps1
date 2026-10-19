$VBOX = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
$ROOT = Split-Path -Parent $PSScriptRoot
$IMG  = if (Test-Path "$ROOT\build\myos.img") { "$ROOT\build\myos.img" } else { "$ROOT\myos.img" }
$BUILD_VDI = "$ROOT\build\myos.vdi"
$VDI  = "$ROOT\myos.vdi"

if (Test-Path $VBOX) {
    try {
        & $VBOX storageattach "myos" --storagectl "IDE" --port 0 --device 0 --medium none 2>&1 | Out-Null
        & $VBOX closemedium disk $VDI 2>&1 | Out-Null
        & $VBOX closemedium disk $BUILD_VDI 2>&1 | Out-Null
    } catch {}

    if (Test-Path $BUILD_VDI) {
        Remove-Item $BUILD_VDI -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path $VDI) {
        Remove-Item $VDI -Force -ErrorAction SilentlyContinue
    }

    & $VBOX convertfromraw $IMG $BUILD_VDI --format VDI
    Copy-Item $BUILD_VDI $VDI -Force -ErrorAction SilentlyContinue
    try {
        & $VBOX storageattach "myos" --storagectl "IDE" --port 0 --device 0 --type hdd --medium $VDI 2>&1 | Out-Null
    } catch {}
    Write-Host "[OK] build\myos.vdi generato e sincronizzato per Oracle VirtualBox!"
}
