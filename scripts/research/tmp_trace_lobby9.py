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


def va_to_off(va):
    rva = va - image_base
    for name, sva, vsize, rawptr, rawsize in sections:
        if sva <= rva < sva + rawsize:
            return rawptr + (rva - sva)
    return None


def off_to_va(off):
    for name, sva, vsize, rawptr, rawsize in sections:
        if rawptr <= off < rawptr + rawsize:
            return image_base + sva + (off - rawptr)
    return None


off = va_to_off(0x53d950)
for i in range(0, 400):
    o = off + i
    va = 0x53d950 + i
    b = data[o]
    line = hex(va)
    if b == 0x6a:
        line += " push " + str(data[o + 1])
    elif b == 0xe8:
        rel = struct.unpack_from("<i", data, o + 1)[0]
        tgt = va + 5 + rel
        line += " call " + hex(tgt)
        if tgt in (0x520500, 0x4c48e0, 0x533020, 0x5362E0):
            line += " ***"
    if "push" in line or "call" in line:
        print(line)

# 520280 - slot mirror sync
off = va_to_off(0x520280)
print("\n520280 head:", data[off:off+48].hex())
for i in range(0, 200):
    o = off + i
    va = 0x520280 + i
    if data[o] == 0xe8:
        rel = struct.unpack_from("<i", data, o + 1)[0]
        tgt = va + 5 + rel
        if tgt in (0x520500, 0x4c48e0, 0x51f675):
            print(hex(va), "call", hex(tgt))
