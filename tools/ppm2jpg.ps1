Add-Type -AssemblyName System.Drawing

function Convert-PpmToJpg($ppm, $jpg) {
    if (-not (Test-Path $ppm)) { Write-Host "No PPM: $ppm"; return }
    $bytes = [System.IO.File]::ReadAllBytes($ppm)
    $idx = 0
    while ($idx -lt 100 -and $bytes[$idx] -ne 10) { $idx++ }
    $idx++
    while ($idx -lt 100 -and $bytes[$idx] -eq 35) {
        while ($bytes[$idx] -ne 10) { $idx++ }
        $idx++
    }
    $dimStr = ""
    while ($bytes[$idx] -ne 10) { $dimStr += [char]$bytes[$idx]; $idx++ }
    $idx++
    $dims = $dimStr.Trim().Split(" ")
    $w = [int]$dims[0]
    $h = [int]$dims[1]
    while ($bytes[$idx] -ne 10) { $idx++ }
    $idx++
    Write-Host "PPM dims: $w x $h"

    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $stride = $data.Stride
    $scan0 = $data.Scan0
    $dstBuf = New-Object byte[] ($stride * $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $srcOff = $idx + ($y * $w + $x) * 3
            $dstOff = ($y * $stride) + ($x * 3)
            if ($srcOff + 2 -lt $bytes.Length) {
                $dstBuf[$dstOff]     = $bytes[$srcOff + 2] # B
                $dstBuf[$dstOff + 1] = $bytes[$srcOff + 1] # G
                $dstBuf[$dstOff + 2] = $bytes[$srcOff + 0] # R
            }
        }
    }
    [System.Runtime.InteropServices.Marshal]::Copy($dstBuf, 0, $scan0, $dstBuf.Length)
    $bmp.UnlockBits($data)
    $bmp.Save($jpg, [System.Drawing.Imaging.ImageFormat]::Jpeg)
    $bmp.Dispose()
    Write-Host "Convertito: $jpg"
}

Convert-PpmToJpg "screenshot_bootscreen.ppm" "screenshot_bootscreen.jpg"
Convert-PpmToJpg "screenshot_loginscreen.ppm" "screenshot_loginscreen.jpg"
