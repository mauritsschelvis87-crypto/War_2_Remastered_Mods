import struct

path = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
data = open(path, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
opt = e_lfanew + 24
image_base = struct.unpack_from("<I", data, opt + 28)[0]
num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
sec_off = e_lfanew + 24 + size_opt
sections = []
for i in range(num_sections):
    o = sec_off + i * 40
    name = data[o:o + 8].split(b"\0")[0].decode()
    vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
    sections.append((name, va, vsize, rawptr, rawsize))


def off_to_va(off):
    for name, sva, vsize, rawptr, rawsize in sections:
        if rawptr <= off < rawptr + rawsize:
            return image_base + sva + (off - rawptr)
    return None


targets = {0x520500, 0x4c48e0, 0x533020, 0x5362E0, 0x533103}
for target in [0x530320, 0x547280, 0x547260]:
    print(f"\nScan calls from {hex(target)}:")
    off = None
    for name, sva, vsize, rawptr, rawsize in sections:
        if name != ".text":
            continue
        rva = target - image_base
        if sva <= rva < sva + rawsize:
            off = rawptr + (rva - sva)
            break
    if off is None:
        continue
    for i in range(0, 600):
        o = off + i
        va = target + i
        if data[o] == 0xe8:
            rel = struct.unpack_from("<i", data, o + 1)[0]
            tgt = va + 5 + rel
            if tgt in targets:
                print(f"  {hex(va)} -> {hex(tgt)}")

# callback dispatcher using table 844200
for name, sva, vsize, rawptr, rawsize in sections:
    if name != ".text":
        continue
    for i in range(rawptr, rawptr + rawsize - 7):
        if data[i:i+3] == bytes([0xff, 0x24, 0x85]):
            imm = struct.unpack_from("<I", data, i + 3)[0]
            if imm in (0x844200, 0x844220):
                va = off_to_va(i)
                print(f"callback jtable dispatch at {hex(va)} table {hex(imm)}")
