param(
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path (Split-Path (Split-Path $here -Parent) -Parent) 'mod\native'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    throw 'MSVC x86 toolset not found. Install Visual Studio C++ desktop build tools.'
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$cmd = @"
call "$vcvars" x86 >nul
cl /nologo /O2 /W3 /EHsc /LD "$here\AllyLeaveHook.cpp" /Fe:"$OutDir\AllyLeaveHook.dll" /link /nologo /DLL /OUT:"$OutDir\AllyLeaveHook.dll"
if errorlevel 1 exit /b 1
cl /nologo /O2 /W3 /EHsc /LD "$here\PauseChatHook.cpp" /Fe:"$OutDir\PauseChatHook.dll" /link /nologo /DLL /OUT:"$OutDir\PauseChatHook.dll"
if errorlevel 1 exit /b 1
cl /nologo /O2 /W3 /EHsc "$here\InjectAllyLeave.cpp" /Fe:"$OutDir\InjectAllyLeave.exe" /link /nologo
if errorlevel 1 exit /b 1
cl /nologo /O2 /W3 /EHsc "$here\InjectPauseChat.cpp" /Fe:"$OutDir\InjectPauseChat.exe" /link /nologo
if errorlevel 1 exit /b 1
cl /nologo /O2 /W3 /EHsc "$here\AllyLeaveWatch.cpp" /Fe:"$OutDir\AllyLeaveWatch.exe" /link /nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup Advapi32.lib
if errorlevel 1 exit /b 1
del /q *.obj 2>nul
exit /b 0
"@

$temp = Join-Path $env:TEMP ('build-allyleave-' + [guid]::NewGuid().ToString('N') + '.cmd')
Set-Content -Path $temp -Value $cmd -Encoding ASCII
try {
    $p = Start-Process -FilePath $env:ComSpec -ArgumentList '/c', "`"$temp`"" -WorkingDirectory $here -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) { throw "Native build failed with exit code $($p.ExitCode)." }
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path $here -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $OutDir -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $OutDir -Filter '*.exp' -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $OutDir -Filter '*.lib' -ErrorAction SilentlyContinue | Remove-Item -Force
}

Write-Host "Built: $OutDir\AllyLeaveHook.dll"
Write-Host "Built: $OutDir\PauseChatHook.dll"
Write-Host "Built: $OutDir\InjectAllyLeave.exe"
Write-Host "Built: $OutDir\InjectPauseChat.exe"
Write-Host "Built: $OutDir\AllyLeaveWatch.exe"
