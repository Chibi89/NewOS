Add-Type -AssemblyName System.Drawing

function Convert-Wallpaper($srcPath, $dstPath) {
    if (-not (Test-Path $srcPath)) {
        Write-Host "[SKIP] $srcPath non trovato." -ForegroundColor Yellow
        return
    }

    Write-Host "Elaborazione Center-Crop di $([System.IO.Path]::GetFileName($srcPath))..."
    $orig = [System.Drawing.Bitmap]::FromFile($srcPath)
    $ow = $orig.Width
    $oh = $orig.Height

    $targetAspect = 1024.0 / 768.0
    $srcAspect    = [double]$ow / [double]$oh

    if ($srcAspect -gt $targetAspect) {
        $cropH = $oh
        $cropW = [int]($oh * $targetAspect)
        $cropX = [int](($ow - $cropW) / 2)
        $cropY = 0
    } else {
        $cropW = $ow
        $cropH = [int]($ow / $targetAspect)
        $cropX = 0
        $cropY = [int](($oh - $cropH) / 2)
    }

    $scaled = New-Object System.Drawing.Bitmap(1024, 768, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gfx = [System.Drawing.Graphics]::FromImage($scaled)
    $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $gfx.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $gfx.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

    $dstRect = New-Object System.Drawing.Rectangle(0, 0, 1024, 768)
    $srcRect = New-Object System.Drawing.Rectangle($cropX, $cropY, $cropW, $cropH)
    $gfx.DrawImage($orig, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)

    $gfx.Dispose()
    $orig.Dispose()

    $rect = New-Object System.Drawing.Rectangle(0, 0, 1024, 768)
    $bmpData = $scaled.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
    $len = 1024 * 768 * 4
    $bytes = New-Object byte[] $len
    [System.Runtime.InteropServices.Marshal]::Copy($bmpData.Scan0, $bytes, 0, $len)
    $scaled.UnlockBits($bmpData)
    $scaled.Dispose()

    [System.IO.File]::WriteAllBytes($dstPath, $bytes)
    Write-Host "[OK] Wallpaper salvato: $([System.IO.Path]::GetFileName($dstPath)) ($len byte)" -ForegroundColor Green
}

Convert-Wallpaper "$PSScriptRoot\..\resources\wallpaper\Windows\img0.jpg" "$PSScriptRoot\..\resources\wallpaper_hero.raw"
Convert-Wallpaper "$PSScriptRoot\..\resources\wallpaper\ThemeA\img20.jpg" "$PSScriptRoot\..\resources\wallpaper_flow.raw"
