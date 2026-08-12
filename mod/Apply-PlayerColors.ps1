# Warcraft II Remastered - Quality of Life Modding (minimap + ally screen)
param(
    [string]$GameRootPath = 'C:\Program Files (x86)\Warcraft II Remastered',
    [switch]$ApplySavedConfigOnly,
    [switch]$ApplyDragSelectFromExtra,
    [switch]$RestoreOnly,
    [switch]$SyncVanillaBackup,
    [switch]$GetDefaultConfig
)

$ErrorActionPreference = 'Stop'

# Player 8 still unsupported: minimap table uses idx 2 (shared); no safe exclusive band yet.
# See scripts/research/p2p8-colors-found.md
$DisabledPlayerIndices = @(7)   # Player 8 only (0-based)

# Authentic Remastered display colors for disabled players (UI only; not patched).
$DisabledPlayerDisplayHex = @{
    7 = '#FFF759'   # Player 8 shared band 188
}

# Palette indices that feed those authentic colors (used when backup is available)
$DisabledPlayerDisplayIndices = @{
    7 = 188   # Player 8 shared
}

# Unit/team remap bands (exe table @ 0x008425F8: D0 D4 D8 DC E0 E4 E8).
# Applied to both .ppl and mapColors sync for supported players.
$PlayerColorIndices = @{
    0 = @(208, 209, 210, 211)
    1 = @(212, 213, 214, 215)   # P2 unit band only
    2 = @(216, 217, 218, 219)
    3 = @(220, 221, 222, 223)
    4 = @(224, 225, 226, 227)
    5 = @(228, 229, 230, 231)
    6 = @(232, 233, 234, 235, 255)   # P7 includes minimap idx 255
}

# Minimap player-dot indices from exe table @ 0x008C8D84: D0 01 D8 DC E0 E4 FF 02
# P2 = 1, P8 = 2. These are .ppl-only (mapColors.bin low bytes are a different format).
$PlayerMinimapPplOnlyIndices = @{
    1 = @(1)
}

# Self highlight green = palette index 250 (own units, buildings, drag box, related UI).
# Enemy highlight = index 249 (also used by some build UI — customize carefully).
# Ally highlight = index 251 (Other-colors ally slot; was previously "shared yellow").
# Critter minimap = index 247 (dots only; selection outline uses Ally / 251).
# Gold mine highlight = indices 236, 237, 238 (resource gold band).
# Oil patch highlight = index 246 (resource selection outline path uses 0xF6).
$SelectionHighlightPaletteIndex = 250
$EnemySelectionHighlightPaletteIndex = 249
$AllyHighlightPaletteIndex = 251
$CritterHighlightPaletteIndex = 247
$BuildStageRedPaletteIndices = @(201, 202)
$GoldMineHighlightPaletteIndices = @(236, 237, 238)
$OilPatchHighlightPaletteIndex = 246

# Enemy highlight shares palette index 249 with some build UI chrome.
$EnemySelectionHighlightUiDisabled = $false
$EnemySelectionDisplayHex = '#FF0000'

# First palette index per player, used to read/write JSON defaults from authentic backup.
# P2 defaults from minimap slot 1 (#0094FC); P8 still shared 188.
$VanillaJsonColorIndices = @(208, 1, 216, 220, 224, 228, 232, 188)

$PplFiles = @(
    'x86\Data\Art\bgs\Forest\forest.ppl',
    'x86\Data\Art\bgs\Iceland\iceland.ppl',
    'x86\Data\Art\bgs\Swamp\swamp.ppl',
    'x86\Data\Art\bgs\XSwamp\xswamp.ppl'
)
$MapColorFiles = @(
    'x86\Data\Art\hd\classic\forest_mapColors.bin',
    'x86\Data\Art\hd\classic\iceland_mapColors.bin',
    'x86\Data\Art\hd\classic\swamp_mapColors.bin',
    'x86\Data\Art\hd\classic\xswamp_mapColors.bin'
)

# Ally screen (F5) reads hardcoded RGBA from these skins, not palette files.
$AllyScreenSkinsJson = 'x86\Data\skins\skins.json'
$AllyScreenSkinPrefix = 'fe_endgame_stats_bar_'

function Get-DisabledPlayerDisplayColor([int]$playerIndex) {
    # Prefer live/vanilla palette sample at the documented minimap source index.
    if ($DisabledPlayerDisplayIndices.ContainsKey($playerIndex)) {
        $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
        if (!$pplPath) {
            $live = Join-Path $GameRootPath 'x86\Data\Art\bgs\Forest\forest.ppl'
            if (Test-Path -LiteralPath $live) { $pplPath = $live }
        }
        if ($pplPath) {
            try {
                $bytes = Read-FileBytes $pplPath
                return Get-ColorFromPaletteBytes $bytes ([int]$DisabledPlayerDisplayIndices[$playerIndex])
            } catch {
                # Fall through to fixed hex.
            }
        }
    }
    if ($DisabledPlayerDisplayHex.ContainsKey($playerIndex)) {
        return Convert-HexToColor $DisabledPlayerDisplayHex[$playerIndex]
    }
    return $null
}

function Get-PlayerDisplayColor($colors, [int]$playerIndex) {
    $forced = Get-DisabledPlayerDisplayColor $playerIndex
    if ($forced) { return $forced }
    return $colors[$playerIndex]
}

function Convert-HexToColor([string]$hex) {
    $h = $hex.Trim()
    if ($h -notmatch '^#') { $h = "#$h" }
    return [pscustomobject]@{
        R = [int]::Parse($h.Substring(1, 2), 'HexNumber')
        G = [int]::Parse($h.Substring(3, 2), 'HexNumber')
        B = [int]::Parse($h.Substring(5, 2), 'HexNumber')
    }
}

function Convert-ColorToHex($color) {
    return '#{0:X2}{1:X2}{2:X2}' -f $color.R, $color.G, $color.B
}

