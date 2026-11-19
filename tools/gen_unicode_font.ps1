param(
    [string]$OutPath = (Join-Path $PSScriptRoot "..\Resources\Fonts\UNICODE.FNT")
)

Add-Type -AssemblyName System.Drawing

$outDir = Split-Path -Parent $OutPath
if (!(Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# Blocchi Unicode BMP da rasterizzare (pagina da 256 codepoint ciascuno)
# 0x00: Latin-1, 0x01: Latin Ext-A, 0x03: Greco, 0x04: Cirillico,
# 0x06: Arabo, 0x09: Devanagari/Bengalese, 0x20: Punteggiatura/Valute,
# 0x21: Frecce/Simboli, 0x26: Simboli vari, 0x27: Dingbats, 0x30: Hiragana/Katakana
$blocks = @(0x00, 0x01, 0x03, 0x04, 0x06, 0x09, 0x20, 0x21, 0x26, 0x27, 0x30)

$bmp = New-Object System.Drawing.Bitmap 12, 18
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$font = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$brush = [System.Drawing.Brushes]::White
$sf = [System.Drawing.StringFormat]::GenericTypographic

$headerSize = 8 + (256 * 4)
$blockOffsets = New-Object uint32[] 256
$blockDataList = New-Object System.Collections.Generic.List[byte[]]

$curOffset = [uint32]$headerSize
foreach ($blk in $blocks) {
    $blockOffsets[$blk] = $curOffset
    $buf = New-Object byte[] (256 * 16)
    for ($i = 0; $i -lt 256; $i++) {
        $cp = ($blk -shl 8) + $i
        if ($cp -lt 0x20 -or ($cp -ge 0x7F -and $cp -le 0x9F)) { continue }
        $ch = [char]$cp
        $g.Clear([System.Drawing.Color]::Black)
        $g.DrawString($ch.ToString(), $font, $brush, 0, 0, $sf)
        for ($y = 0; $y -lt 16; $y++) {
            [byte]$row = 0
            for ($x = 0; $x -lt 8; $x++) {
                $px = $bmp.GetPixel($x, $y)
                if ($px.R -gt 96 -or $px.G -gt 96 -or $px.B -gt 96) {
                    $row = $row -bor (1 -shl (7 - $x))
                }
            }
            $buf[($i * 16) + $y] = $row
        }
    }
    $blockDataList.Add($buf)
    $curOffset += [uint32]($buf.Length)
}

$fs = [System.IO.File]::Create($OutPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("UFNT"))
$bw.Write([uint32]$blocks.Count)
for ($b = 0; $b -lt 256; $b++) {
    $bw.Write([uint32]$blockOffsets[$b])
}
foreach ($bd in $blockDataList) {
    $bw.Write($bd)
}
$bw.Close()
$fs.Close()
$g.Dispose()
$bmp.Dispose()
$font.Dispose()

Write-Host "[OK] Generato font Unicode $OutPath ($curOffset byte, $($blocks.Count) blocchi)"
