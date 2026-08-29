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


# callback table around 84425c
base = 0x844200
off = va_to_off(base)
print("Callback table 0x844200-0x844300:")
for i in range(0, 0x100, 4):
    va = base + i
    ptr = struct.unpack_from("<I", data, off + i)[0]
    if 0x400000 <= ptr <= 0x700000:
        mark = ""
        if ptr == 0x523640:
            mark = " <-- slot update 523640"
        if ptr == 0x520910:
            mark = " <-- enqueue23 path 520910"
        print(f"  {hex(va)}: {hex(ptr)}{mark}")

# refs to 84425c
needle = struct.pack("<I", 0x0084425C)
idx = 0
while True:
    j = data.find(needle, idx)
    if j < 0:
        break
    va = off_to_va(j)
    if va and 0x400000 <= va <= 0x600000:
        print(f"code ref to table entry at {hex(va)}")
    idx = j + 1

hits = find_callers(0x520901)
print("callers of 520901:", [hex(h) for h in hits[:10]])

# 53d950 - what does it do, callers context
for h in find_callers(0x53d950):
    print(f"context {hex(h)}:")
    o = va_to_off(h - 0x20)
    for va in range(h - 0x20, h + 0x10):
        oo = va_to_off(va)
        if data[oo] == 0xe8:
            rel = struct.unpack_from("<i", data, oo + 1)[0]
            print(f"  {hex(va)} call {hex(va + 5 + rel)}")
