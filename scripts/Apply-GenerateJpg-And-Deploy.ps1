# Pull latest from GitHub on Maurits laptop and publish Studio to Desktop.
$ErrorActionPreference = 'Stop'
$repo = 'C:\Users\mauri\Projects\war_2mod'
$desktop = Join-Path $env:USERPROFILE 'Desktop\Quality of Life Modding 1.0.6-beta'

Set-Location $repo
git fetch origin
git pull --ff-only origin main

# Rebuild AllyLeaveWatch if build script exists
$watchBuild = Join-Path $repo 'native\AllyLeaveHook\rebuild-watch.ps1'
if (Test-Path $watchBuild) {
  & $watchBuild
} elseif (Test-Path (Join-Path $repo 'native\AllyLeaveHook\build.ps1')) {
  & (Join-Path $repo 'native\AllyLeaveHook\build.ps1')
}

dotnet publish (Join-Path $repo 'app\PlayerColorStudio\PlayerColorStudio.csproj') -c Release -o $desktop
Write-Host "Published to $desktop"

# Restart AllyLeaveWatch if running
Get-Process AllyLeaveWatch -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$watchExe = Join-Path $desktop 'AllyLeaveWatch.exe'
if (-not (Test-Path $watchExe)) {
  $watchExe = Join-Path $repo 'mod\native\AllyLeaveWatch.exe'
}
if (Test-Path $watchExe) {
  Start-Process $watchExe
  Write-Host "Started AllyLeaveWatch from $watchExe"
}
