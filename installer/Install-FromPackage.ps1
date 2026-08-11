param(
    [string]$GameRootPath = '',
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-GameRoot([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path)) { return $false }
    return Test-Path -LiteralPath (Join-Path $path 'x86\Data')
}

$packageRoot = $PSScriptRoot
$payloadRoot = Join-Path $packageRoot 'payload'
$payloadApp = Join-Path $payloadRoot 'app'
$payloadMod = Join-Path $payloadRoot 'mod'
$defaultGameRoot = 'C:\Program Files (x86)\Warcraft II Remastered'

if (!(Test-Path -LiteralPath (Join-Path $payloadApp 'PlayerColorStudio.exe')) -or
    !(Test-Path -LiteralPath (Join-Path $payloadMod 'Apply-PlayerColors.ps1'))) {
    throw 'Installer package is incomplete. Extract the full zip and run Install.bat from that folder.'
}

if ([string]::IsNullOrWhiteSpace($GameRootPath)) {
    $GameRootPath = $defaultGameRoot
}

if (!(Test-GameRoot $GameRootPath)) {
    Write-Host "Warcraft II Remastered was not found at:`n  $GameRootPath"
    Write-Host ''
    $GameRootPath = Read-Host 'Enter the full path to your Warcraft II Remastered folder'
    if (!(Test-GameRoot $GameRootPath)) {
        throw "Warcraft II Remastered was not found at: $GameRootPath"
    }
}

$programFiles = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
$needsElevation = $GameRootPath.StartsWith($programFiles, [StringComparison]::OrdinalIgnoreCase)
if ($needsElevation -and !(Test-IsAdministrator)) {
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -GameRootPath `"$GameRootPath`""
    if ($NoLaunch) { $arguments += ' -NoLaunch' }
    $p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -ArgumentList $arguments
    exit $p.ExitCode
}

$installRoot = Join-Path $GameRootPath 'x86\Mods\PlayerColorStudio'
$appTarget = Join-Path $installRoot 'app'
$modTarget = Join-Path $installRoot 'mod'

New-Item -ItemType Directory -Path $appTarget, $modTarget -Force | Out-Null
Copy-Item -Path (Join-Path $payloadApp '*') -Destination $appTarget -Recurse -Force

# Fresh mod payload only (engine + natives). Keep any existing local configs/backups.
Get-ChildItem -LiteralPath $modTarget -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne 'backup' -and $_.Name -notmatch '^(player-colors|extra-features|studio-settings)\.json$' } |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item -Path (Join-Path $payloadMod '*') -Destination $modTarget -Recurse -Force

$exe = Join-Path $appTarget 'PlayerColorStudio.exe'
if (!(Test-Path -LiteralPath $exe)) {
    throw 'Install failed: PlayerColorStudio.exe is missing after copy.'
}

if (!$NoLaunch) {
    Start-Process -FilePath $exe -WorkingDirectory $appTarget
}

Write-Host ''
Write-Host "Installed Quality of Life Modding:"
Write-Host "  $installRoot"
Write-Host ''
Write-Host 'Open the app, set your colors/options, then press Apply.'
Write-Host 'Restart Warcraft II Remastered so changes load.'
