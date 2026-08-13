# Verify time chain (0x67FDF0) and the per-entry visibility logic
# (0x614DA0 update fn + full draw-loop function around 0x615000).
import struct
import capstone

EXE = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
data = open(EXE, "rb").read()

pe_off = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
image_base = struct.unpack_from("<I", data, pe_off + 24 + 28)[0]
sec_off = pe_off + 24 + opt_size
sections = []
for i in range(nsec):
    o = sec_off + i * 40
    va = struct.unpack_from("<I", data, o + 12)[0]
    vsz = struct.unpack_from("<I", data, o + 8)[0]
    raw = struct.unpack_from("<I", data, o + 20)[0]
    sections.append((va, vsz, raw))

def va_to_off(va):
    rva = va - image_base
    for sva, svsz, sraw in sections:
        if sva <= rva < sva + svsz:
            return sraw + (rva - sva)
    raise ValueError(hex(va))

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def dump(va, size, title, stop_at_ret=True):
    print(f"===== {title} @ {va:#x} =====")
    off = va_to_off(va)
    for ins in md.disasm(data[off:off + size], va):
        print(f"0x{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str}")
        if stop_at_ret and ins.mnemonic == "ret":
            break
    print()

dump(0x67FDF0, 0x60, "TimeInner")
dump(0x614DA0, 0x50, "EntryUpdate(entry, arg)")
# Draw-loop function: find its start by scanning back from 0x615089 for int3 padding
off = va_to_off(0x615089)
p = off
while data[p - 1] != 0xCC:
    p -= 1
start_va = 0x615089 - (off - p)
dump(start_va, 0x615089 + 0x100 - start_va, f"DrawLoop fn (start {start_va:#x})", stop_at_ret=False)
