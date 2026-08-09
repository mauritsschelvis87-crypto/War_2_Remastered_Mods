# Warcraft II Player Color Studio

Player Color Studio is a Windows WPF tool for safely changing the confirmed Warcraft II Remastered player-color palette bands:

- Player 1 and Players 3–7: minimap colors and victory/ally bars.
- Player 2 and Player 8: intentionally disabled. No safe, exclusive Remastered hook has been verified.
- Friendly selection highlight: preserved by the patch engine, not exposed in this version.

## Install from source

1. Clone this repository outside the Warcraft II installation folder when possible.
2. Run `installer\PLAYER COLORS.bat` as a normal user. It asks for administrator approval only when copying into `Program Files`.
3. The installer builds the WPF app and installs it at `x86\Mods\PlayerColorStudio`.
4. On first launch, a local vanilla backup is captured from the current game installation. Run Battle.net **Scan and Repair** first if the game files might already be modified.

The installer needs the .NET 10 SDK. A release package can ship a prebuilt app separately; generated binaries are deliberately not committed.

## Safety and source policy

This repository contains only the app source, patch engine, installer, and documentation. It explicitly excludes Warcraft II game data, executables, DLLs, palettes, local vanilla backups, logs, configurations, and build output.

`mod\Apply-PlayerColors.ps1` is the only component that writes game files. It creates/uses a local `backup\vanilla` restore source before applying changes.
