# Disassemble the map-message ring functions in "Warcraft II.exe":
#   0x614D50 = store entry (text, colorByte, arg3) -> slot metadata layout
#   0x614DD0 = draw loop  -> how expiry/visibility is decided
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
md.detail = False

def dump(va, size, title):
    print(f"===== {title} @ {va:#x} =====")
    off = va_to_off(va)
    for ins in md.disasm(data[off:off + size], va):
        print(f"0x{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str}")
        if ins.mnemonic == "ret" and ins.address > va + 0x40:
            break

dump(0x614D50, 0x100, "StoreMsgEntry(entry, text, colorByte, arg3)")
dump(0x614DD0, 0x300, "DrawMapMessages")
