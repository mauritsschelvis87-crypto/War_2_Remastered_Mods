# Unit stat tables — Warcraft II Remastered RE spike

Date: 2026-08-31  
Exe: `C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe` (5.3 MB, image base `0x00400000`)

## Summary

| Path | Verdict | Notes |
|------|---------|-------|
| **Per-map UDTA** | ✅ Supported today | PUD `UDTA` section — War2 Content Studio preserves blob on save |
| **Global stat tables in exe** | 🟡 GO (deeper RE) | BNE-era tables likely embedded; automated HP fingerprint had no hits (stats may be byte-packed or split) |
| **Locale display names** | ✅ GO (file patch) | `Strings\*.json`, keys like `unit_%d_tooltip_advice` |
| **Runtime DLL patch** | 🟡 Prototype | Pattern: `native/AllyLeaveHook/` hooks |

## Scan results (`scripts/research/scan-war2-exe.py`)

```json
{
  "hp_sequence_hits": [],
  "locale_string_hints": [
    "Strings\\credits.json",
    "unit_%d_tooltip_advice",
    "upgrade_%d_tooltip",
    "spell_%d_tooltip"
  ]
}
```

No contiguous u16 HP chain (60, 60, 240, 240, …) at raw file offsets — tables are probably:

- Stored as bytes / structs with padding
- Split across multiple arrays (HP, damage, armor, …)
- Loaded from data files at runtime (less likely for core unit stats)

## Recommended next RE steps (Ghidra / rizin)

1. Import `Warcraft II.exe` at base `0x00400000`.
2. Search strings `unit_%d_tooltip` → xref to unit-type indexing code.
3. From unit-type dispatch (switch on 0x01–0x5F), trace reads of fixed arrays in `.data`/`.rdata`.
4. Compare with classic BNE offsets (community docs) — Remastered rebase but structure often similar.
5. Document struct layout: `{ hp, mana, damage, range, cost, build_time, … }` per type ID.

## Practical modding paths (priority)

### A. Per-map (editor — done in Fase 1)

Vanilla map editor stats = PUD `UDTA`. War2 Content Studio keeps the section intact; future UI can expose fields per pudspec.

### B. Display names (low risk)

Patch `x86/Data/Strings/<locale>/` JSON — same workflow as PlayerColorStudio tooltip strings in `mod/Apply-PlayerColors.ps1`.

Example keys from exe: `unit_%d_tooltip_advice`, `spell_%d_tooltip`.

### C. Global stats (high risk)

Memory patch or exe edit:

- **SP/custom:** feasible with hook DLL
- **MP:** desync unless all clients match — treat as single-player / custom campaign only
- **Updates:** offsets break on patches — version-pin or signature scan

## Deliverable status

- [x] Automated scan script
- [x] Locale/name path documented
- [ ] Ghidra struct proof (manual follow-up)
- [ ] PoC stat patch DLL (future — out of MVP scope)

## References

- PUD UDTA: `app/War2ContentStudio/docs/pudspec.txt`
- Hook pattern: `native/AllyLeaveHook/ChatNameColorHook.cpp`
- BNE plugin globals (lobby): `scripts/research/legacy-qol-mod-inventory.md`
