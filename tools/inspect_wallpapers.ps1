Add-Type -AssemblyName System.Drawing

$files = @(
    "$PSScriptRoot\..\resources\wallpaper\Windows\img0.jpg",
    "$PSScriptRoot\..\resources\wallpaper\ThemeA\img20.jpg"
)

foreach ($f in $files) {
    if (Test-Path $f) {
        $img = [System.Drawing.Image]::FromFile($f)
        Write-Host "$([System.IO.Path]::GetFileName($f)): $($img.Width)x$($img.Height)"
        $img.Dispose()
    }
}
