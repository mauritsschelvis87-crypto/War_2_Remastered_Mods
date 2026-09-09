# War2 Content Studio

Modern Warcraft II Remastered content tooling: PUD map editor, campaign deploy, and research hooks for deeper modding.

## Scope (plan phases)

| Phase | Status | Features |
|-------|--------|----------|
| **1 — Map editor** | MVP | Load/save `.pud`, terrain paint, unit placement, UDTA preserve, undo/redo, minimap preview |
| **2 — Campaign** | MVP | JSON project, deploy to `x86/Data/Campaign` + `x86/Maps` with backup |
| **3 — RE** | Research docs | Unit stat tables, spell learn tables, locale name paths |

## Build & run

```powershell
dotnet build app/War2ContentStudio/War2ContentStudio.csproj
dotnet run --project app/War2ContentStudio/War2ContentStudio.csproj
```

## Tests

```powershell
dotnet test tests/War2Pud.Tests/War2Pud.Tests.csproj
```

## PUD library

- Spec reference: `docs/pudspec.txt` (from [jcfieldsdev/warcraft2-map-editor](https://github.com/jcfieldsdev/warcraft2-map-editor))
- `PudReader` / `PudWriter` — section round-trip
- `PudDocument` — tiles, units, description, era, UDTA flag
- `PudFactory.CreateEmpty()` — minimal valid map
- `PudThumbnail.Render()` — minimap preview

## Campaign project

Example `campaign.json`:

```json
{
  "Version": 1,
  "Name": "My Campaign",
  "CampaignFolderName": "Custom",
  "GameRootPath": "C:\\Program Files (x86)\\Warcraft II Remastered",
  "Missions": [
    {
      "Title": "Mission 1",
      "SourcePudPath": "C:\\maps\\m1.pud",
      "DeployFileName": "m1.pud",
      "Briefing": "Destroy all enemies."
    }
  ]
}
```

Deploy backs up existing files under `%LOCALAPPDATA%\War2ContentStudio\campaign-backup\`.

## Research

- `scripts/research/legacy-qol-mod-inventory.md` — LimeWire / BNE QoL sources
- `scripts/research/unit-stat-tables.md` — exe stat table RE
- `scripts/research/spell-learn-tables.md` — spell modding go/no-go

## Not in MVP

- Custom unit types / sprites
- New spells
- Custom win conditions
- MP-safe global stat patches
