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


def find_jmps(target):
    hits = []
    for name, sva, vsize, rawptr, rawsize in sections:
        if name != ".text":
            continue
        for i in range(rawptr, rawptr + rawsize - 5):
            va = off_to_va(i)
            if not va:
                continue
            b = data[i]
            if b == 0xE9:
                rel = struct.unpack_from("<i", data, i + 1)[0]
                if va + 5 + rel == target:
                    hits.append((va, "jmp near"))
            elif b == 0xEB:
                rel = data[i + 1]
                if rel > 127:
                    rel -= 256
                if va + 2 + rel == target:
                    hits.append((va, "jmp short"))
    return hits


for target in [0x52367f, 0x520910, 0x523600, 0x523650, 0x4a0a00, 0x53d940]:
    c = find_callers(target)
    j = find_jmps(target)
    print(f"{hex(target)}: calls={len(c)} jmps={len(j)}")
    for h in c[:8]:
        print("  call from", hex(h))
    for h, t in j[:8]:
        print("  jmp from", hex(h), t)

# find func containing 52367f
off = va_to_off(0x523600)
for i in range(0, 0x400):
    va = 0x523600 + i
    o = va_to_off(va)
    if data[o] == 0x55 and data[o + 1] == 0x8B and data[o + 2] == 0xEC:
        print("push ebp at", hex(va))
        break

c640 = find_callers(0x523640)
print("callers of 523640:", [hex(x) for x in c640[:15]])

print("\n523500-523700 annotated:")
for va in range(0x523500, 0x523700):
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
