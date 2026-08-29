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


def annotate_range(start, end):
    for va in range(start, end):
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


for target in [0x52367f, 0x520500, 0x520910, 0x4a0950, 0x4a0600]:
    hits = find_callers(target)
    print(f"callers of {hex(target)}: {len(hits)}")
    for h in hits[:12]:
        print(" ", hex(h))

print("\n=== 523650-523690 ===")
annotate_range(0x523650, 0x523690)

print("\n=== 520910 start ===")
annotate_range(0x520910, 0x520a20)

# 4a0950 - status UI update?
off = va_to_off(0x4a0950)
print("\n4a0950:", data[off:off+32].hex())
