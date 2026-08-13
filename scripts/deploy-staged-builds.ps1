# Deploys staged builds (chat history mod) once Warcraft II and the Studio
# app are closed. Started in the background right after a build while the
# game is still running; safe to re-run manually.
param(
    [string]$NativeStage = "$env:TEMP\war2stage-hist",
    [string]$AppStage = "$env:TEMP\war2stage-app"
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$modNative = Join-Path $repo 'mod\native'
$appBin = Join-Path $repo 'app\PlayerColorStudio\bin\Release\net10.0-windows'
$installNative = 'C:\Program Files (x86)\Warcraft II Remastered\x86\Mods\PlayerColorStudio\mod\native'
$log = Join-Path $env:TEMP 'war2-deploy-staged.log'

function Log([string]$msg) {
    "$(Get-Date -Format 'HH:mm:ss') $msg" | Out-File -FilePath $log -Append -Encoding utf8
}

Log "deploy waiting: game/app to close (native=$NativeStage app=$AppStage)"
while (Get-Process 'Warcraft II', 'PlayerColorStudio' -ErrorAction SilentlyContinue) {
    Start-Sleep -Seconds 5
}
Log 'game and app closed'

Get-Process AllyLeaveWatch -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

Copy-Item (Join-Path $NativeStage '*') $modNative -Force
Log "native copied to $modNative"
try {
    Copy-Item (Join-Path $NativeStage '*') $installNative -Force
    Log "native copied to $installNative"
} catch {
    Log "install-copy skipped: $($_.Exception.Message)"
}

try {
    Copy-Item (Join-Path $AppStage '*') $appBin -Recurse -Force
    Log "app copied to $appBin"
} catch {
    Log "app-copy failed: $($_.Exception.Message)"
}

Start-Process (Join-Path $modNative 'AllyLeaveWatch.exe') -WorkingDirectory $modNative
Log 'watcher restarted — done'
