# One-time install: copies app + vanilla/enhanced audio locally, puts launcher on Desktop.
$ErrorActionPreference = 'Stop'

$Desktop = [Environment]::GetFolderPath('Desktop')
$AppDir = Join-Path $env:LOCALAPPDATA 'War2VoiceCompare'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ModRoot = Join-Path $RepoRoot 'mod'
$VanillaSrc = Join-Path $ModRoot 'backup\vanilla\x86\Data\Gamesfx'
$EnhancedSrc = Join-Path $ModRoot 'assets\audio\voice'
$SkipDirs = @('Misc', 'Spells', 'Bldg')

Write-Host 'War2 Voice Compare — installer'
Write-Host "App: $AppDir"

# Remove old desktop folder if present
$OldLab = Join-Path $Desktop 'War2VoiceLab'
if (Test-Path -LiteralPath $OldLab) {
    Write-Host "Removing old folder: $OldLab"
    Remove-Item -LiteralPath $OldLab -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'voice_compare.py') -Destination (Join-Path $AppDir 'voice_compare.py') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'unit_presets.py') -Destination (Join-Path $AppDir 'unit_presets.py') -Force
$examplePresets = Join-Path $PSScriptRoot 'unit-presets'
if (Test-Path -LiteralPath $examplePresets) {
    $presetDest = Join-Path $env:LOCALAPPDATA 'War2VoiceCompare\unit-presets'
    New-Item -ItemType Directory -Force -Path $presetDest | Out-Null
    Get-ChildItem -LiteralPath $examplePresets -Filter '*.json' | ForEach-Object {
        $dest = Join-Path $presetDest $_.Name
        if (!(Test-Path -LiteralPath $dest)) {
            Copy-Item -LiteralPath $_.FullName -Destination $dest -Force
        }
    }
}

function Copy-VoiceTree {
    param([string]$Source, [string]$Dest)
    if (!(Test-Path -LiteralPath $Source)) {
        throw "Source missing: $Source"
    }
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    $count = 0
    Get-ChildItem -LiteralPath $Source -Directory | ForEach-Object {
        if ($SkipDirs -contains $_.Name) { return }
        $outDir = Join-Path $Dest $_.Name
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        Get-ChildItem -LiteralPath $_.FullName -Filter '*.wav' -File | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $outDir $_.Name) -Force
            $count++
        }
    }
    return $count
}

$vanillaDest = Join-Path $AppDir 'audio\vanilla'
$enhancedDest = Join-Path $AppDir 'audio\enhanced'
$replacerDest = Join-Path $AppDir 'replacer'

Write-Host 'Copying vanilla samples…'
$vc = Copy-VoiceTree $VanillaSrc $vanillaDest
Write-Host "  $vc files"

Write-Host 'Copying enhanced samples…'
$ec = Copy-VoiceTree $EnhancedSrc $enhancedDest
Write-Host "  $ec files"

New-Item -ItemType Directory -Force -Path $replacerDest | Out-Null

$Launcher = Join-Path $Desktop 'War2 Content Lab.bat'
@(
    '@echo off'
    'title War2 Content Lab'
    "cd /d `"$AppDir`""
    'REM Close stale servers on wrong port'
    'for /f "tokens=5" %%a in (''netstat -ano ^| findstr ":8766.*LISTENING"'') do taskkill /F /PID %%a >nul 2>&1'
    'for /f "tokens=5" %%a in (''netstat -ano ^| findstr ":8799.*LISTENING"'') do taskkill /F /PID %%a >nul 2>&1'
    'py -3 voice_compare.py'
    'if errorlevel 1 pause'
) | Set-Content -LiteralPath $Launcher -Encoding ASCII

$OldLauncher = Join-Path $Desktop 'War2 Voice Compare.bat'
if (Test-Path -LiteralPath $OldLauncher) { Remove-Item -LiteralPath $OldLauncher -Force }

Write-Host ''
Write-Host 'Done.'
Write-Host "Double-click on Desktop: War2 Content Lab.bat"
Write-Host 'Tabs: Audio (voices) · Units (custom heroes for Content Studio)'
Write-Host 'Re-run this installer to refresh enhanced audio from the mod.'
