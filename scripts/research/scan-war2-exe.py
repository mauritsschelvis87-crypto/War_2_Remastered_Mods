#!/usr/bin/env python3
"""Scan Warcraft II Remastered x86 exe for unit-stat and spell-table RE leads."""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

DEFAULT_EXE = Path(r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe")

# Known WC2 BNE unit baseline stats (HP) for fingerprinting — not exhaustive.
UNIT_HP_SAMPLES = [
    (0x01, 60),   # footman
    (0x02, 60),   # grunt
    (0x05, 240),  # knight
    (0x06, 240),  # ogre
    (0x0E, 255),  # dragon
    (0x32, 900),  # town hall
]

# Spell IDs sometimes referenced in BNE docs (research only).
SPELL_NAMES = ["heal", "slow", "bloodlust", "haste", "invisibility", "polymorph", "blizzard", "death coil"]


def parse_pe(data: bytes) -> tuple[int, list[tuple[str, int, int, int, int]]]:
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    opt = e_lfanew + 24
    image_base = struct.unpack_from("<I", data, opt + 28)[0]
    num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    sec_off = e_lfanew + 24 + size_opt
    sections = []
    for i in range(num_sections):
        o = sec_off + i * 40
        name = data[o : o + 8].split(b"\0")[0].decode(errors="replace")
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        sections.append((name, va, vsize, rawptr, rawsize))
    return image_base, sections


def va_to_off(va: int, image_base: int, sections: list) -> int | None:
    rva = va - image_base
    for _name, sva, _vsize, rawptr, rawsize in sections:
        if sva <= rva < sva + rawsize:
            return rawptr + (rva - sva)
    return None


def find_u16_sequences(data: bytes, values: list[int], max_hits: int = 20) -> list[int]:
    hits = []
    needle = b"".join(struct.pack("<H", v) for v in values)
    start = 0
    while len(hits) < max_hits:
        idx = data.find(needle, start)
        if idx < 0:
            break
        hits.append(idx)
        start = idx + 2
    return hits


def find_ascii_strings(data: bytes, min_len: int = 6) -> list[tuple[int, str]]:
    out = []
    i = 0
    while i < len(data):
        if data[i] < 0x20 or data[i] > 0x7E:
            i += 1
            continue
        j = i
        while j < len(data) and 0x20 <= data[j] <= 0x7E:
            j += 1
        if j - i >= min_len:
            out.append((i, data[i:j].decode("ascii", errors="ignore")))
        i = j
    return out


def main() -> int:
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_EXE
    if not exe.is_file():
        print(json.dumps({"error": f"exe not found: {exe}", "scanned": False}))
        return 1

    data = exe.read_bytes()
    image_base, sections = parse_pe(data)
    text = next((s for s in sections if s[0] == ".text"), None)
    rdata = next((s for s in sections if s[0] == ".rdata"), None)

    hp_seq = [hp for _id, hp in UNIT_HP_SAMPLES]
    hp_hits = find_u16_sequences(data, hp_seq)

    spell_string_hits = {}
    if rdata:
        _name, _va, _vsize, rawptr, rawsize = rdata
        chunk = data[rawptr : rawptr + rawsize]
        for spell in SPELL_NAMES:
            idx = chunk.lower().find(spell.encode())
            if idx >= 0:
                spell_string_hits[spell] = rawptr + idx

    # Locale path hint (Remastered ships JSON strings)
    locale_hints = []
    for off, s in find_ascii_strings(data, 8):
        if "Strings" in s or "locale" in s.lower() or "tooltip" in s.lower():
            locale_hints.append({"offset": off, "text": s[:80]})
        if len(locale_hints) >= 30:
            break

    report = {
        "scanned": True,
        "exe": str(exe),
        "size_bytes": len(data),
        "image_base": hex(image_base),
        "hp_sequence_hits": hp_hits[:10],
        "hp_sequence_note": "Raw file offsets where footman/grunt/knight HP u16 chain appears — manual RE in Ghidra",
        "spell_string_hits_rva": spell_string_hits,
        "locale_string_hints": locale_hints[:15],
        "recommendation": {
            "unit_stats": "GO for deeper RE — HP fingerprint hits suggest embedded tables; use Ghidra data xref from hits",
            "custom_names": "GO via x86/Data/Strings JSON (same pattern as Apply-PlayerColors tooltips)",
            "spells": "NO-GO for new spells without new unit types; see spell-learn-tables.md",
        },
    }
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
