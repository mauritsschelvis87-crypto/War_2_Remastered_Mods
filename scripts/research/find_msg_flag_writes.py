# Find writes to the map-message "initialized" flag byte at 0x9B1798:
#   C6 05 98 17 9B 00 xx   mov byte [0x9B1798], xx
# If it is reset to 0 at match end, polling it gives a hook-free
# "new game started" edge for the chat-history reset.
import struct
import capstone

EXE = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
data = open(EXE, "rb").read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt = struct.unpack_from("<H", data, pe + 20)[0]
base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
so = pe + 24 + opt
secs = []
for i in range(nsec):
    o = so + i * 40
    secs.append((struct.unpack_from("<I", data, o + 12)[0],
                 struct.unpack_from("<I", data, o + 8)[0],
                 struct.unpack_from("<I", data, o + 20)[0]))

def off2va(off):
    for sva, svsz, sraw in secs:
        if sraw <= off < sraw + svsz:
            return base + sva + (off - sraw)
    return None

def va2off(va):
    rva = va - base
    for sva, svsz, sraw in secs:
        if sva <= rva < sva + svsz:
            return sraw + (rva - sva)
    raise ValueError(hex(va))

needle = bytes.fromhex("C605981 79B00".replace(" ", ""))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

idx = data.find(needle)
while idx != -1:
    va = off2va(idx)
    val = data[idx + 6]
    print(f"write @ {va:#x}: mov byte [0x9B1798], {val}")
    # context
    start = va - 0x20
    for ins in md.disasm(data[va2off(start):va2off(start) + 0x40], start):
        mark = " <<<" if ins.address == va else ""
        print(f"  0x{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str}{mark}")
    print()
    idx = data.find(needle, idx + 1)
