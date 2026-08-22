# Warcraft II — Quality of Life Modding

**Beta Version 1.0.5**

Windows WPF tool for safely changing confirmed Warcraft II Remastered player colors, plus optional multiplayer QoL.

## Features

- **Players 1–7:** minimap dots, unit/team palette bands, and victory/ally bars
- **Player 2:** unit band `212–215` + minimap palette index `1` (exe table `0x008C8D84`) + ally/victory skins
- **Player 8:** still locked (shared yellow / minimap index `2`)
- **Extra colors (on Player colors tab):** Self highlight (`250`), Critter minimap (`247`)
- **Feature:** mark gone players on Alliances, and chat names in player color.
- **Bug fixes:** optional **chat during pause screen** (read + send while paused). Enable once — a background watcher (`AllyLeaveWatch`) starts with Windows and injects when the game runs; Studio can stay closed. Preferences are stored in `mod\extra-features.json` and survive reboot.

## Share / release installer

Build a zip others can download — full self-contained Setup inside, no Visual Studio or .NET SDK needed:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File installer\Build-SharePackage.ps1
```

That creates:

- `dist\QoL-Modding-Setup-1.0.5.zip` — **share this**
- a copy on your Desktop: `QoL Modding Setup.zip`

Recipients extract the zip, run `QoL Modding Setup.exe`, accept Administrator, press Install, then Apply in the app and restart Warcraft II.

## Install from source (developers)

1. Clone this repository outside the Warcraft II installation folder when possible.
2. Run `installer\PLAYER COLORS.bat`.
3. The installer builds the WPF app, native Extra hook, and installs at `x86\Mods\PlayerColorStudio`.
4. On first launch, a local vanilla backup is captured from the current game installation. Run Battle.net **Scan and Repair** first if files may already be modified.

The source installer needs the .NET 10 SDK and MSVC x86 tools (for the Extra hook). Generated binaries are deliberately not committed.

## Safety and source policy

Only app source, the patch engine, native Extra-hook source, installer, research notes, and documentation are committed. Warcraft II game data, executables, DLLs, palettes, local vanilla backups, logs, configurations, and build output are excluded.

`mod\Apply-PlayerColors.ps1` is the only component that writes game color files. It creates and uses a local `backup\vanilla` restore source before applying changes. The Extra leave-marker loads `AllyLeaveHook.dll` into a running game process when enabled.
