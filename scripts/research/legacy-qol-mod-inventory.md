# Legacy QoL / BNE mod inventory

Date: 2026-08-31

## LimeWire link (`https://limewire.com/d/A1MfH#fXqhXUGsbN`)

**Status:** Not auto-downloadable (browser-only share). Place extracted contents in
`Desktop\All_mod_stuff\limewire-qol\` when available.

**Expected contents (community WC2 BNE QoL packs often include):**

- `plugin\*.w2p` — runtime plugins (`w2p_init`, `screen_update`)
- `LC.dll` — Lambchops PUD library (used by lobby_map)
- Map/campaign tools, stat editors (if present)

Until downloaded, use the sources below.

## QoL Modding Setup.zip (Desktop)

Installer-only package (PlayerColorStudio v1.0.4). **No** map editor or unit-stat source.

## Warcraft-II-Plugins — `lobby_map` (Desktop)

| Item | Value |
|------|-------|
| Path | `Desktop\All_mod_stuff\Warcraft-II-Plugins-main\lobby_map\` |
| License | Plugin ecosystem / Lambchops |
| Exports | `w2p_init`, `screen_update` |
| Lobby detect | `GAME_MODE==0x0D` && `MAP_LOBBY!=0` |
| Map name ptr | `0x004ABB0C` |
| Map path ptr | `0x004ABFA4` |
| Thumb default | x=452, y=106 @ 640×480 |

**Remastered:** BNE memory globals still exist at same RVAs; `.w2p` loader does not.

## Installed overlay — `lobby_map_mod`

| Item | Path |
|------|------|
| Overlay script | `Warcraft II Remastered\lobby_map_mod\mod\lobby-map-overlay.ps1` |
| PUD thumbnail | `...\pud-thumbnail.ps1` (section parser reference) |

UIAutomation anchors MP lobby row label **"Map"**; map value read via sibling scan.

## Remastered exe anchors (MP lobby UI)

| Symbol | VA |
|--------|-----|
| Lobby layout fn | `0x005362E0` |
| `mp_lobby_map` widget | push @ `0x005369B0` |
| `player_slots` | push @ `0x0053674C` |
| UI add route | `0x005ACA30` |

## Relevance to War2ContentStudio

| Legacy asset | Use |
|--------------|-----|
| `pudthumb.cpp` / `pud-thumbnail.ps1` | MTXM tile colors, UNIT mines/starts |
| PUD section names in `lamb.h` | Reader/writer section IDs |
| Campaign folder layout | `x86\Data\Campaign\` deploy target |
| Locale JSON | Custom unit display names (phase 3) |

## Not portable from BNE

- `.w2p` plugin injection into Remastered exe
- Fixed 640×480 overlay coordinates without UIAutomation
- MPQ / `war2dat.mpq` workflow (classic install)
