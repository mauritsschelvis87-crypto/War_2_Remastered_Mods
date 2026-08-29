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


def find_callers(target):
    hits = []
    for name, sva, vsize, rawptr, rawsize in sections:
        if name != ".text":
            continue
        for i in range(rawptr, rawptr + rawsize - 5):
            if data[i] != 0xE8:
                continue
            rel = struct.unpack_from("<i", data, i + 1)[0]
            va = off_to_va(i)
            if va and va + 5 + rel == target:
                hits.append(va)
    return hits


print("callers of 51f980:", [hex(h) for h in find_callers(0x51f980)[:15]])

for va in range(0x53b550, 0x53b5c0):
    o = va_to_off(va)
    b = data[o]
    line = hex(va)
    if b == 0x6a:
        line += " push " + str(data[o + 1])
    elif b == 0xe8:
        rel = struct.unpack_from("<i", data, o + 1)[0]
        line += " call " + hex(va + 5 + rel)
    if "push" in line or "call" in line:
        print(line)

# refs to callback table base 844200
for g in [0x844200, 0x84425C]:
    needle = struct.pack("<I", g)
    idx = 0
    n = 0
    while n < 10:
        j = data.find(needle, idx)
        if j < 0:
            break
        va = off_to_va(j)
        if va and 0x500000 <= va <= 0x550000:
            print(f"ref {hex(g)} at {hex(va)}")
            n += 1
        idx = j + 1
