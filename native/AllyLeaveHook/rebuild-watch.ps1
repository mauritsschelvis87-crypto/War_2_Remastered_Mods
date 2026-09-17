$ErrorActionPreference = 'Stop'
Get-Process AllyLeaveWatch -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$here = 'C:\Users\mauri\Projects\war_2mod\native\AllyLeaveHook'
$out = 'C:\Users\mauri\Projects\war_2mod\mod\native'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
$exe = Join-Path $out 'AllyLeaveWatch.exe'
$cmd = @"
call "$vcvars" x86 >nul
cl /nologo /O2 /W3 /EHsc "$here\AllyLeaveWatch.cpp" /Fe:"$exe" /link /nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup Advapi32.lib Shell32.lib User32.lib
if errorlevel 1 exit /b 1
del /q "$here\*.obj" 2>nul
exit /b 0
"@
$temp = Join-Path $env:TEMP ('build-watch-' + [guid]::NewGuid().ToString('N') + '.cmd')
Set-Content -Path $temp -Value $cmd -Encoding ASCII
$p = Start-Process -FilePath $env:ComSpec -ArgumentList '/c', "`"$temp`"" -WorkingDirectory $here -Wait -PassThru -NoNewWindow
Remove-Item $temp -Force -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0) { throw "cl failed $($p.ExitCode)" }
Write-Host "built $exe"
Start-Process -FilePath $exe
Start-Sleep -Seconds 1
Get-Process AllyLeaveWatch | Format-Table Id, Path -AutoSize
