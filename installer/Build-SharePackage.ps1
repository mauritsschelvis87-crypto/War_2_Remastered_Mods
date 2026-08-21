param(
    [string]$OutDir = '',
    [string]$DesktopCopyName = 'QoL Modding Setup.zip',
    # Reuse the binaries already in mod\native (skip the native rebuild).
    [switch]$SkipNativeBuild,
    # Take native dll/exe payload from this folder instead of mod\native
    # (e.g. the staging folder while the game still locks mod\native).
    [string]$NativeSource = ''
)

$ErrorActionPreference = 'Stop'

$installerDir = $PSScriptRoot
$sourceRoot = Split-Path -Parent $installerDir
$projectFile = Join-Path $sourceRoot 'app\PlayerColorStudio\PlayerColorStudio.csproj'
$setupProject = Join-Path $installerDir 'SetupApp\SetupApp.csproj'
$engineSource = Join-Path $sourceRoot 'mod'
$nativeBuild = Join-Path $sourceRoot 'native\AllyLeaveHook\build.ps1'
$setupIcon = Join-Path $installerDir 'SetupApp\app.ico'
$installScript = Join-Path $installerDir 'Install-FromPackage.ps1'
$installBat = Join-Path $installerDir 'package\Install.bat'
$setupPayloadZip = Join-Path $installerDir 'SetupApp\payload.zip'

if (!(Test-Path -LiteralPath $projectFile)) { throw "Project not found: $projectFile" }
if (!(Test-Path -LiteralPath $setupProject)) { throw "Setup project not found: $setupProject" }
if (!(Test-Path -LiteralPath $engineSource)) { throw "Mod engine not found: $engineSource" }
if (!(Test-Path -LiteralPath $nativeBuild)) { throw "Native build script not found: $nativeBuild" }
if (!(Test-Path -LiteralPath $setupIcon)) { throw "Setup icon not found: $setupIcon" }
if (!(Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '.NET SDK is required to build the share package.'
}

$versionNode = Select-Xml -Path $projectFile -XPath '//Version' | Select-Object -First 1
$version = if ($versionNode) { $versionNode.Node.InnerText.Trim() } else { '1.0.0' }
if ([string]::IsNullOrWhiteSpace($version)) { $version = '1.0.0' }

$packageName = "QoL-Modding-$version"
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $sourceRoot 'dist'
}
$stageRoot = Join-Path $OutDir $packageName
$payloadApp = Join-Path $stageRoot 'payload\app'
$payloadMod = Join-Path $stageRoot 'payload\mod'
$payloadNative = Join-Path $payloadMod 'native'
$zipPath = Join-Path $OutDir ($packageName + '.zip')
$setupOut = Join-Path $OutDir ('QoL-Modding-Setup-' + $version + '.exe')
$setupZipOut = Join-Path $OutDir ('QoL-Modding-Setup-' + $version + '.zip')
$publishTemp = Join-Path ([IO.Path]::GetTempPath()) ('qol-publish-' + [guid]::NewGuid().ToString('N'))
$setupPublish = Join-Path ([IO.Path]::GetTempPath()) ('qol-setup-' + [guid]::NewGuid().ToString('N'))
$desktop = [Environment]::GetFolderPath('Desktop')

Write-Host "Building share package $packageName ..."

