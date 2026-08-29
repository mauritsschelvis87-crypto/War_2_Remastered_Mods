# Open side-by-side vanilla vs enhanced voice compare in your browser.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $repoRoot
py -3 (Join-Path $repoRoot 'scripts\audio\compare_voice_samples.py')
