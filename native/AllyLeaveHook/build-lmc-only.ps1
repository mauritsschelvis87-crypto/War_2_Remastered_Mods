$ErrorActionPreference = 'Stop'
$here = 'C:\Users\mauri\Projects\war_2mod\native\AllyLeaveHook'
$out = 'C:\Users\mauri\Projects\war_2mod\mod\native'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
$buildDll = Join-Path $out 'LobbyMapClickHook.build.dll'
$finalDll = Join-Path $out 'LobbyMapClickHook.dll'
$studioDll = 'C:\Program Files (x86)\Warcraft II Remastered\x86\Mods\PlayerColorStudio\mod\native\LobbyMapClickHook.dll'
$cmd = @"
call "$vcvars" x86 >nul
cl /nologo /O2 /W3 /EHsc /LD "$here\LobbyMapClickHook.cpp" /Fe:"$buildDll" /link /nologo /DLL /OUT:"$buildDll" User32.lib Shell32.lib
if errorlevel 1 exit /b 1
del /q "$here\*.obj" 2>nul
exit /b 0
"@
$temp = Join-Path $env:TEMP ('build-lmc-' + [guid]::NewGuid().ToString('N') + '.cmd')
Set-Content -Path $temp -Value $cmd -Encoding ASCII
$p = Start-Process -FilePath $env:ComSpec -ArgumentList '/c', "`"$temp`"" -WorkingDirectory $here -Wait -PassThru -NoNewWindow
Remove-Item $temp -Force -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0) { throw "cl failed $($p.ExitCode)" }
foreach ($dst in @($finalDll, $studioDll)) {
  try {
    Copy-Item -LiteralPath $buildDll -Destination $dst -Force
    Write-Host "swapped: $dst"
  } catch {
    Write-Host "LOCKED: $dst"
  }
}
Get-Item $buildDll, $finalDll, $studioDll | Format-Table FullName, Length, LastWriteTime -AutoSize