try {
    if ($SkipNativeBuild) {
        Write-Host 'Skipping native build (using existing mod\native binaries).'
    } else {
        Write-Host 'Building native Extra hooks...'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $nativeBuild | Write-Host
        if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
    }

    Write-Host 'Publishing self-contained PlayerColorStudio (win-x64)...'
    New-Item -ItemType Directory -Path $publishTemp -Force | Out-Null
    & dotnet publish $projectFile `
        --configuration Release `
        --runtime win-x64 `
        --self-contained true `
        -p:PublishSingleFile=true `
        -p:IncludeNativeLibrariesForSelfExtract=true `
        --output $publishTemp | Write-Host
    if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed.' }

    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $payloadApp, $payloadNative -Force | Out-Null

    Copy-Item -Path (Join-Path $publishTemp '*') -Destination $payloadApp -Recurse -Force
    Get-ChildItem -LiteralPath $payloadApp -Filter '*.pdb' -Recurse -ErrorAction SilentlyContinue |
        Remove-Item -Force
    Copy-Item -LiteralPath $setupIcon -Destination (Join-Path $payloadApp 'app.ico') -Force
    Copy-Item -LiteralPath (Join-Path $engineSource 'Apply-PlayerColors.ps1') -Destination $payloadMod -Force
    $assetSource = Join-Path $engineSource 'assets'
    if (Test-Path -LiteralPath $assetSource) {
        Copy-Item -LiteralPath $assetSource -Destination $payloadMod -Recurse -Force
    }

    $nativeSource = if ([string]::IsNullOrWhiteSpace($NativeSource)) {
        Join-Path $engineSource 'native'
    } else {
        $NativeSource
    }
    Write-Host "Native payload from: $nativeSource"
    Get-ChildItem -LiteralPath $nativeSource -File -ErrorAction Stop |
        Where-Object { $_.Extension -in '.dll', '.exe' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $payloadNative -Force }

    if (Test-Path -LiteralPath $installScript) {
        Copy-Item -LiteralPath $installScript -Destination (Join-Path $stageRoot 'Install.ps1') -Force
    }
    if (Test-Path -LiteralPath $installBat) {
        Copy-Item -LiteralPath $installBat -Destination (Join-Path $stageRoot 'Install.bat') -Force
    }

    $readme = @"
Warcraft II Remastered — Quality of Life Mods
Version $version

Easiest install
---------------
1. Extract this zip.
2. Run "QoL Modding Setup.exe"
3. Accept the Administrator prompt, choose your game folder if asked, press Install.

After install
-------------
1. In the app: change colors or options, press Apply.
2. Restart Warcraft II Remastered.

Default install location
------------------------
  <Warcraft II Remastered>\x86\Mods\PlayerColorStudio

Notes
-----
- No Visual Studio or .NET SDK required.
- First Apply creates a local vanilla backup from your game files.
- Run Battle.net Scan and Repair first if game files may already be modified.
"@
    Set-Content -LiteralPath (Join-Path $stageRoot 'README.txt') -Value $readme -Encoding UTF8

    Write-Host 'Packing payload for Setup.exe...'
    if (Test-Path -LiteralPath $setupPayloadZip) { Remove-Item -LiteralPath $setupPayloadZip -Force }
    Compress-Archive -Path (Join-Path $stageRoot 'payload\app'), (Join-Path $stageRoot 'payload\mod') `
        -DestinationPath $setupPayloadZip -CompressionLevel Optimal

    Write-Host 'Publishing Setup.exe (self-contained, with icon)...'
    New-Item -ItemType Directory -Path $setupPublish -Force | Out-Null
    & dotnet publish $setupProject `
        --configuration Release `
        --runtime win-x64 `
        --self-contained true `
        -p:PublishSingleFile=true `
        -p:IncludeNativeLibrariesForSelfExtract=true `
        --output $setupPublish | Write-Host
    if ($LASTEXITCODE -ne 0) { throw 'Setup publish failed.' }

    $builtSetup = Join-Path $setupPublish 'QoL Modding Setup.exe'
    if (!(Test-Path -LiteralPath $builtSetup)) {
        throw "Setup exe not found at $builtSetup"
    }

    Copy-Item -LiteralPath $builtSetup -Destination $setupOut -Force

    # Zip Setup + README for sharing (much smaller than the raw .exe).
    $zipStage = Join-Path ([IO.Path]::GetTempPath()) ('qol-setup-zip-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $zipStage -Force | Out-Null
    try {
        Copy-Item -LiteralPath $builtSetup -Destination (Join-Path $zipStage 'QoL Modding Setup.exe') -Force
        Copy-Item -LiteralPath (Join-Path $stageRoot 'README.txt') -Destination (Join-Path $zipStage 'README.txt') -Force
        if (Test-Path -LiteralPath $setupZipOut) { Remove-Item -LiteralPath $setupZipOut -Force }
        Compress-Archive -Path (Join-Path $zipStage '*') -DestinationPath $setupZipOut -CompressionLevel Optimal
    }
    finally {
        Remove-Item -LiteralPath $zipStage -Recurse -Force -ErrorAction SilentlyContinue
    }

    $desktopTarget = Join-Path $desktop $DesktopCopyName
    Copy-Item -LiteralPath $setupZipOut -Destination $desktopTarget -Force
    $oldDesktopExe = Join-Path $desktop 'QoL Modding Setup.exe'
    if (Test-Path -LiteralPath $oldDesktopExe) { Remove-Item -LiteralPath $oldDesktopExe -Force }

    # Folder package gets the zip (not the huge uncompressed exe).
    Copy-Item -LiteralPath $setupZipOut -Destination (Join-Path $stageRoot $DesktopCopyName) -Force

    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path $stageRoot -DestinationPath $zipPath -CompressionLevel Optimal

    $setupZipMb = [math]::Round((Get-Item -LiteralPath $setupZipOut).Length / 1MB, 1)
    Write-Host ''
    Write-Host "Share this zip:     $setupZipOut  ($setupZipMb MB)"
    Write-Host "Desktop copy:       $desktopTarget"
    Write-Host "Setup exe (local):  $setupOut"
    Write-Host "Package folder:     $stageRoot"
    Write-Host "Full package zip:   $zipPath"
}
finally {
    if (Test-Path -LiteralPath $publishTemp) {
        Remove-Item -LiteralPath $publishTemp -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $setupPublish) {
        Remove-Item -LiteralPath $setupPublish -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $setupPayloadZip) {
        Remove-Item -LiteralPath $setupPayloadZip -Force -ErrorAction SilentlyContinue
    }
}
