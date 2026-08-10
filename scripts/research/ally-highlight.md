# Ally highlight colors (unit outlines)

Date: 2026-08-10

## Question

Is there a separate **ally highlight** palette index (like self **250** / enemy **249**)?

## Finding (updated 2026-08-10)

Dedicated selection-outline palette slots used by Remastered:

| Role | Index | Notes |
|------|-------|--------|
| Self | **250** | Own units + drag box |
| Enemy | **249** | Enemy outlines (+ some build UI) |
| Ally | **251** | Yellow ally outline (`mov [0x96512c], 0xFB` paths) |
| Critter | **247** | Minimap dots; selection outline uses Ally **251** |
| Gold mine | **236–238** | Resource gold selection band |
| Oil patch | **246** | Oil resource selection outline (`0xF6`) |

Non-self owner path at `0x004D3D70` still falls through to player table `0x008C8D64` for some cases; ally yellow **251** is the dedicated ally glow used by several selection drawers.
