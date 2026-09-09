# Spell learn tables — go/no-go

Date: 2026-08-31  
Scope: custom **spells**, spell **names**, and mage spell sets in Warcraft II Remastered

## Executive summary

**NO-GO** for adding new spells or reassigning spell sets to arbitrary units without new unit types and deep exe/data modding.

**GO** for:

- Renaming existing spells via locale JSON (`spell_%d_tooltip` keys in exe strings)
- Per-map unit stat tweaks (UDTA) on spell-capable units
- Narrative "spells" via voice lines + briefing text (existing audio pipeline)

## Evidence

### Automated scan (`scan-war2-exe.py`)

Spell-related ASCII in `.rdata`:

| String | File offset (approx.) |
|--------|------------------------|
| heal | 0x0043A023 |
| haste | 0x00439F87 |
| slow | 0x0043A047 |
| blizzard | 0x0043C56F |

These are **localization / tooltip** references, not editable learn tables exposed as data files.

### Engine behavior (vanilla + BNE)

- Spell availability is tied to **unit type** (Paladin, Ogre Mage, Death Knight, …).
- Learn progression uses fixed mage tiers; not stored in PUD for arbitrary units.
- PUD `UDTA` adjusts unit instance stats, not spell book composition.

### Remastered data layout

- Strings: `x86/Data/Strings/` — rename only
- No `spells.json` or equivalent loose data file found in install tree for learn masks
- Learn logic expected in `Warcraft II.exe` (BNE heritage)

## Options matrix

| Goal | Feasible? | Approach |
|------|-----------|----------|
| Rename "Holy Light" → custom text | ✅ | Locale JSON patch |
| Change Paladin HP on one map | ✅ | PUD UDTA |
| Give Footman a spell book | ❌ | No unit type with footman art + spell UI |
| New spell effect (e.g. custom AoE) | ❌ | Requires code + VFX + network sync |
| Swap spells between mage types | ❌ Near-term NO-GO | Remastered has no clean unit→mask table; casters hardcoded (see `spellbook-mix-spike.md`). SP exe/hook PoC only. |

## RE spike conclusion

**Stop line for Fase 3 MVP:** document and deprioritize unless Ghidra session finds a clear, patchable learn-table array with stable xrefs.

If RE continues:

1. Xref from spell cast handlers (search "blizzard" / spell ID pushes in `.text`).
2. Locate unit-type → spell-mask table.
3. PoC: single-player only, one mage type, one spell swapped.
4. If no table found in 1–2 sessions → **closed as NO-GO**.

## Workarounds (campaign authoring)

1. Use existing spell units (Paladin, Ogre Mage, …) with custom **names** and **voices**.
2. Use UDTA for tuned stats (mana, HP) on those units.
3. Objectives remain vanilla win conditions; story via DESC/briefing and `Gamesfx` WAV mods.

## Related

- Unit stats RE: `scripts/research/unit-stat-tables.md`
- PUD editor: `app/War2ContentStudio/`
