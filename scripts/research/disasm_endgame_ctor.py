# Disassemble the defeat/victory popup constructors in "Warcraft II.exe".
# The defeat screen ctor references 'defeat' @ 0x547A0E/0x547AC9,
# 'defeat_btn' @ 0x547B36 and 'exit_game' @ 0x547B3B; victory ctor
# references 'victory' @ 0x547C39/0x547DEE and 'victory_btn' @ 0x547C9F.
# Goal: recover the widget/button creation calls so a hook can add an
# "Observe" button below the Exit Game button.
import struct

import capstone
import pefile

EXE = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase

secs = []
for s in pe.sections:
    secs.append((base + s.VirtualAddress, s.get_data(), s.Name.decode(errors="ignore").strip("\x00")))

def read_bytes(va, n):
    for sva, data, _ in secs:
        off = va - sva
        if 0 <= off <= len(data) - n:
            return data[off:off + n]
    return None

def read_cstr(va, cap=48):
    for sva, data, _ in secs:
        off = va - sva
        if 0 <= off < len(data):
            end = data.find(b"\x00", off, off + cap)
            if end < 0:
                end = off + cap
            s = data[off:end]
            if s and all(32 <= c < 127 for c in s):
                return s.decode()
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = False

def dump(start, end, title):
    print(f"\n===== {title} ({start:08X}..{end:08X}) =====")
    code = read_bytes(start, end - start)
    for ins in md.disasm(code, start):
        note = ""
        # annotate immediates that point at strings
        for tok in ins.op_str.replace(",", " ").split():
            t = tok.strip("[]")
            if t.startswith("0x"):
                try:
                    v = int(t, 16)
                except ValueError:
                    continue
                s = read_cstr(v)
                if s and len(s) >= 3:
                    note = f"; '{s}'"
        print(f"{ins.address:08X}  {ins.mnemonic:<7} {ins.op_str} {note}")

# defeat ctor region (starts a bit before the first 'defeat' ref)
dump(0x5479A0, 0x547C20, "defeat screen ctor")
# victory ctor region
dump(0x547C20, 0x547E80, "victory screen ctor")
