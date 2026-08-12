# Locate the HD unit tint table in "Warcraft II.exe" and disassemble the
# code that references it, to find how the game maps a player seat to the
# color-slot index used for rendering.
import struct
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"

VANILLA = [
    (164, 0, 0), (0, 60, 192), (44, 180, 148), (156, 72, 176),
    (240, 132, 20), (40, 40, 60), (206, 205, 212), (252, 252, 72),
]

def entry_bytes(rgb):
    r, g, b = rgb
    return struct.pack("<4f", r / 255.0, g / 255.0, b / 255.0, 1.0)

pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase

# P2..P8 needle (P1 may differ / be patched in file? file is vanilla, use full)
needle = b"".join(entry_bytes(v) for v in VANILLA)

table_va = None
for sec in pe.sections:
    data = sec.get_data()
    idx = data.find(needle)
    if idx >= 0:
        table_va = base + sec.VirtualAddress + idx
        print(f"tint table at VA 0x{table_va:08X} in section {sec.Name.decode(errors='ignore').strip(chr(0))}")
        break

if table_va is None:
    # try P2..P8 only
    needle = b"".join(entry_bytes(v) for v in VANILLA[1:])
    for sec in pe.sections:
        data = sec.get_data()
        idx = data.find(needle)
        if idx >= 0:
            table_va = base + sec.VirtualAddress + idx - 16
            print(f"tint table (via P2 needle) at VA 0x{table_va:08X} in {sec.Name.decode(errors='ignore').strip(chr(0))}")
            break

if table_va is None:
    print("table not found in file")
    sys.exit(1)

# search code sections for 4-byte immediates pointing at/near the table
code_secs = [s for s in pe.sections if s.Characteristics & 0x20000000]  # executable
hits = []
for sec in code_secs:
    data = sec.get_data()
    sec_va = base + sec.VirtualAddress
    for delta in range(0, 128, 4):  # table start or any entry/channel offset
        imm = struct.pack("<I", table_va + delta)
        start = 0
        while True:
            i = data.find(imm, start)
            if i < 0:
                break
            hits.append((sec_va + i, delta, sec, i))
            start = i + 1

print(f"{len(hits)} immediate refs")
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False
for va, delta, sec, off in hits:
    data = sec.get_data()
    ctx_start = max(0, off - 48)
    code = data[ctx_start:off + 48]
    print(f"\n--- ref at VA~0x{va:08X} (table+{delta}) ---")
    for ins in md.disasm(code, base + sec.VirtualAddress + ctx_start):
        marker = " <==" if ins.address <= va < ins.address + ins.size else ""
        print(f"  0x{ins.address:08X}  {ins.mnemonic} {ins.op_str}{marker}")
