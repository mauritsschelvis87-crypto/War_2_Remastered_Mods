#!/usr/bin/env python3
"""RE spike: look for unit-type → spell bitmask tables in Warcraft II Remastered."""

from __future__ import annotations

import json
import struct
import sys
from collections import Counter
from pathlib import Path

DEFAULT_EXE = Path(r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe")

# ALOW spell bits from pudspec.txt
SPELL_BITS = {
    0: "holy_vision",
    1: "healing",
    3: "exorcism",
    4: "flame_shield",
    5: "fireball",
    6: "slow",
    7: "invisibility",
    8: "polymorph",
    9: "blizzard",
    10: "eye_of_kilrogg",
    11: "bloodlust",
    13: "raise_dead",
    14: "death_coil",
    15: "whirlwind",
    16: "haste",
    17: "unholy_armor",
    18: "runes",
    19: "death_and_decay",
}

# Typical vanilla spellbooks (research hypotheses — not proven for Remastered).
PALADIN_MASK = (1 << 0) | (1 << 1) | (1 << 3)  # 0x0000000B
MAGE_MASK = (
    (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9)
)  # 0x000003F0
OGRE_MAGE_MASK = (1 << 10) | (1 << 11) | (1 << 18)  # 0x00040C00
DEATH_KNIGHT_MASK = (
    (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16) | (1 << 17) | (1 << 19)
)  # 0x000BE000

def parse_pe(data: bytes):
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


def off_to_va(off: int, image_base: int, sections) -> int | None:
    for _name, sva, _vsize, rawptr, rawsize in sections:
        if rawptr <= off < rawptr + rawsize:
            return image_base + sva + (off - rawptr)
    return None


def find_imm32(data: bytes, value: int) -> list[int]:
    needle = struct.pack("<I", value)
    hits = []
    start = 0
    while True:
        idx = data.find(needle, start)
        if idx < 0:
            break
        hits.append(idx)
        start = idx + 1
    return hits


def decode_mask(mask: int) -> list[str]:
    return [name for bit, name in SPELL_BITS.items() if mask & (1 << bit)]


def scan_u32_tables(data: bytes, image_base: int, sections) -> list[dict]:
    """Find 110-dword regions where known caster slots look like spell masks."""
    # Unit order from pudspec Appendix A (first ~20 caster-relevant ids):
    # 0a mage, 0b death knight, 0c?, need accurate indices.
    # We'll score any 110-u32 window that contains both MAGE_MASK and OGRE_MAGE_MASK
    # (or DEATH_KNIGHT_MASK) as distinct entries.
    targets = {
        MAGE_MASK: "mage_like",
        OGRE_MAGE_MASK: "ogre_mage_like",
        DEATH_KNIGHT_MASK: "death_knight_like",
        PALADIN_MASK: "paladin_like",
    }
    hits = []
    # Search .rdata + .data
    for name, sva, vsize, rawptr, rawsize in sections:
        if name not in (".rdata", ".data"):
            continue
        chunk = data[rawptr : rawptr + rawsize]
        # slide by 4 over plausible table starts
        max_start = len(chunk) - 110 * 4
        if max_start <= 0:
            continue
        # only check alignments where any target mask appears
        candidate_starts = set()
        for mask in targets:
            for off in find_imm32(chunk, mask):
                # back up to possible table index 0..109
                for idx in range(110):
                    start = off - idx * 4
                    if 0 <= start <= max_start and start % 4 == 0:
                        candidate_starts.add(start)
        for start in sorted(candidate_starts):
            values = [struct.unpack_from("<I", chunk, start + i * 4)[0] for i in range(110)]
            found = {}
            for i, v in enumerate(values):
                if v in targets:
                    found.setdefault(targets[v], []).append({"index": i, "mask": hex(v)})
            # Require at least two different caster families
            families = set(found)
            if len(families) < 2:
                continue
            # Prefer sparse tables (most zeros / small values)
            nonzero = sum(1 for v in values if v != 0)
            if nonzero > 40:
                continue
            file_off = rawptr + start
            va = off_to_va(file_off, image_base, sections)
            hits.append(
                {
                    "section": name,
                    "file_offset": hex(file_off),
                    "va": hex(va) if va is not None else None,
                    "nonzero_entries": nonzero,
                    "families": found,
                    "sample_nonzero": [
                        {"index": i, "mask": hex(v), "spells": decode_mask(v)}
                        for i, v in enumerate(values)
                        if v != 0
                    ][:20],
                }
            )
            if len(hits) >= 25:
                return hits
    return hits


def scan_string_keys(data: bytes) -> dict:
    keys = [
        b"spell_%d_tooltip",
        b"bloodlust",
        b"blizzard",
        b"death_coil",
        b"holy_vision",
        b"flame_shield",
        b"eye_of_kilrogg",
        b"death_and_decay",
        b"unit_can_cast",
        b"spellbook",
        b"spell_mask",
        b"allowed_spells",
    ]
    out = {}
    for k in keys:
        idx = data.find(k)
        out[k.decode()] = hex(idx) if idx >= 0 else None
        # also case-insensitive for plain names
        if idx < 0 and k.isalpha():
            idx = data.lower().find(k.lower())
            out[k.decode()] = hex(idx) if idx >= 0 else None
    return out


def scan_imm_hits(data: bytes, image_base: int, sections) -> dict:
    interesting = {
        "PALADIN_MASK": PALADIN_MASK,
        "MAGE_MASK": MAGE_MASK,
        "OGRE_MAGE_MASK": OGRE_MAGE_MASK,
        "DEATH_KNIGHT_MASK": DEATH_KNIGHT_MASK,
        "BLOODLUST_BIT": 1 << 11,
        "BLIZZARD_BIT": 1 << 9,
        "MAGE_PLUS_BLOODLUST": MAGE_MASK | (1 << 11),
    }
    report = {}
    for label, value in interesting.items():
        offs = find_imm32(data, value)
        # Filter to code/data sections, cap list
        kept = []
        for off in offs:
            va = off_to_va(off, image_base, sections)
            if va is None:
                continue
            kept.append({"file_offset": hex(off), "va": hex(va)})
            if len(kept) >= 15:
                break
        report[label] = {"value": hex(value), "hit_count": len(offs), "hits": kept}
    return report


def main() -> int:
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_EXE
    data = exe.read_bytes()
    image_base, sections = parse_pe(data)

    tables = scan_u32_tables(data, image_base, sections)
    imm = scan_imm_hits(data, image_base, sections)
    strings = scan_string_keys(data)

    # Heuristic: if MAGE_MASK appears as imm and OGRE as imm in .text, likely switch/case not table
    text_sec = next((s for s in sections if s[0] == ".text"), None)
    code_has_both = False
    if text_sec:
        _n, _va, _vs, rp, rs = text_sec
        text = data[rp : rp + rs]
        code_has_both = (struct.pack("<I", MAGE_MASK) in text) and (
            struct.pack("<I", OGRE_MAGE_MASK) in text
            or struct.pack("<I", DEATH_KNIGHT_MASK) in text
        )

    verdict = {
        "table_candidates": len(tables),
        "likely_static_table": len(tables) > 0,
        "likely_hardcoded_in_code": code_has_both and len(tables) == 0,
        "sp_campaign_mix_feasibility": (
            "YELLOW — candidate table(s) found; PoC patch next"
            if tables
            else (
                "YELLOW — masks appear as immediates in code; may need code patch / hook"
                if code_has_both
                else "RED — no clear mage/orc spell masks found with this fingerprint"
            )
        ),
    }

    report = {
        "exe": str(exe),
        "size": len(data),
        "image_base": hex(image_base),
        "masks": {
            "paladin": hex(PALADIN_MASK),
            "mage": hex(MAGE_MASK),
            "ogre_mage": hex(OGRE_MAGE_MASK),
            "death_knight": hex(DEATH_KNIGHT_MASK),
        },
        "string_keys": strings,
        "imm32_hits": imm,
        "u32_table_candidates": tables,
        "verdict": verdict,
    }
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
