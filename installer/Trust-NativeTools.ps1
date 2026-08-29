# Unblock Mark-of-the-Web and allow Defender to run our unsigned game hooks.
# Does NOT disable Windows Security globally — only this install folder.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot
)

$ErrorActionPreference = 'SilentlyContinue'

function Unblock-Tree([string]$root) {
    if (!(Test-Path -LiteralPath $root)) { return }
    Get-ChildItem -LiteralPath $root -Recurse -File -Force |
        Unblock-File -ErrorAction SilentlyContinue
}

Unblock-Tree $InstallRoot

$native = Join-Path $InstallRoot 'mod\native'
$app = Join-Path $InstallRoot 'app'
$paths = @($InstallRoot, $native, $app) | Where-Object { Test-Path -LiteralPath $_ }
$processes = @(
    'AllyLeaveWatch.exe',
    'InjectPauseChat.exe',
    'InjectAllyLeave.exe',
    'InjectChatNameColor.exe',
    'InjectDragSelect.exe',
    'InjectUnitColor.exe',
    'InjectObserve.exe',
    'PlayerColorStudio.exe'
)

try {
    foreach ($p in $paths) {
        Add-MpPreference -ExclusionPath $p -ErrorAction SilentlyContinue
    }
    foreach ($exe in $processes) {
        Add-MpPreference -ExclusionProcess $exe -ErrorAction SilentlyContinue
    }
    Write-Host 'Windows Defender: allowed Quality of Life Modding hooks in this install folder.'
} catch {
    Write-Host 'Could not add Defender exclusions (need Administrator). Unblocked downloaded files anyway.'
}
