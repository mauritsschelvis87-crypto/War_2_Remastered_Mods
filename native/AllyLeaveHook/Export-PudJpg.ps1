# Export a Warcraft II .pud minimap as a .jpg (create/overwrite).
# Builds a 1px-per-tile minimap, then nearest-neighbor upscales for a sharp overlay.
param(
    [Parameter(Mandatory = $true)]
    [string]$PudPath,
    [string]$OutputPath,
    [string]$PalettePath,
    [int]$TargetSize = 1280,
    [int]$JpegQuality = 92
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Get-DefaultTileColors {
    return @(
        0x00, 0xB7, 0xB5, 0xAA, 0x46, 0x97, 0x96, 0x92, 0x3B,
        0x6F, 0x6E, 0x6F, 0x6E, 0xB6, 0xB6, 0x88, 0x72, 0xAA,
        0x97, 0x91, 0x6F, 0x6E
    )
}

function Get-DefaultPlayerColors { return @(0xA8, 0xD4, 0xD8, 0xDC, 0xE0, 0xEA, 0xFF, 0xC8) }

function Read-PudSections([byte[]]$bytes) {
    $sections = @{}
    $offset = 0
    while (($offset + 8) -le $bytes.Length) {
        $name = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $size = [BitConverter]::ToInt32($bytes, $offset + 4)
        $dataStart = $offset + 8
        $dataEnd = $dataStart + $size
        if ($size -lt 0 -or $dataEnd -gt $bytes.Length) { break }
        $data = New-Object byte[] $size
        if ($size -gt 0) { [Array]::Copy($bytes, $dataStart, $data, 0, $size) }
        $sections[$name] = @{ Size = $size; Data = $data }
        $offset = $dataEnd
    }
    return $sections
}

function Get-TileColor([int]$tileValue, [int[]]$tileColors) {
    if ($tileValue -lt 0xD0) { $index = $tileValue -shr 4 }
    else { $index = ($tileValue -shr 8) + 0x0C }
    if ($index -gt 0x15) { $index = 0 }
    return $tileColors[$index]
}

function Set-SafePixel([int[]]$pixels, [int]$width, [int]$height, [int]$x, [int]$y, [int]$color) {
    if ($x -lt 0 -or $y -lt 0 -or $x -ge $width -or $y -ge $height) { return }
    $pixels[($y * $width) + $x] = $color
}

function Draw-Mine([int[]]$pixels, [int]$width, [int]$height, [int]$xPos, [int]$yPos, [int]$resource, [int]$color) {
    $d = 1
    if ($resource -ge 50000) { $d++ }
    $xPos++; $yPos++
    for ($y = ($yPos - $d); $y -le ($yPos + $d); $y++) {
        for ($x = ($xPos - $d); $x -le ($xPos + $d); $x++) {
            Set-SafePixel $pixels $width $height $x $y $color
        }
    }
}

function Draw-Start([int[]]$pixels, [int]$width, [int]$height, [int]$x, [int]$y, [int]$player, [int[]]$playerColors) {
    $c = $playerColors[$player % 8]
    Set-SafePixel $pixels $width $height $x $y $c
    Set-SafePixel $pixels $width $height ($x - 1) ($y - 1) 0
    Set-SafePixel $pixels $width $height ($x + 1) ($y + 1) 0
    Set-SafePixel $pixels $width $height ($x + 1) ($y - 1) 0
    Set-SafePixel $pixels $width $height ($x - 1) ($y + 1) 0
    Set-SafePixel $pixels $width $height ($x - 1) $y $c
    Set-SafePixel $pixels $width $height ($x + 1) $y $c
    Set-SafePixel $pixels $width $height $x ($y - 1) $c
    Set-SafePixel $pixels $width $height $x ($y + 1) $c
}

function Resolve-PalettePath([string]$explicit, [hashtable]$sections) {
    if (![string]::IsNullOrWhiteSpace($explicit) -and (Test-Path -LiteralPath $explicit)) {
        return $explicit
    }
    $era = 0
    if ($sections.ContainsKey('ERA ') -and $sections['ERA '].Data.Length -ge 2) {
        $era = [BitConverter]::ToUInt16($sections['ERA '].Data, 0)
    }
    $root = 'C:\Program Files (x86)\Warcraft II Remastered\x86\Data\Art\bgs'
    $rel = switch ($era) {
        1 { 'Winter\winter.ppl' }
        2 { 'Wasteland\wasteland.ppl' }
        3 { 'Swamp\swamp.ppl' }
        default { 'Forest\forest.ppl' }
    }
    $path = Join-Path $root $rel
    if (Test-Path -LiteralPath $path) { return $path }
    $fallback = Join-Path $root 'Forest\forest.ppl'
    if (Test-Path -LiteralPath $fallback) { return $fallback }
    throw "Palette not found under $root"
}

function Import-PaletteRgb([string]$palettePath) {
    $bytes = [IO.File]::ReadAllBytes($palettePath)
    $colors = New-Object System.Drawing.Color[] 256
    for ($i = 0; $i -lt 256; $i++) {
        $off = $i * 3
        if (($off + 2) -ge $bytes.Length) { break }
        $r = [int][math]::Round($bytes[$off] * 255 / 63)
        $g = [int][math]::Round($bytes[$off + 1] * 255 / 63)
        $b = [int][math]::Round($bytes[$off + 2] * 255 / 63)
        $colors[$i] = [System.Drawing.Color]::FromArgb(255, $r, $g, $b)
    }
    return $colors
}

function Save-JpegHighQuality([System.Drawing.Bitmap]$bmp, [string]$path, [int]$quality) {
    $quality = [Math]::Max(1, [Math]::Min(100, $quality))
    $codecs = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders()
    $jpegCodec = $codecs | Where-Object { $_.MimeType -eq 'image/jpeg' } | Select-Object -First 1
    if (!$jpegCodec) {
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Jpeg)
        return
    }
    $eps = New-Object System.Drawing.Imaging.EncoderParameters 1
    $eps.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter (
        [System.Drawing.Imaging.Encoder]::Quality, [long]$quality)
    $bmp.Save($path, $jpegCodec, $eps)
    $eps.Dispose()
}

