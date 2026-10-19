Add-Type -AssemblyName System.Drawing

$srcPath = (Resolve-Path "loginscreen\loginscreen.jpg").Path
$bmp = New-Object System.Drawing.Bitmap($srcPath)

# Check top right quadrant: x > 1100, y < 250
$minX = 9999; $maxX = 0; $minY = 9999; $maxY = 0;
for ($y = 0; $y -lt 300; $y++) {
    for ($x = 1100; $x -lt $bmp.Width; $x++) {
        $c = $bmp.GetPixel($x, $y)
        if ($c.R -gt 180 -and $c.G -gt 180 -and $c.B -gt 180) {
            if ($x -lt $minX) { $minX = $x }
            if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }
            if ($y -gt $maxY) { $maxY = $y }
        }
    }
}
Write-Host "Logo top-right: X=[$minX, $maxX], Y=[$minY, $maxY]"

# Check particle cloud bounding box across whole image
$pMinX = 9999; $pMaxX = 0; $pMinY = 9999; $pMaxY = 0;
for ($y = 0; $y -lt $bmp.Height; $y += 4) {
    for ($x = 0; $x -lt $bmp.Width; $x += 4) {
        $c = $bmp.GetPixel($x, $y)
        if ($c.R -gt 60 -or $c.G -gt 60 -or $c.B -gt 60) {
            if ($x -lt $pMinX) { $pMinX = $x }
            if ($x -gt $pMaxX) { $pMaxX = $x }
            if ($y -lt $pMinY) { $pMinY = $y }
            if ($y -gt $pMaxY) { $pMaxY = $y }
        }
    }
}
Write-Host "Particle cloud: X=[$pMinX, $pMaxX], Y=[$pMinY, $pMaxY]"

$bmp.Dispose()
