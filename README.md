# Warcraft II — Modding Studio

**Version 1.0.2**

Windows WPF tool for safely changing confirmed Warcraft II Remastered player colors, plus optional multiplayer QoL.

## Features

- **Players 1–7:** minimap dots, unit/team palette bands, and victory/ally bars
- **Player 2:** unit band `212–215` + minimap palette index `1` (exe table `0x008C8D84`) + ally/victory skins
- **Player 8:** still locked (shared yellow / minimap index `2`)
- **Extra:** mark left/dropped/eliminated players in red on the Alliances screen (runtime hook; auto-injects when the game is running)

## Install from source

1. Clone this repository outside the Warcraft II installation folder when possible.
2. Run `installer\PLAYER COLORS.bat`.
3. The installer builds the WPF app, native Extra hook, and installs at `x86\Mods\PlayerColorStudio`.
4. On first launch, a local vanilla backup is captured from the current game installation. Run Battle.net **Scan and Repair** first if files may already be modified.

The installer needs the .NET 10 SDK and MSVC x86 tools (for the Extra hook). Generated binaries are deliberately not committed.

## Safety and source policy

Only app source, the patch engine, native Extra-hook source, installer, research notes, and documentation are committed. Warcraft II game data, executables, DLLs, palettes, local vanilla backups, logs, configurations, and build output are excluded.

`mod\Apply-PlayerColors.ps1` is the only component that writes game color files. It creates and uses a local `backup\vanilla` restore source before applying changes. The Extra leave-marker loads `AllyLeaveHook.dll` into a running game process when enabled.
