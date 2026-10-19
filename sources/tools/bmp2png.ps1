Add-Type -AssemblyName System.Drawing
$src = if ($args[0]) { $args[0] } else { "$PSScriptRoot\..\screenshot.bmp" }
$dst = if ($args[1]) { $args[1] } else { "$PSScriptRoot\..\screenshot.png" }
$bytes = [System.IO.File]::ReadAllBytes($src)
$ms = New-Object System.IO.MemoryStream(,$bytes)
$bmp = [System.Drawing.Bitmap]::FromStream($ms)
$bmp.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$ms.Dispose()
Write-Host "PNG Saved to $dst!"