function Scale-Channel([int]$chan) {
    return [byte][math]::Round($chan * 63 / 255)
}

function Get-GameRootPaths {
    # Only patch the live game install - not the dev repo copy.
    return @($GameRootPath.TrimEnd('\'))
}

function Get-GameFilePath([string]$relativePath, [string]$root) {
    return Join-Path $root $relativePath
}

function Get-RelativeGamePath([string]$absolutePath, [string]$root) {
    return $absolutePath.Substring($root.TrimEnd('\').Length).TrimStart('\')
}

function Get-ColorFromPaletteBytes($bytes, [int]$idx, [switch]$EightBit) {
    $off = $idx * 3
    if (($off + 2) -ge $bytes.Length) {
        throw "Palette index buiten bereik: $idx"
    }
    if ($EightBit) {
        return [pscustomobject]@{
            R = [int]$bytes[$off]
            G = [int]$bytes[$off + 1]
            B = [int]$bytes[$off + 2]
        }
    }
    return [pscustomobject]@{
        R = [int][math]::Round($bytes[$off] * 255 / 63)
        G = [int][math]::Round($bytes[$off + 1] * 255 / 63)
        B = [int][math]::Round($bytes[$off + 2] * 255 / 63)
    }
}

function Get-DefaultPlayerHexColors {
    $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
    if (!$pplPath) {
        throw 'Vanilla backup ontbreekt. Run capture-vanilla.bat na Scan and Repair.'
    }
    $bytes = Read-FileBytes $pplPath
    $result = @()
    foreach ($idx in $VanillaJsonColorIndices) {
        $result += (Convert-ColorToHex (Get-ColorFromPaletteBytes $bytes $idx))
    }
    return $result
}

function Get-VanillaBackupRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return Join-Path $scriptDir 'backup\vanilla'
}

function Get-AuthenticVanillaSourcePath([string]$relativePath) {
    # Ground truth = live game install (Program Files) after Scan and Repair.
    $live = Join-Path $GameRootPath $relativePath
    if (Test-Path -LiteralPath $live) { return $live }

    $scriptDir = Split-Path -Parent $PSCommandPath
    $legacyRel = $relativePath -replace '^x86\\', ''
    $leaf = Split-Path -Leaf $relativePath
    $candidates = @(
        (Join-Path (Join-Path $scriptDir 'backup\vanilla') $relativePath),
        (Join-Path $GameRootPath ("x86\Mods\Player6-Cyan\backup\{0}" -f $legacyRel)),
        (Join-Path $scriptDir ("backup\Data\Art\hd\classic\{0}" -f $leaf))
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Sync-AuthenticVanillaBackup {
    $vanillaRoot = Get-VanillaBackupRoot
    if (!(Test-Path -LiteralPath $vanillaRoot)) {
        New-Item -ItemType Directory -Path $vanillaRoot -Force | Out-Null
    }

    foreach ($rel in ($PplFiles + $MapColorFiles + @($AllyScreenSkinsJson))) {
        $source = Get-AuthenticVanillaSourcePath $rel
        if (!$source) {
            throw "Geen bron gevonden voor: $rel (run eerst Battle.net Scan and Repair)"
        }
        $dest = Join-Path $vanillaRoot $rel
        $destDir = Split-Path -Parent $dest
        if (!(Test-Path -LiteralPath $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        Copy-Item -LiteralPath $source -Destination $dest -Force
        Write-Host "Vanilla backup <= $source"
    }

    $pplPath = Join-Path $vanillaRoot 'x86\Data\Art\bgs\Forest\forest.ppl'
    $pplBytes = Read-FileBytes $pplPath
    $colors = foreach ($idx in $VanillaJsonColorIndices) {
        Get-ColorFromPaletteBytes $pplBytes $idx
    }
    Save-ColorsToJson @(Get-DefaultPlayerColors) `
        (Get-DefaultSelectionHighlightHex) `
        (Get-DefaultEnemySelectionHighlightHex) `
        (Get-DefaultAllyHighlightHex) `
        (Get-DefaultCritterHighlightHex) `
        (Get-DefaultGoldMineHighlightHex) `
        (Get-DefaultOilPatchHighlightHex)
    Write-ApplyLog "Captured backup\vanilla from $GameRootPath (Scan and Repair source)"
}

function Get-VanillaBackupPath([string]$relativePath) {
    $scriptDir = Split-Path -Parent $PSCommandPath
    $candidates = @(
        (Join-Path (Join-Path $scriptDir 'backup\vanilla') $relativePath),
        (Join-Path (Join-Path $GameRootPath 'war2-color-mod\backup\vanilla') $relativePath)
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Get-BackupFilePath([string]$absolutePath, [string]$root) {
    $rel = Get-RelativeGamePath $absolutePath $root

    # Shipped vanilla backups are the restore source of truth.
    $vanilla = Get-VanillaBackupPath $rel
    if ($vanilla) { return $vanilla }

    $scriptDir = Split-Path -Parent $PSCommandPath
    $legacyRel = $rel -replace '^x86\\', ''
    $candidates = @(
        (Join-Path (Join-Path $GameRootPath 'war2-color-mod\backup') $legacyRel),
        (Join-Path (Join-Path $scriptDir 'backup') $rel),
        (Join-Path (Join-Path $GameRootPath 'war2-color-mod\backup') $rel),
        (Join-Path $GameRootPath ("x86\Mods\Player6-Cyan\backup\{0}" -f $legacyRel))
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Read-FileBytes([string]$path) {
    return [IO.File]::ReadAllBytes($path)
}

function Write-FileBytes([string]$path, [byte[]]$bytes) {
    [IO.File]::WriteAllBytes($path, $bytes)
}

function Ensure-BackupOfFile([string]$absolutePath, [string]$root) {
    $rel = Get-RelativeGamePath $absolutePath $root
    $vanilla = Get-VanillaBackupPath $rel
    if (!$vanilla) {
        throw "Vanilla backup ontbreekt voor: $rel. Voer sync-to-game.bat opnieuw uit."
    }
    return $vanilla
}

function Get-DefaultPlayerColors {
    return @(Get-DefaultPlayerHexColors | ForEach-Object { Convert-HexToColor $_ })
}

function Restore-OriginalPaletteFiles {
    foreach ($rel in ($MapColorFiles + $PplFiles + @($AllyScreenSkinsJson))) {
        Restore-FromBackup $rel
    }
    Save-ColorsToJson @(Get-DefaultPlayerColors) `
        (Get-DefaultSelectionHighlightHex) `
        (Get-DefaultEnemySelectionHighlightHex) `
        (Get-DefaultAllyHighlightHex) `
        (Get-DefaultCritterHighlightHex) `
        (Get-DefaultGoldMineHighlightHex) `
        (Get-DefaultOilPatchHighlightHex)
    Write-ApplyLog "Restored authentic vanilla game files and player-colors.json"
}

function Restore-FromBackup([string]$relativePath) {
    foreach ($root in (Get-GameRootPaths)) {
        $targetPath = Get-GameFilePath $relativePath $root
        if (!(Test-Path -LiteralPath $targetPath)) { continue }
        $backupPath = Get-BackupFilePath $targetPath $root
        if (!$backupPath) {
            throw "Geen vanilla backup gevonden voor: $relativePath"
        }
        $targetDir = Split-Path -Parent $targetPath
        if (!(Test-Path $targetDir)) {
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        }
        Copy-Item -LiteralPath $backupPath -Destination $targetPath -Force
        Write-Host "Restored $targetPath <= $backupPath"
    }
}

function Write-ApplyLog([string]$message) {
    $logPath = Join-Path (Split-Path -Parent $PSCommandPath) 'war2_color_patch_log.txt'
    $line = '{0:yyyy-MM-dd HH:mm:ss}  {1}' -f (Get-Date), $message
    Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8
    Write-Host $line
}

function Test-GameRunning {
    return $null -ne (Get-Process -Name 'Warcraft II' -ErrorAction SilentlyContinue)
}

# UI slots restored from vanilla after each apply unless customized below.
# 189-191 = build-bar yellows; 201-202 = build-stage red.
# 251 is ally highlight (customized); not preserved.
$PreservePaletteIndicesPpl = @(245, 189, 190, 191) + $BuildStageRedPaletteIndices
$PreservePaletteIndicesBin = @(245) + $BuildStageRedPaletteIndices

$TilesetPalettePairs = @(
    @{
        Ppl = 'x86\Data\Art\bgs\Forest\forest.ppl'
        Bin = 'x86\Data\Art\hd\classic\forest_mapColors.bin'
    },
    @{
        Ppl = 'x86\Data\Art\bgs\Iceland\iceland.ppl'
        Bin = 'x86\Data\Art\hd\classic\iceland_mapColors.bin'
    },
    @{
        Ppl = 'x86\Data\Art\bgs\Swamp\swamp.ppl'
        Bin = 'x86\Data\Art\hd\classic\swamp_mapColors.bin'
    },
    @{
        Ppl = 'x86\Data\Art\bgs\XSwamp\xswamp.ppl'
        Bin = 'x86\Data\Art\hd\classic\xswamp_mapColors.bin'
    }
)

function Get-AllPlayerPaletteIndices {
    $indices = New-Object System.Collections.Generic.HashSet[int]
    foreach ($playerIndex in $PlayerColorIndices.Keys) {
        foreach ($idx in $PlayerColorIndices[$playerIndex]) {
            [void]$indices.Add($idx)
        }
    }
    return @($indices)
}

function Copy-PaletteIndexBytes($sourceBytes, $targetBytes, [int]$idx) {
    $off = $idx * 3
    if (($off + 2) -ge $sourceBytes.Length) { return }
    if (($off + 2) -ge $targetBytes.Length) { return }
    $targetBytes[$off] = $sourceBytes[$off]
    $targetBytes[$off + 1] = $sourceBytes[$off + 1]
    $targetBytes[$off + 2] = $sourceBytes[$off + 2]
}

function Restore-PreservedPaletteSlots($targetBytes, $vanillaBytes, [int[]]$indices) {
    foreach ($idx in $indices) {
        Copy-PaletteIndexBytes $vanillaBytes $targetBytes $idx
    }
}

# True when a palette index plays the same role in both files: identical
# vanilla bytes. mapColors.bin has a different layout for the highlight
# slots (236-238 is the local player's white HD-minimap dot, 246/247/250
# hold tileset-specific data), so only matching slots may be mirrored.
function Test-SamePaletteRole($aBytes, $bBytes, [int]$idx) {
    $o = $idx * 3
    if (($o + 2) -ge $aBytes.Length -or ($o + 2) -ge $bBytes.Length) { return $false }
    return ($aBytes[$o] -eq $bBytes[$o] -and
            $aBytes[$o + 1] -eq $bBytes[$o + 1] -and
            $aBytes[$o + 2] -eq $bBytes[$o + 2])
}

function Set-PlayerColorsOnBytes($bytes, $colors, [switch]$IncludePplOnlyMinimap) {
    foreach ($playerIndex in $PlayerColorIndices.Keys) {
        if ($DisabledPlayerIndices -contains $playerIndex) { continue }
        $baseColor = $colors[$playerIndex]
        foreach ($idx in $PlayerColorIndices[$playerIndex]) {
            Set-PaletteIndexFromColor $bytes $idx $baseColor
        }
        if ($IncludePplOnlyMinimap -and $PlayerMinimapPplOnlyIndices.ContainsKey([int]$playerIndex)) {
            foreach ($idx in $PlayerMinimapPplOnlyIndices[[int]$playerIndex]) {
                Set-PaletteIndexFromColor $bytes $idx $baseColor
            }
        }
    }
}

function Get-DefaultSelectionHighlightHex {
    $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
    if (!$pplPath) {
        return '#00FF00'
    }
    $bytes = Read-FileBytes $pplPath
    return Convert-ColorToHex (Get-ColorFromPaletteBytes $bytes $SelectionHighlightPaletteIndex)
}

function Set-SelectionHighlightOnBytes($bytes, $color) {
    Set-PaletteIndexFromColor $bytes $SelectionHighlightPaletteIndex $color
}

function Get-DefaultEnemySelectionHighlightHex {
    $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
    if (!$pplPath) {
        return '#FF0000'
    }
    $bytes = Read-FileBytes $pplPath
    return Convert-ColorToHex (Get-ColorFromPaletteBytes $bytes $EnemySelectionHighlightPaletteIndex)
}

function Set-EnemySelectionHighlightOnBytes($bytes, $color) {
    Set-PaletteIndexFromColor $bytes $EnemySelectionHighlightPaletteIndex $color
}

function Get-DefaultAllyHighlightHex {
    $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
    if (!$pplPath) {
        return '#FFFF00'
    }
    $bytes = Read-FileBytes $pplPath
    return Convert-ColorToHex (Get-ColorFromPaletteBytes $bytes $AllyHighlightPaletteIndex)
}

function Set-AllyHighlightOnBytes($bytes, $color) {
    Set-PaletteIndexFromColor $bytes $AllyHighlightPaletteIndex $color
}

function Get-DefaultCritterHighlightHex {
    $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
    if (!$pplPath) {
        return '#C0C0C0'
    }
    $bytes = Read-FileBytes $pplPath
    return Convert-ColorToHex (Get-ColorFromPaletteBytes $bytes $CritterHighlightPaletteIndex)
}

function Set-CritterHighlightOnBytes($bytes, $color) {
    Set-PaletteIndexFromColor $bytes $CritterHighlightPaletteIndex $color
}

function Get-DefaultGoldMineHighlightHex {
    $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
    if (!$pplPath) {
        return '#694114'
    }
    $bytes = Read-FileBytes $pplPath
    return Convert-ColorToHex (Get-ColorFromPaletteBytes $bytes $GoldMineHighlightPaletteIndices[0])
}

function Set-GoldMineHighlightOnBytes($bytes, $color) {
    foreach ($idx in $GoldMineHighlightPaletteIndices) {
        Set-PaletteIndexFromColor $bytes $idx $color
    }
}

function Get-DefaultOilPatchHighlightHex {
    $pplPath = Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl'
    if (!$pplPath) {
        return '#FFFBF3'
    }
    $bytes = Read-FileBytes $pplPath
    return Convert-ColorToHex (Get-ColorFromPaletteBytes $bytes $OilPatchHighlightPaletteIndex)
}

function Set-OilPatchHighlightOnBytes($bytes, $color) {
    Set-PaletteIndexFromColor $bytes $OilPatchHighlightPaletteIndex $color
}

function Get-SelectionPatchPaletteIndices {
    return @(
        $SelectionHighlightPaletteIndex,
        $EnemySelectionHighlightPaletteIndex,
        $AllyHighlightPaletteIndex,
        $CritterHighlightPaletteIndex,
        $OilPatchHighlightPaletteIndex
    ) + $GoldMineHighlightPaletteIndices
}

function Get-AllyBarCursorColor($color) {
    # Ally chips render progress_cursor; keep the exact player color (no 2x clip to white).
    return [pscustomobject]@{
        R = $color.R
        G = $color.G
        B = $color.B
    }
}

function Apply-AllyScreenSkinsJson {
    param(
        [object[]]$Colors,
        [string]$Root
    )

    $path = Get-GameFilePath $AllyScreenSkinsJson $Root
    if (!(Test-Path -LiteralPath $path)) {
        throw "skins.json niet gevonden: $path"
    }

    Ensure-BackupOfFile $path $Root | Out-Null
    $content = [IO.File]::ReadAllText($path)

    for ($playerIndex = 0; $playerIndex -lt 8; $playerIndex++) {
        if ($DisabledPlayerIndices -contains $playerIndex) { continue }

        $base = $Colors[$playerIndex]
        $cursor = Get-AllyBarCursorColor $base
        $skinId = "$AllyScreenSkinPrefix$playerIndex"
        $baseRgba = "$($base.R), $($base.G), $($base.B), 255"
        $cursorRgba = "$($cursor.R), $($cursor.G), $($cursor.B), 255"

        $progressPattern = '(?s)("id"\s*:\s*"' + [regex]::Escape($skinId) + '".*?"progress"\s*:\s*\{\s*"color"\s*:\s*)\[[^\]]+\]'
        $content = [regex]::Replace($content, $progressPattern, "`${1}[$baseRgba]", 1)

        $cursorPattern = '(?s)("id"\s*:\s*"' + [regex]::Escape($skinId) + '".*?"progress_cursor"\s*:\s*\{\s*"color"\s*:\s*)\[[^\]]+\]'
        $content = [regex]::Replace($content, $cursorPattern, "`${1}[$cursorRgba]", 1)
    }

    [IO.File]::WriteAllText($path, $content, [Text.UTF8Encoding]::new($false))
    Write-Host "  Patched skins.json ally bars (fe_endgame_stats_bar_*)"
}

function Test-AllyScreenSkinsJson($colors, [string]$Root) {
    $path = Get-GameFilePath $AllyScreenSkinsJson $Root
    $content = [IO.File]::ReadAllText($path)
    $base = $colors[0]
    $expected = "$($base.R), $($base.G), $($base.B), 255"
    $skinId = "${AllyScreenSkinPrefix}0"
    $pattern = '(?s)"id"\s*:\s*"' + [regex]::Escape($skinId) + '".*?"progress"\s*:\s*\{\s*"color"\s*:\s*\[([^\]]+)\]'
    $match = [regex]::Match($content, $pattern)
    if (!$match.Success) {
        throw "Kon fe_endgame_stats_bar_0 niet vinden in skins.json."
    }
    $actual = ($match.Groups[1].Value -replace '\s', '')
    $want = ($expected -replace '\s', '')
    if ($actual -ne $want) {
        throw "Patch mislukt op skins.json ($skinId progress.color)."
    }
}

function Set-PaletteIndexFromColor($bytes, [int]$idx, $color) {
    $off = $idx * 3
    if (($off + 2) -ge $bytes.Length) { return }
    # Minimap + units use 6-bit palette bytes (0-63 per channel).
    $bytes[$off] = Scale-Channel $color.R
    $bytes[$off + 1] = Scale-Channel $color.G
    $bytes[$off + 2] = Scale-Channel $color.B
}

function Apply-MinimapAndAllyColors {
    param(
        [object[]]$Colors,
        $SelectionHighlightColor,
        $EnemySelectionHighlightColor,
        $AllyHighlightColor,
        $CritterHighlightColor,
        $GoldMineHighlightColor,
        $OilPatchHighlightColor
    )

    $colors = @($Colors)
    if ($colors.Count -ne 8) {
        throw "Kleuren konden niet worden gelezen (verwacht 8 spelers, kreeg $($colors.Count))."
    }
    if (!$SelectionHighlightColor) {
        $SelectionHighlightColor = Convert-HexToColor (Get-DefaultSelectionHighlightHex)
    }
    if (!$EnemySelectionHighlightColor) {
        $EnemySelectionHighlightColor = Get-EnemySelectionHighlightColorForApply
    }
    if (!$AllyHighlightColor) {
        $AllyHighlightColor = Convert-HexToColor (Get-DefaultAllyHighlightHex)
    }
    if (!$CritterHighlightColor) {
        $CritterHighlightColor = Convert-HexToColor (Get-DefaultCritterHighlightHex)
    }
    if (!$GoldMineHighlightColor) {
        $GoldMineHighlightColor = Convert-HexToColor (Get-DefaultGoldMineHighlightHex)
    }
    if (!$OilPatchHighlightColor) {
        $OilPatchHighlightColor = Convert-HexToColor (Get-DefaultOilPatchHighlightHex)
    }

    $gameRunning = Test-GameRunning
    $root = $GameRootPath.TrimEnd('\')
    $playerIndices = Get-AllPlayerPaletteIndices
    $selectionPatchIndices = Get-SelectionPatchPaletteIndices
    Write-Host "Patching: $root"

    foreach ($pair in $TilesetPalettePairs) {
        $pplFile = Get-GameFilePath $pair.Ppl $root
        $binFile = Get-GameFilePath $pair.Bin $root
        if (!(Test-Path -LiteralPath $pplFile)) {
            Write-Host "Skip missing: $pplFile"
            continue
        }
        if (!(Test-Path -LiteralPath $binFile)) {
            Write-Host "Skip missing: $binFile"
            continue
        }

        Ensure-BackupOfFile $pplFile $root | Out-Null
        Ensure-BackupOfFile $binFile $root | Out-Null

        $vanillaPpl = Read-FileBytes (Get-VanillaBackupPath $pair.Ppl)
        $vanillaBin = Read-FileBytes (Get-VanillaBackupPath $pair.Bin)
        $pplBytes = Read-FileBytes $pplFile
        $binBytes = Read-FileBytes $binFile

        Set-PlayerColorsOnBytes $pplBytes $colors -IncludePplOnlyMinimap
        Restore-PreservedPaletteSlots $pplBytes $vanillaPpl $PreservePaletteIndicesPpl
        Set-SelectionHighlightOnBytes $pplBytes $SelectionHighlightColor
        Set-EnemySelectionHighlightOnBytes $pplBytes $EnemySelectionHighlightColor
        Set-AllyHighlightOnBytes $pplBytes $AllyHighlightColor
        Set-CritterHighlightOnBytes $pplBytes $CritterHighlightColor
        Set-GoldMineHighlightOnBytes $pplBytes $GoldMineHighlightColor
        Set-OilPatchHighlightOnBytes $pplBytes $OilPatchHighlightColor

        # mapColors.bin: unit bands only — never low minimap slots 1/2 (different file format).
        Set-PlayerColorsOnBytes $binBytes $colors
        foreach ($idx in $playerIndices) {
            Copy-PaletteIndexBytes $pplBytes $binBytes $idx
        }
        # Highlight slots: only mirror into the bin when the index has the same
        # role there (identical vanilla bytes). Blind copies painted the local
        # player's white minimap dots gold (bin 236-238 got the ppl gold band).
        # Non-matching slots are reset to vanilla to heal earlier corruption.
        foreach ($idx in $selectionPatchIndices) {
            if (Test-SamePaletteRole $vanillaPpl $vanillaBin $idx) {
                Copy-PaletteIndexBytes $pplBytes $binBytes $idx
            } else {
                Copy-PaletteIndexBytes $vanillaBin $binBytes $idx
            }
        }
        Restore-PreservedPaletteSlots $binBytes $vanillaBin $PreservePaletteIndicesBin

        Write-FileBytes $pplFile $pplBytes
        Write-FileBytes $binFile $binBytes
        Write-Host "  Patched $(Split-Path -Leaf $pplFile) + $(Split-Path -Leaf $binFile)"
    }

    $verifyPpl = Get-GameFilePath 'x86\Data\Art\bgs\Forest\forest.ppl' $root
    $verifyBin = Get-GameFilePath 'x86\Data\Art\hd\classic\forest_mapColors.bin' $root
    $ppl = Read-FileBytes $verifyPpl
    $bin = Read-FileBytes $verifyBin
    $o = 208 * 3
    $expected = @(
        (Scale-Channel $colors[0].R)
        (Scale-Channel $colors[0].G)
        (Scale-Channel $colors[0].B)
    )
    if ($ppl[$o] -ne $expected[0] -or $ppl[$o + 1] -ne $expected[1] -or $ppl[$o + 2] -ne $expected[2]) {
        throw "Patch mislukt op forest.ppl (idx 208). Probeer open-desktop.bat als Administrator te starten."
    }
    if ($bin[$o] -ne $expected[0] -or $bin[$o + 1] -ne $expected[1] -or $bin[$o + 2] -ne $expected[2]) {
        throw "Patch mislukt op forest_mapColors.bin (idx 208). Probeer open-desktop.bat als Administrator te starten."
    }
    if ($bin[$o] -ne $ppl[$o] -or $bin[$o + 1] -ne $ppl[$o + 1] -or $bin[$o + 2] -ne $ppl[$o + 2]) {
        throw "forest.ppl en forest_mapColors.bin komen niet overeen op idx 208."
    }

    $vanillaPpl = Read-FileBytes (Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl')
    $vanillaBin = Read-FileBytes (Get-VanillaBackupPath 'x86\Data\Art\hd\classic\forest_mapColors.bin')
    $oFriendly = $SelectionHighlightPaletteIndex * 3
    $expectedFriendly = @(
        (Scale-Channel $SelectionHighlightColor.R)
        (Scale-Channel $SelectionHighlightColor.G)
        (Scale-Channel $SelectionHighlightColor.B)
    )
    if ($ppl[$oFriendly] -ne $expectedFriendly[0] -or $ppl[$oFriendly + 1] -ne $expectedFriendly[1] -or $ppl[$oFriendly + 2] -ne $expectedFriendly[2]) {
        throw "Patch mislukt op forest.ppl (idx $SelectionHighlightPaletteIndex, friendly highlight)."
    }
    if (Test-SamePaletteRole $vanillaPpl $vanillaBin $SelectionHighlightPaletteIndex) {
        if ($bin[$oFriendly] -ne $expectedFriendly[0] -or $bin[$oFriendly + 1] -ne $expectedFriendly[1] -or $bin[$oFriendly + 2] -ne $expectedFriendly[2]) {
            throw "Patch mislukt op forest_mapColors.bin (idx $SelectionHighlightPaletteIndex, friendly highlight)."
        }
    } elseif ($bin[$oFriendly] -ne $vanillaBin[$oFriendly] -or $bin[$oFriendly + 1] -ne $vanillaBin[$oFriendly + 1] -or $bin[$oFriendly + 2] -ne $vanillaBin[$oFriendly + 2]) {
        throw "forest_mapColors.bin (idx $SelectionHighlightPaletteIndex) is niet vanilla gebleven."
    }

    $oEnemy = $EnemySelectionHighlightPaletteIndex * 3
    $expectedEnemy = @(
        (Scale-Channel $EnemySelectionHighlightColor.R)
        (Scale-Channel $EnemySelectionHighlightColor.G)
        (Scale-Channel $EnemySelectionHighlightColor.B)
    )
    if ($ppl[$oEnemy] -ne $expectedEnemy[0] -or $ppl[$oEnemy + 1] -ne $expectedEnemy[1] -or $ppl[$oEnemy + 2] -ne $expectedEnemy[2]) {
        throw "Patch mislukt op forest.ppl (idx $EnemySelectionHighlightPaletteIndex, enemy highlight)."
    }

    $oAlly = $AllyHighlightPaletteIndex * 3
    $expectedAlly = @(
        (Scale-Channel $AllyHighlightColor.R)
        (Scale-Channel $AllyHighlightColor.G)
        (Scale-Channel $AllyHighlightColor.B)
    )
    if ($ppl[$oAlly] -ne $expectedAlly[0] -or $ppl[$oAlly + 1] -ne $expectedAlly[1] -or $ppl[$oAlly + 2] -ne $expectedAlly[2]) {
        throw "Patch mislukt op forest.ppl (idx $AllyHighlightPaletteIndex, ally highlight)."
    }

    $oCritter = $CritterHighlightPaletteIndex * 3
    $expectedCritter = @(
        (Scale-Channel $CritterHighlightColor.R)
        (Scale-Channel $CritterHighlightColor.G)
        (Scale-Channel $CritterHighlightColor.B)
    )
    if ($ppl[$oCritter] -ne $expectedCritter[0] -or $ppl[$oCritter + 1] -ne $expectedCritter[1] -or $ppl[$oCritter + 2] -ne $expectedCritter[2]) {
        throw "Patch mislukt op forest.ppl (idx $CritterHighlightPaletteIndex, critter minimap)."
    }

    $oGold = $GoldMineHighlightPaletteIndices[0] * 3
    $expectedGold = @(
        (Scale-Channel $GoldMineHighlightColor.R)
        (Scale-Channel $GoldMineHighlightColor.G)
        (Scale-Channel $GoldMineHighlightColor.B)
    )
    if ($ppl[$oGold] -ne $expectedGold[0] -or $ppl[$oGold + 1] -ne $expectedGold[1] -or $ppl[$oGold + 2] -ne $expectedGold[2]) {
        throw "Patch mislukt op forest.ppl (idx $($GoldMineHighlightPaletteIndices[0]), gold mine highlight)."
    }

    $oOil = $OilPatchHighlightPaletteIndex * 3
    $expectedOil = @(
        (Scale-Channel $OilPatchHighlightColor.R)
        (Scale-Channel $OilPatchHighlightColor.G)
        (Scale-Channel $OilPatchHighlightColor.B)
    )
    if ($ppl[$oOil] -ne $expectedOil[0] -or $ppl[$oOil + 1] -ne $expectedOil[1] -or $ppl[$oOil + 2] -ne $expectedOil[2]) {
        throw "Patch mislukt op forest.ppl (idx $OilPatchHighlightPaletteIndex, oil patch highlight)."
    }

    foreach ($buildIdx in $BuildStageRedPaletteIndices) {
        $oBuild = $buildIdx * 3
        if ($ppl[$oBuild] -ne $vanillaPpl[$oBuild] -or $ppl[$oBuild + 1] -ne $vanillaPpl[$oBuild + 1] -or $ppl[$oBuild + 2] -ne $vanillaPpl[$oBuild + 2]) {
            throw "Patch mislukt op forest.ppl (idx $buildIdx, build stage red)."
        }
        if ($bin[$oBuild] -ne $vanillaBin[$oBuild] -or $bin[$oBuild + 1] -ne $vanillaBin[$oBuild + 1] -or $bin[$oBuild + 2] -ne $vanillaBin[$oBuild + 2]) {
            throw "Patch mislukt op forest_mapColors.bin (idx $buildIdx, build stage red)."
        }
    }

    Apply-AllyScreenSkinsJson -Colors $colors -Root $root
    Test-AllyScreenSkinsJson -colors $colors -Root $root

    $patchedPlayers = @(
        $PlayerColorIndices.Keys | Where-Object { $DisabledPlayerIndices -notcontains $_ } | ForEach-Object { $_ + 1 } | Sort-Object
    ) -join ','
    Write-ApplyLog "Applied minimap + ally + selection highlight to $root (players $patchedPlayers)"
    if ($gameRunning) {
        Write-Host 'Warcraft II is running — restart the game to load the new colors.'
    }
    return $gameRunning
}

function Get-PlayerColorsJsonPath {
    return Join-Path (Split-Path -Parent $PSCommandPath) 'player-colors.json'
}

function Get-ExtraFeaturesJsonPath {
    return Join-Path (Split-Path -Parent $PSCommandPath) 'extra-features.json'
}

function Read-ExtraDragSelectConfig {
    $path = Get-ExtraFeaturesJsonPath
    $enabled = $false
    $hex = $null
    if (Test-Path -LiteralPath $path) {
        try {
            $raw = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
            if ($raw.DragSelectColorEnabled -eq $true) { $enabled = $true }
            if ($raw.DragSelectColor) { $hex = [string]$raw.DragSelectColor }
        } catch {
            Write-ApplyLog "Kon extra-features.json niet lezen: $($_.Exception.Message)"
        }
    }
    if (!$hex) { $hex = Get-DefaultSelectionHighlightHex }
    return [pscustomobject]@{ Enabled = $enabled; Hex = $hex }
}

function Apply-DragSelectPaletteColor {
    param(
        [bool]$Enabled,
        [string]$ColorHex
    )
    # Drag-select cannot safely own a separate palette slot yet: index 250 is shared by
    # unit outlines, the rubber-band, and other UI greens. Selection owns 250 instead.
    Write-ApplyLog "Drag-select palette apply skipped (shared index 250; use Selection). enabled=$Enabled color=$ColorHex"
}

function Load-ColorsFromJsonOrDefault {
    $jsonPath = Get-PlayerColorsJsonPath
    $defaultHex = Get-DefaultPlayerHexColors

    if (!(Test-Path -LiteralPath $jsonPath)) {
        return ,@(Get-DefaultPlayerColors)
    }

    $raw = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    $hexByIndex = @{}
    foreach ($p in $raw.players) {
        $idx = [int]$p.player - 1
        if ($idx -ge 0 -and $idx -le 7) { $hexByIndex[$idx] = [string]$p.color }
    }

    $result = for ($i = 0; $i -lt 8; $i++) {
        $h = if ($hexByIndex.ContainsKey($i)) { $hexByIndex[$i] } else { $defaultHex[$i] }
        Convert-HexToColor $h
    }
    return ,$result
}

function Load-SelectionHighlightFromJsonOrDefault {
    $jsonPath = Get-PlayerColorsJsonPath
    if (!(Test-Path -LiteralPath $jsonPath)) {
        return Convert-HexToColor (Get-DefaultSelectionHighlightHex)
    }

    $raw = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    if ($raw.selectionHighlight) {
        return Convert-HexToColor ([string]$raw.selectionHighlight)
    }
    return Convert-HexToColor (Get-DefaultSelectionHighlightHex)
}

function Get-EnemySelectionHighlightColorForApply {
    return Load-EnemySelectionHighlightFromJsonOrDefault
}

function Load-EnemySelectionHighlightFromJsonOrDefault {
    if ($EnemySelectionHighlightUiDisabled) {
        return Convert-HexToColor $EnemySelectionDisplayHex
    }
    $jsonPath = Get-PlayerColorsJsonPath
    if (!(Test-Path -LiteralPath $jsonPath)) {
        return Convert-HexToColor (Get-DefaultEnemySelectionHighlightHex)
    }

    $raw = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    if ($raw.enemySelectionHighlight) {
        return Convert-HexToColor ([string]$raw.enemySelectionHighlight)
    }
    return Convert-HexToColor (Get-DefaultEnemySelectionHighlightHex)
}

function Load-AllyHighlightFromJsonOrDefault {
    $jsonPath = Get-PlayerColorsJsonPath
    if (!(Test-Path -LiteralPath $jsonPath)) {
        return Convert-HexToColor (Get-DefaultAllyHighlightHex)
    }

    $raw = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    if ($raw.allyHighlight) {
        return Convert-HexToColor ([string]$raw.allyHighlight)
    }
    return Convert-HexToColor (Get-DefaultAllyHighlightHex)
}

function Load-CritterHighlightFromJsonOrDefault {
    $jsonPath = Get-PlayerColorsJsonPath
    if (!(Test-Path -LiteralPath $jsonPath)) {
        return Convert-HexToColor (Get-DefaultCritterHighlightHex)
    }

    $raw = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    if ($raw.critterHighlight) {
        return Convert-HexToColor ([string]$raw.critterHighlight)
    }
    return Convert-HexToColor (Get-DefaultCritterHighlightHex)
}

function Load-GoldMineHighlightFromJsonOrDefault {
    $jsonPath = Get-PlayerColorsJsonPath
    if (!(Test-Path -LiteralPath $jsonPath)) {
        return Convert-HexToColor (Get-DefaultGoldMineHighlightHex)
    }

    $raw = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    if ($raw.goldMineHighlight) {
        return Convert-HexToColor ([string]$raw.goldMineHighlight)
    }
    # Legacy combined field from older Studio / configs.
    if ($raw.goldMineOilHighlight) {
        return Convert-HexToColor ([string]$raw.goldMineOilHighlight)
    }
    return Convert-HexToColor (Get-DefaultGoldMineHighlightHex)
}

function Load-OilPatchHighlightFromJsonOrDefault {
    $jsonPath = Get-PlayerColorsJsonPath
    if (!(Test-Path -LiteralPath $jsonPath)) {
        return Convert-HexToColor (Get-DefaultOilPatchHighlightHex)
    }

    $raw = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    if ($raw.oilPatchHighlight) {
        return Convert-HexToColor ([string]$raw.oilPatchHighlight)
    }
    return Convert-HexToColor (Get-DefaultOilPatchHighlightHex)
}

function Save-ColorsToJson(
    $colors,
    [string]$selectionHighlightHex,
    [string]$enemySelectionHighlightHex,
    [string]$allyHighlightHex,
    [string]$critterHighlightHex,
    [string]$goldMineHighlightHex,
    [string]$oilPatchHighlightHex
) {
    try {
        if (!$selectionHighlightHex) {
            $selectionHighlightHex = Get-DefaultSelectionHighlightHex
        }
        if (!$enemySelectionHighlightHex) {
            $enemySelectionHighlightHex = if ($EnemySelectionHighlightUiDisabled) {
                $EnemySelectionDisplayHex
            } else {
                Get-DefaultEnemySelectionHighlightHex
            }
        }
        if (!$allyHighlightHex) {
            $allyHighlightHex = Get-DefaultAllyHighlightHex
        }
        if (!$critterHighlightHex) {
            $critterHighlightHex = Get-DefaultCritterHighlightHex
        }
        if (!$goldMineHighlightHex) {
            $goldMineHighlightHex = Get-DefaultGoldMineHighlightHex
        }
        if (!$oilPatchHighlightHex) {
            $oilPatchHighlightHex = Get-DefaultOilPatchHighlightHex
        }
        $players = for ($i = 0; $i -lt 8; $i++) {
            [pscustomobject]@{
                player = $i + 1
                preset = 'Custom'
                color  = (Convert-ColorToHex $colors[$i])
            }
        }
        $json = ([pscustomobject]@{
            players                  = $players
            selectionHighlight       = $selectionHighlightHex
            enemySelectionHighlight  = $enemySelectionHighlightHex
            allyHighlight            = $allyHighlightHex
            critterHighlight         = $critterHighlightHex
            goldMineHighlight        = $goldMineHighlightHex
            oilPatchHighlight        = $oilPatchHighlightHex
        } | ConvertTo-Json -Depth 5)
        [IO.File]::WriteAllText((Get-PlayerColorsJsonPath), $json, [Text.UTF8Encoding]::new($false))
    } catch {
        Write-ApplyLog "Kon player-colors.json niet opslaan: $($_.Exception.Message)"
    }
}

function Test-VanillaBackupReady {
    return $null -ne (Get-VanillaBackupPath 'x86\Data\Art\bgs\Forest\forest.ppl')
}

function Ensure-VanillaBackupReady {
    if (Test-VanillaBackupReady) { return }
    Sync-AuthenticVanillaBackup
}

if ($GetDefaultConfig) {
    # Backups are deliberately created locally from this installation and are
    # never distributed with the app or committed to source control.
    Ensure-VanillaBackupReady

    $defaultColors = Get-DefaultPlayerColors
    $players = for ($i = 0; $i -lt 8; $i++) {
        [pscustomobject]@{
            player = $i + 1
            color = Convert-ColorToHex (Get-PlayerDisplayColor $defaultColors $i)
        }
    }
    [pscustomobject]@{
        players = $players
        selectionHighlight = (Get-DefaultSelectionHighlightHex)
        enemySelectionHighlight = (Get-DefaultEnemySelectionHighlightHex)
        allyHighlight = (Get-DefaultAllyHighlightHex)
        critterHighlight = (Get-DefaultCritterHighlightHex)
        goldMineHighlight = (Get-DefaultGoldMineHighlightHex)
        oilPatchHighlight = (Get-DefaultOilPatchHighlightHex)
    } | ConvertTo-Json -Compress
    exit 0
}

if ($SyncVanillaBackup) {
    Sync-AuthenticVanillaBackup
    exit 0
}

if ($RestoreOnly) {
    Ensure-VanillaBackupReady
    Restore-OriginalPaletteFiles
    exit 0
}

if ($ApplyDragSelectFromExtra) {
    # Kept for Studio compatibility; drag no longer patches a separate palette slot.
    Ensure-VanillaBackupReady
    Write-ApplyLog 'ApplyDragSelectFromExtra: no-op (Selection owns shared index 250).'
    exit 0
}

if ($ApplySavedConfigOnly) {
    Ensure-VanillaBackupReady
    $loaded = Load-ColorsFromJsonOrDefault
    $highlight = Load-SelectionHighlightFromJsonOrDefault
    $enemy = Load-EnemySelectionHighlightFromJsonOrDefault
    $ally = Load-AllyHighlightFromJsonOrDefault
    $critter = Load-CritterHighlightFromJsonOrDefault
    $gold = Load-GoldMineHighlightFromJsonOrDefault
    $oil = Load-OilPatchHighlightFromJsonOrDefault
    Apply-MinimapAndAllyColors -Colors @($loaded) `
        -SelectionHighlightColor $highlight `
        -EnemySelectionHighlightColor $enemy `
        -AllyHighlightColor $ally `
        -CritterHighlightColor $critter `
        -GoldMineHighlightColor $gold `
        -OilPatchHighlightColor $oil
    exit 0
}

Write-Error 'Specify -GetDefaultConfig, -ApplySavedConfigOnly, -ApplyDragSelectFromExtra, -RestoreOnly, or -SyncVanillaBackup.'
exit 1

