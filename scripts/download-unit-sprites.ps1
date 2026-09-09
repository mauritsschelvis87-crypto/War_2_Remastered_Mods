# Downloads WC2 unit map sprites (MIT: jcfieldsdev/warcraft2-map-editor) for offline editor use.
param(
    [int]$MaxUnitId = 105,
    [string[]]$Eras = @('forest', 'winter', 'wasteland', 'swamp')
)

$base = 'https://raw.githubusercontent.com/jcfieldsdev/warcraft2-map-editor/master/www/units'
$dest = Join-Path $env:LOCALAPPDATA 'War2ContentStudio\unit-sprites'
$ok = 0
$fail = 0

foreach ($era in $Eras) {
    $dir = Join-Path $dest $era
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    for ($i = 0; $i -le $MaxUnitId; $i++) {
        $name = '{0:D4}.png' -f $i
        $url = "$base/$era/$name"
        $out = Join-Path $dir $name
        if (Test-Path $out) { $ok++; continue }
        try {
            Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing | Out-Null
            $ok++
        } catch {
            $fail++
        }
    }
}

Write-Host "Cached under $dest (ok=$ok fail=$fail)"
