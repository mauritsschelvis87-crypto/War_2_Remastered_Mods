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
    throw 'Installer files are incomplete. Download the complete Quality of Life Modding release.'
}
if (!(Test-Path -LiteralPath (Join-Path $GameRootPath 'x86\Data'))) {
    throw "Warcraft II Remastered was not found at: $GameRootPath"
}
if (!(Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '.NET SDK 10 is required to build this source installer. Use the packaged release installer instead.'
}

try {
    $nativeBuild = Join-Path $sourceRoot 'native\AllyLeaveHook\build.ps1'
    if (Test-Path -LiteralPath $nativeBuild) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $nativeBuild | Write-Host
        if ($LASTEXITCODE -ne 0) { throw 'The Extra hook native build failed.' }
    }

    & dotnet publish $projectFile --configuration Release --output (Join-Path $stage 'app') | Write-Host
    if ($LASTEXITCODE -ne 0) { throw 'The Quality of Life Modding app build failed.' }

    New-Item -ItemType Directory -Path $appTarget, $modTarget -Force | Out-Null
    Copy-Item -Path (Join-Path $stage 'app\*') -Destination $appTarget -Recurse -Force
    Copy-Item -Path (Join-Path $engineSource '*') -Destination $modTarget -Recurse -Force

    $watch = Join-Path $modTarget 'native\AllyLeaveWatch.exe'
    $extraConfig = Join-Path $modTarget 'extra-features.json'
    if ((Test-Path -LiteralPath $watch) -and (Test-Path -LiteralPath $extraConfig) -and
        ((Get-Content -LiteralPath $extraConfig -Raw) -match '"AllyLeaveRedNames"\s*:\s*true')) {
        Start-Process -FilePath $watch -ArgumentList '--install-startup' -WorkingDirectory (Split-Path $watch -Parent) -WindowStyle Hidden
        Write-Host 'Extra watcher registered for Windows startup (Extra was already ON).'
    }

    if (!$NoLaunch) {
        Start-Process -FilePath (Join-Path $appTarget 'PlayerColorStudio.exe') -WorkingDirectory $appTarget
    }
    Write-Host "Installed Quality of Life Modding: $installRoot"
    Write-Host 'Tip: turn Extra ON once in the app to auto-inject on every game launch (no need to keep the app open).'
}
finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
