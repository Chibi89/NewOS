Add-Type -AssemblyName System.Drawing

function Convert-ScreenImage($srcPath, $dstBmpPath, $dstJpgPath) {
    if (-not (Test-Path $srcPath)) {
        Write-Host "File non trovato: $srcPath"
        return
    }
    $img = [System.Drawing.Bitmap]::FromFile($srcPath)
    Write-Host "Caricato: $srcPath ($($img.Width)x$($img.Height))"

    $out = New-Object System.Drawing.Bitmap(1024, 768, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($out)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality

    $g.DrawImage($img, 0, 0, 1024, 768)
    $g.Dispose()
    $img.Dispose()

    # Salva BMP a 24bpp (compatibile al 100% con il decoder BMP del kernel)
    $out.Save($dstBmpPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
    Write-Host "Salvato BMP: $dstBmpPath ($((Get-Item $dstBmpPath).Length) byte)"

    # Salva JPEG con alta qualita 92
    $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
    $encoderParams = New-Object System.Drawing.Imaging.EncoderParameters(1)
    $encoderParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]92)
    $out.Save($dstJpgPath, $codec, $encoderParams)
    Write-Host "Salvato JPG: $dstJpgPath ($((Get-Item $dstJpgPath).Length) byte)"

    $out.Dispose()
}

Convert-ScreenImage "loginscreen\loginscreen.jpg" "loginscreen\loginscreen_1024.bmp" "loginscreen\loginscreen_1024.jpg"
Convert-ScreenImage "logoutscreen\logoutscreen.jpg" "logoutscreen\logoutscreen_1024.bmp" "logoutscreen\logoutscreen_1024.jpg"
