# Verify the time function used for message expiry and find the ring
# iteration that compares entry+0xC8 against "now".
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
    name = data[o:o + 8].rstrip(b"\0").decode()
    va = struct.unpack_from("<I", data, o + 12)[0]
    vsz = struct.unpack_from("<I", data, o + 8)[0]
    raw = struct.unpack_from("<I", data, o + 20)[0]
    sections.append((name, va, vsz, raw))

def va_to_off(va):
    rva = va - image_base
    for _, sva, svsz, sraw in sections:
        if sva <= rva < sva + svsz:
            return sraw + (rva - sva)
    raise ValueError(hex(va))

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def dump(va, size, title):
    print(f"===== {title} @ {va:#x} =====")
    off = va_to_off(va)
    for ins in md.disasm(data[off:off + size], va):
        print(f"0x{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str}")
        if ins.mnemonic == "ret":
            break
    print()

dump(0x625940, 0x60, "TimeNow")

# Find code references to the ring base 0x9B17A0 across .text
text = next(s for s in sections if s[0] == ".text")
_, tva, tvsz, traw = text
needle = struct.pack("<I", 0x9B17A0)
hits = []
idx = data.find(needle, traw, traw + tvsz)
while idx != -1:
    hits.append(image_base + tva + (idx - traw))
    idx = data.find(needle, idx + 1, traw + tvsz)
print("ring base refs:", [hex(h) for h in hits])
for h in hits:
    start = h - 0x30
    print(f"--- around {h:#x} ---")
    off = va_to_off(start)
    for ins in md.disasm(data[off:off + 0x70], start):
        mark = " <<<" if ins.address <= h < ins.address + ins.size else ""
        print(f"0x{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str}{mark}")
    print()
