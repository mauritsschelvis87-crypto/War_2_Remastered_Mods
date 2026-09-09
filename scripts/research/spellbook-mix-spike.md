# Spellbook mix RE spike — Human Mage + Orc spell

Date: 2026-09-09  
Scope: Can a **Human Mage / human hero-mage** cast an **Orc mage spell** (e.g. Bloodlust) in Warcraft II Remastered for a **single-player campaign**?  
Exe: `x86\Warcraft II.exe` (5 558 992 bytes, image base `0x00400000`)

## Verdict

**NO-GO for Content Studio / campaign presets (near term).**  
**YELLOW for a deep, version-pinned SP exe/hook PoC** — no clean unit-type → spell bitmask table found; caster identity is hardcoded.

Single-player removes the MP desync objection, but **does not** expose an editable spellbook per unit type.

## What we proved

### 1. Locale spell IDs match ALOW bit order

`x86\Data\Strings\enUS.json`:

| ID | Name |
|----|------|
| 0 | Holy Vision |
| 1 | Heal |
| 3 | Exorcism |
| 4–9 | Flame Shield … Blizzard (human mage set) |
| 10–11, 18 | Eye of Kilrogg, Bloodlust, Runes (ogre-mage set) |
| 13–17, 19 | Death Knight set |

Renaming spells = JSON only (already known GO).

### 2. Classic “mage mask” fingerprints are false positives

Hypothesized ALOW-style books:

| Role | Mask |
|------|------|
| Paladin | `0x0000000B` |
| Mage | `0x000003F0` |
| Ogre-Mage | `0x00040C00` |
| Death Knight | `0x000BE000` |

Scanner (`scan-spell-masks.py` + Capstone):

- **No** 110-entry `u32` table in `.rdata`/`.data` containing distinct mage vs ogre-mage masks.
- Almost all `0x3F0` hits in `.text` are unrelated (loop counters `mov [ebp-10], 3`, stack sizes, SIMD offsets, etc.).
- `0x40C00` / `0xBE000` do **not** appear as live spellbook immediates in `.text`.

So Remastered is **not** using a simple “patch mage dword |= bloodlust bit” table of the classic shape.

### 3. ALOW is player-level only (confirmed in code)

PUD section dispatch (`.data` ~`0x8C4540`):

| Section | Handler VA |
|---------|------------|
| `ALOW` | `0x004D19D0` |
| `UDTA` | `0x004D2440` |

`ALOW` loader copies six `0x40`-byte blocks (16 players × `u32`) into:

| VA | Meaning (pudspec) |
|----|-------------------|
| `0x919210` | Units allowed |
| `0x919250` | Spells you start with |
| `0x919290` | Spells allowed to research |
| `0x9192D0` | Spells researching |
| `0x919310` | Upgrades allowed |
| `0x919350` | Upgrades acquiring |

Enabling Bloodlust in `ALOW` only affects **player research/start bits**, not “this Mage unit’s buttons”.

Example check (`0x004AD650`): uses `playerIndex` from `unit+0x2C`, tests `1 << spellBit` against `[player*4 + 0x919250]`, plus a per-player byte table at `0x918F30` (`player * 0x1B + spellId`). That is **research/availability**, not unit spellbook composition.

### 4. Caster unit types are hardcoded

Function at `0x004F12C0` is a switch on unit type / related ids. Branches accept e.g.:

- `0x0A` Mage, `0x18` Khadgar  
- `0x0D` Ogre-Mage, `0x07` Ogre, `0x31` Cho’gall, `0x17` Dentarg  
- `0x0B` Death Knight, `0x15` Teron, …

This is an **allow-list of magic unit types**, not a mixable spell list. Giving a Mage Bloodlust would mean teaching the UI/cast path that Bloodlust is valid for type `0x0A` — logic scattered in code, not one data cell.

## Practical campaign guidance

| Goal | Feasible? |
|------|-----------|
| Human hero-mage with custom name/stats/audio | ✅ PUD `UDTA` + Strings + audio packs |
| Human mage with only human spells | ✅ + `ALOW` |
| Human mage that also casts Bloodlust/Runes/Eye | ❌ without exe/hook PoC |
| Story “has orc magic” via second hero (ogre-mage skinned) | ✅ |

## Tools added this spike

- `scripts/research/scan-spell-masks.py` — mask fingerprint + table hunt  
- `scripts/research/disasm-spell-masks.py` — Capstone around hits  
- Outputs: `spell-mask-scan-out.json`, `spell-mask-disasm.txt`

## If a PoC is still desired later (SP only)

1. Trace **command button population** when selecting unit type `0x0A` (find who pushes spell order ids 4–9).  
2. Trace **cast validation** for Bloodlust (`spell_11` / bit 11) and note unit-type checks.  
3. Hook those two sites (same pattern as `AllyLeaveHook`) to allow bit 11 for type `0x0A`.  
4. Version-pin Remastered build; expect breakage on patches.  
5. Stop after one successful SP cast — do not productize until stable.

**Estimated effort:** multi-session RE + fragile hook. **Not** a Content Studio preset field.

## Related

- Earlier go/no-go: `spell-learn-tables.md`  
- Unit stats: `unit-stat-tables.md`  
- PUD ALOW/UDTA: `app/War2ContentStudio/docs/pudspec.txt`
