Add-Type -AssemblyName System.Drawing

function Make-1024Wallpaper($src, $dstBmp, $dstJpg) {
    if (-not (Test-Path $src)) { return }
    $img = [System.Drawing.Bitmap]::FromFile($src)
    $out = New-Object System.Drawing.Bitmap(1024, 768, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($out)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($img, 0, 0, 1024, 768)
    $g.Dispose()
    $img.Dispose()

    if ($dstBmp) {
        $out.Save($dstBmp, [System.Drawing.Imaging.ImageFormat]::Bmp)
        Write-Host "[OK] Salvato BMP: $dstBmp ($((Get-Item $dstBmp).Length) byte)"
    }
    if ($dstJpg) {
        $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
        $encoderParams = New-Object System.Drawing.Imaging.EncoderParameters(1)
        $encoderParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]90)
        $out.Save($dstJpg, $codec, $encoderParams)
        Write-Host "[OK] Salvato JPG: $dstJpg ($((Get-Item $dstJpg).Length) byte)"
    }
    $out.Dispose()
}

$wpDir = "$PSScriptRoot\..\resources\wallpaper"
Make-1024Wallpaper "$wpDir\Windows\img0.jpg" "$wpDir\HERO.BMP" "$wpDir\HERO.JPG"
Make-1024Wallpaper "$wpDir\ThemeB\img24.jpg" "$wpDir\FLOW.BMP" "$wpDir\FLOW.JPG"
Make-1024Wallpaper "$wpDir\ThemeA\img20.jpg" "$wpDir\THEME_A.BMP" "$wpDir\THEME_A.JPG"

# Generazione BLISS.BMP (1024x768)
$blissBmp = "$wpDir\BLISS.BMP"
$blissJpg = "$wpDir\BLISS.JPG"
$out = New-Object System.Drawing.Bitmap(1024, 768, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g = [System.Drawing.Graphics]::FromImage($out)
$skyBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.Point(0, 0)),
    (New-Object System.Drawing.Point(0, 500)),
    [System.Drawing.Color]::FromArgb(0, 100, 210),
    [System.Drawing.Color]::FromArgb(140, 200, 255)
)
$g.FillRectangle($skyBrush, 0, 0, 1024, 500)
$skyBrush.Dispose()

# Colline verdi Bliss
$hillBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.Point(0, 480)),
    (New-Object System.Drawing.Point(0, 768)),
    [System.Drawing.Color]::FromArgb(85, 175, 45),
    [System.Drawing.Color]::FromArgb(35, 100, 20)
)
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$path.AddCurve(@(
    (New-Object System.Drawing.Point(0, 520)),
    (New-Object System.Drawing.Point(300, 470)),
    (New-Object System.Drawing.Point(700, 510)),
    (New-Object System.Drawing.Point(1024, 460)),
    (New-Object System.Drawing.Point(1024, 768)),
    (New-Object System.Drawing.Point(0, 768))
))
$g.FillPath($hillBrush, $path)
$path.Dispose()
$hillBrush.Dispose()

# Nuvole bianche stilizzate
$cloudBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(160, 255, 255, 255))
$g.FillEllipse($cloudBrush, 150, 160, 260, 60)
$g.FillEllipse($cloudBrush, 200, 130, 180, 70)
$g.FillEllipse($cloudBrush, 650, 220, 220, 50)
$g.FillEllipse($cloudBrush, 700, 195, 150, 55)
$cloudBrush.Dispose()
$g.Dispose()

$out.Save($blissBmp, [System.Drawing.Imaging.ImageFormat]::Bmp)
Write-Host "[OK] Salvato BMP: $blissBmp"
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
$encoderParams = New-Object System.Drawing.Imaging.EncoderParameters(1)
$encoderParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]90)
$out.Save($blissJpg, $codec, $encoderParams)
Write-Host "[OK] Salvato JPG: $blissJpg"
$out.Dispose()
