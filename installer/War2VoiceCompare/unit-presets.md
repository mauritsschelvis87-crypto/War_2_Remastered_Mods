# Custom unit presets (audio app → map editor)

## Model

A “custom unit” is **not** a new engine type. It is a preset that binds:

| Field | Meaning |
|-------|---------|
| `baseUnitType` | Existing WC2 type id (`0x0A` Mage, `0x0C` Paladin, …). **Spells come from this.** |
| `displayName` | Shown name (`unit_N` string override on campaign deploy) |
| `audioBank` | Gamesfx folder name used for voice lines (same as Audio tab) |
| `stats` | PUD `UDTA` overrides for that type on maps that use the preset |

Presets are JSON files:

`%LOCALAPPDATA%\War2VoiceCompare\unit-presets\<id>.json`

War2 Content Studio reads the same folder into the unit palette under **Custom**.

## Limits

- Two presets that share the same `baseUnitType` **share** UDTA stats on one map.
- Prefer unique campaign heroes (`Khadgar`, `Alleria`, …) as bases when you need distinct stats.
- Cannot mix human + orc spellbooks on one type (see `spellbook-mix-spike.md`).

## Workflow

1. **Audio** tab — craft WAVs per bank (unchanged).
2. **Units** tab — create preset: name + base type + stats + audio bank.
3. **Content Studio** — Custom palette entry places `baseUnitType`; save/deploy applies name/stats/audio with the campaign.
