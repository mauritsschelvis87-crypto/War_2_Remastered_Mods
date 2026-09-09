# Always build + run the newest PlayerColorStudio from this repo.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $repo 'app\PlayerColorStudio\PlayerColorStudio.csproj'
if (-not (Test-Path $project)) {
    $repo = 'c:\Users\mauri\Projects\war_2mod'
    $project = Join-Path $repo 'app\PlayerColorStudio\PlayerColorStudio.csproj'
}
$outDir = Join-Path $repo 'app\PlayerColorStudio\bin\Release\net10.0-windows'

Write-Host 'Building latest Quality of Life Modding...'
dotnet build $project -c Release --nologo -v q
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$exe = Join-Path $outDir 'PlayerColorStudio.exe'
if (-not (Test-Path $exe)) { throw "Missing $exe" }

# Studio finds mod\ by walking up from BaseDirectory — Release bin works with repo layout.
Start-Process -FilePath $exe -WorkingDirectory $outDir
