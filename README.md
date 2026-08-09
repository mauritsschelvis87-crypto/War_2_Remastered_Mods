# Warcraft II Player Color Studio

Player Color Studio is a Windows WPF tool for safely changing confirmed Warcraft II Remastered player-color palette bands.

- Player 1 and Players 3–7: minimap colors and victory/ally bars.
- Player 2 and Player 8: intentionally disabled; no safe, exclusive Remastered hook has been verified.

## Install from source

1. Clone this repository outside the Warcraft II installation folder when possible.
2. Run `installer\PLAYER COLORS.bat`.
3. The installer builds the WPF app and installs it at `x86\Mods\PlayerColorStudio`.
4. On first launch, a local vanilla backup is captured from the current game installation. Run Battle.net **Scan and Repair** first if files may already be modified.

The installer needs the .NET 10 SDK. Generated binaries are deliberately not committed.

## Safety and source policy

Only app source, the patch engine, installer, and documentation are committed. Warcraft II game data, executables, DLLs, palettes, local vanilla backups, logs, configurations, and build output are excluded.

`mod\Apply-PlayerColors.ps1` is the only component that writes game files. It creates and uses a local `backup\vanilla` restore source before applying changes.
