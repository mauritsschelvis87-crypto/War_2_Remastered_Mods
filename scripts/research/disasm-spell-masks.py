#!/usr/bin/env python3
"""Disassemble sites referencing hypothesized mage spell masks."""

from __future__ import annotations

import struct
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_32, Cs

EXE = Path(r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe")
MAGE_MASK = 0x3F0
OGRE_MAGE_MASK = 0x40C00
PALADIN_MASK = 0xB
DK_MASK = 0xBE000


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


def va_to_off(va: int, image_base: int, sections):
    rva = va - image_base
    for name, sva, vsize, rawptr, rawsize in sections:
        if sva <= rva < sva + rawsize:
            return rawptr + (rva - sva), name
    return None, None


def off_to_va(off: int, image_base: int, sections):
    for name, sva, vsize, rawptr, rawsize in sections:
        if rawptr <= off < rawptr + rawsize:
            return image_base + sva + (off - rawptr), name
    return None, None


def find_imm32(data: bytes, value: int):
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


def disasm_around(md: Cs, data: bytes, image_base: int, sections, file_off: int, before=48, after=80):
    va, sec = off_to_va(file_off, image_base, sections)
    if va is None or sec != ".text":
        return [f"; skip non-text {sec} @ {hex(file_off)}"]
    start_off = max(0, file_off - before)
    start_va = va - (file_off - start_off)
    chunk = data[start_off : file_off + after]
    lines = [f"=== {hex(va)} (file {hex(file_off)}) section={sec} ==="]
    for insn in md.disasm(chunk, start_va):
        mark = " <<" if file_off <= (insn.address - start_va + start_off) < file_off + 4 else ""
        # better mark: if imm overlaps
        if insn.address <= va < insn.address + insn.size:
            mark = "  <== IMM HERE"
        lines.append(f"  {insn.address:08x}: {insn.mnemonic:8} {insn.op_str}{mark}")
    return lines


def main():
    data = EXE.read_bytes()
    image_base, sections = parse_pe(data)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = False

    text = next(s for s in sections if s[0] == ".text")
    text_chunk = data[text[3] : text[3] + text[4]]

    out_lines = []
    for label, mask in [
        ("MAGE_MASK", MAGE_MASK),
        ("OGRE_MAGE_MASK", OGRE_MAGE_MASK),
        ("DK_MASK", DK_MASK),
        ("PALADIN_MASK", PALADIN_MASK),
    ]:
        hits = find_imm32(data, mask)
        text_hits = []
        for off in hits:
            va, sec = off_to_va(off, image_base, sections)
            if sec == ".text":
                text_hits.append(off)
        out_lines.append(f"\n##### {label}={hex(mask)} text_hits={len(text_hits)} total={len(hits)}")
        for off in text_hits[:12]:
            out_lines.extend(disasm_around(md, data, image_base, sections, off))

    # Also search for cmp/test patterns with unit type 0x0a near 'spell' string xref
    # Find push/lea of spell_%d_tooltip and xref-ish: just dump nearby code that compares to 0x0a/0x0d
    spell_key = data.find(b"spell_%d_tooltip")
    out_lines.append(f"\n##### spell_%d_tooltip at file {hex(spell_key)}")

    # Scan .text for: cmp reg, 0x0A followed within 32 bytes by and/test with 0x3F0-ish — heavy
    # Simpler: find sequences mov eax, imm32 where imm is mage mask after cmp al, 0x0a
    pattern_hits = []
    for i in range(len(text_chunk) - 16):
        # cmp eax/al/ecx, 0x0a  => 83 F8 0A / 3C 0A / 83 F9 0A
        b0, b1, b2 = text_chunk[i], text_chunk[i + 1], text_chunk[i + 2]
        is_cmp_a = (b0 == 0x3C and b1 == 0x0A) or (
            b0 == 0x83 and b2 == 0x0A and b1 in (0xF8, 0xF9, 0xFA, 0xFB, 0xF8)
        )
        # also 80 F9 0A etc
        is_cmp_a = is_cmp_a or (b0 == 0x80 and b1 in (0xF8, 0xF9, 0xFA, 0xFB) and b2 == 0x0A)
        if not is_cmp_a:
            continue
        window = text_chunk[i : i + 48]
        if struct.pack("<I", MAGE_MASK) in window or struct.pack("<I", OGRE_MAGE_MASK) in window:
            file_off = text[3] + i
            pattern_hits.append(file_off)

    out_lines.append(f"\n##### cmp unit==mage near mask imm: {len(pattern_hits)} hits")
    for off in pattern_hits[:20]:
        out_lines.extend(disasm_around(md, data, image_base, sections, off, before=16, after=64))

    # Look for 110-entry tables of u16/u32 where index 0x0a and 0x0d differ and look spellish
    # Try: find dword arrays length>=20 where values are only subsets of known spell bits
    known = {0, PALADIN_MASK, MAGE_MASK, OGRE_MAGE_MASK, DK_MASK, 0xFFFFFFFF, 0x7FFFF}
    # expand: any value using only bits in SPELL range 0..19
    def is_spellish(v: int) -> bool:
        if v == 0:
            return True
        return (v & ~0xFFFFF) == 0 and v.bit_count() <= 8

    rdata = next(s for s in sections if s[0] == ".rdata")
    rchunk = data[rdata[3] : rdata[3] + rdata[4]]
    table_hits = []
    for start in range(0, len(rchunk) - 110 * 4, 4):
        vals = [struct.unpack_from("<I", rchunk, start + i * 4)[0] for i in range(110)]
        if not all(is_spellish(v) for v in vals):
            continue
        nz = [v for v in vals if v]
        if len(nz) < 3 or len(nz) > 25:
            continue
        # must have mage-ish and orc-ish distinct
        mage_i, ogre_i = vals[0x0A], vals[0x0D]
        dk_i, pal_i = vals[0x0B], vals[0x0C]
        if mage_i == 0 and ogre_i == 0 and dk_i == 0 and pal_i == 0:
            continue
        if mage_i == ogre_i == dk_i == pal_i:
            continue
        # at least one caster nonzero
        if max(mage_i, ogre_i, dk_i, pal_i) == 0:
            continue
        file_off = rdata[3] + start
        va = image_base + rdata[1] + start
        table_hits.append(
            {
                "va": hex(va),
                "file": hex(file_off),
                "mage": hex(mage_i),
                "dk": hex(dk_i),
                "paladin": hex(pal_i),
                "ogre_mage": hex(ogre_i),
                "nonzero": sum(1 for v in vals if v),
            }
        )
        if len(table_hits) >= 30:
            break

    out_lines.append(f"\n##### spellish 110-u32 tables in .rdata: {len(table_hits)}")
    for t in table_hits:
        out_lines.append(str(t))

    out_path = Path(__file__).with_name("spell-mask-disasm.txt")
    out_path.write_text("\n".join(out_lines), encoding="utf-8")
    print(f"wrote {out_path} lines={len(out_lines)} tables={len(table_hits)} cmp_near={len(pattern_hits)}")


if __name__ == "__main__":
    main()
