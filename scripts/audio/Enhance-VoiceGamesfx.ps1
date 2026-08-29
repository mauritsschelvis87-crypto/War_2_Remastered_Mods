# Re-master all unit voice WAVs from vanilla backup into mod/assets/audio/voice.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $repoRoot

$gameRoot = 'C:\Program Files (x86)\Warcraft II Remastered'
if (Test-Path $gameRoot) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'mod\Apply-PlayerColors.ps1') `
        -SyncVoiceGamesfxBackup -GameRootPath $gameRoot
}

py -3 -m pip install -q -r (Join-Path $repoRoot 'scripts\audio\requirements.txt')
py -3 (Join-Path $repoRoot 'scripts\audio\enhance_voice_gamesfx.py')