if (!(Test-Path -LiteralPath $PudPath)) { throw "PUD not found: $PudPath" }

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = [IO.Path]::ChangeExtension($PudPath, '.jpg')
}

$tileColors = Get-DefaultTileColors
$playerColors = Get-DefaultPlayerColors
$goldColor = 0xFB
$oilColor = 0x00

$bytes = [IO.File]::ReadAllBytes($PudPath)
$sections = Read-PudSections $bytes
if (!$sections.ContainsKey('DIM ') -or !$sections.ContainsKey('MTXM')) {
    throw 'Invalid PUD: missing DIM or MTXM.'
}

$mapSize = [BitConverter]::ToUInt16($sections['DIM '].Data, 0)
if ($mapSize -le 0) { throw 'Invalid PUD map size.' }

if ([string]::IsNullOrWhiteSpace($PalettePath)) {
    $PalettePath = Resolve-PalettePath '' $sections
} elseif (!(Test-Path -LiteralPath $PalettePath)) {
    $PalettePath = Resolve-PalettePath '' $sections
}

# Flat buffer: one palette index per tile (fast in PowerShell).
$pixels = New-Object int[] ($mapSize * $mapSize)
$mtx = $sections['MTXM'].Data
for ($y = 0; $y -lt $mapSize; $y++) {
    $row = $y * $mapSize
    for ($x = 0; $x -lt $mapSize; $x++) {
        $tileValue = [BitConverter]::ToUInt16($mtx, ($row + $x) * 2)
        $pixels[$row + $x] = Get-TileColor $tileValue $tileColors
    }
}

if ($sections.ContainsKey('UNIT')) {
    $unitData = $sections['UNIT'].Data
    $unitCount = [int][math]::Floor($unitData.Length / 8)
    for ($i = 0; $i -lt $unitCount; $i++) {
        $off = $i * 8
        $x = [BitConverter]::ToUInt16($unitData, $off)
        $y = [BitConverter]::ToUInt16($unitData, $off + 2)
        $type = $unitData[$off + 4]
        $owner = $unitData[$off + 5]
        $ai = [BitConverter]::ToUInt16($unitData, $off + 6)
        switch ($type) {
            0x5C { Draw-Mine $pixels $mapSize $mapSize $x $y ($ai * 2500) $goldColor }
            0x5D { Draw-Mine $pixels $mapSize $mapSize $x $y ($ai * 2500) $oilColor }
            0x5E { Draw-Start $pixels $mapSize $mapSize $x $y $owner $playerColors }
            0x5F { Draw-Start $pixels $mapSize $mapSize $x $y $owner $playerColors }
        }
    }
}

$palette = Import-PaletteRgb $PalettePath
$srcBmp = New-Object System.Drawing.Bitmap $mapSize, $mapSize, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$rect = New-Object System.Drawing.Rectangle 0, 0, $mapSize, $mapSize
$bmpData = $srcBmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $srcBmp.PixelFormat)
try {
    $stride = $bmpData.Stride
    $buf = New-Object byte[] ($stride * $mapSize)
    for ($y = 0; $y -lt $mapSize; $y++) {
        $rowBytes = $y * $stride
        $rowPix = $y * $mapSize
        for ($x = 0; $x -lt $mapSize; $x++) {
            $index = $pixels[$rowPix + $x]
            if ($index -lt 0 -or $index -ge 256) { $index = 0 }
            $c = $palette[$index]
            $i = $rowBytes + ($x * 4)
            $buf[$i] = $c.B
            $buf[$i + 1] = $c.G
            $buf[$i + 2] = $c.R
            $buf[$i + 3] = 255
        }
    }
    [Runtime.InteropServices.Marshal]::Copy($buf, 0, $bmpData.Scan0, $buf.Length)
} finally {
    $srcBmp.UnlockBits($bmpData)
}

$outBmp = $srcBmp
if ($TargetSize -gt 0 -and ($mapSize -ne $TargetSize)) {
    $outBmp = New-Object System.Drawing.Bitmap $TargetSize, $TargetSize, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($outBmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $g.Clear([System.Drawing.Color]::Black)
    $g.DrawImage($srcBmp, 0, 0, $TargetSize, $TargetSize)
    $g.Dispose()
    $srcBmp.Dispose()
}

$dir = Split-Path -Parent $OutputPath
if ($dir -and !(Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

try {
    Save-JpegHighQuality $outBmp $OutputPath $JpegQuality
} catch {
    $fallbackDir = Join-Path $env:TEMP 'War2MapPreviews'
    New-Item -ItemType Directory -Path $fallbackDir -Force | Out-Null
    $safe = [IO.Path]::GetFileNameWithoutExtension($PudPath) -replace '[^\w\-. ]', '_'
    $OutputPath = Join-Path $fallbackDir ($safe + '.jpg')
    Save-JpegHighQuality $outBmp $OutputPath $JpegQuality
}

$outBmp.Dispose()
Write-Output $OutputPath
