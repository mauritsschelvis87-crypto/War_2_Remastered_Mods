# WC2r Audio Tool (source mirror)

Canonical install: `%LOCALAPPDATA%\WC2rAudioTool\`  
Desktop shortcut: **WC2r Audio Tool**

This folder mirrors the app so Units-tab + presets stay in the repo. After editing here, copy to LocalAppData:

```powershell
Copy-Item installer\WC2rAudioTool\audio_tool.py $env:LOCALAPPDATA\WC2rAudioTool\ -Force
Copy-Item installer\WC2rAudioTool\unit_presets.py $env:LOCALAPPDATA\WC2rAudioTool\ -Force
```

## Tabs

- **Audio** — original voice tool (custom A/B, speeds, trim, export)
- **Units** — custom campaign unit presets (name, base type/spells, stats, audio bank)

Presets: `%LOCALAPPDATA%\WC2rAudioTool\unit-presets\`  
War2 Content Studio loads them under palette group **Custom**.
