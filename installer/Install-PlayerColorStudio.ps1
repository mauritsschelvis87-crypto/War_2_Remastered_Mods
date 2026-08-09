param(
    [string]$GameRootPath = 'C:\Program Files (x86)\Warcraft II Remastered',
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$programFiles = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
$needsElevation = $GameRootPath.StartsWith($programFiles, [StringComparison]::OrdinalIgnoreCase)
if ($needsElevation -and !(Test-IsAdministrator)) {
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -GameRootPath `"$GameRootPath`""
    Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList $arguments
    exit $LASTEXITCODE
}

$sourceRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $sourceRoot 'app\PlayerColorStudio\PlayerColorStudio.csproj'
$engineSource = Join-Path $sourceRoot 'mod'
$installRoot = Join-Path $GameRootPath 'x86\Mods\PlayerColorStudio'
$appTarget = Join-Path $installRoot 'app'
$modTarget = Join-Path $installRoot 'mod'
$stage = Join-Path ([IO.Path]::GetTempPath()) ('PlayerColorStudio-' + [guid]::NewGuid().ToString('N'))

if (!(Test-Path -LiteralPath $projectFile) -or !(Test-Path -LiteralPath $engineSource)) {
    throw 'Installer files are incomplete. Download the complete Player Color Studio release.'
}
if (!(Test-Path -LiteralPath (Join-Path $GameRootPath 'x86\Data'))) {
    throw "Warcraft II Remastered was not found at: $GameRootPath"
}
if (!(Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '.NET SDK 10 is required to build this source installer. Use the packaged release installer instead.'
}

try {
    & dotnet publish $projectFile --configuration Release --output (Join-Path $stage 'app') | Write-Host
    if ($LASTEXITCODE -ne 0) { throw 'The Player Color Studio app build failed.' }

    New-Item -ItemType Directory -Path $appTarget, $modTarget -Force | Out-Null
    Copy-Item -Path (Join-Path $stage 'app\*') -Destination $appTarget -Recurse -Force
    Copy-Item -Path (Join-Path $engineSource '*') -Destination $modTarget -Recurse -Force

    if (!$NoLaunch) {
        Start-Process -FilePath (Join-Path $appTarget 'PlayerColorStudio.exe') -WorkingDirectory $appTarget
    }
    Write-Host "Installed Player Color Studio: $installRoot"
}
finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
