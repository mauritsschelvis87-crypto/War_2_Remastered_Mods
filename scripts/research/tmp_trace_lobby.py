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


hits = find_callers(0x4c48e0)
print("callers of 4c48e0:", len(hits))
for h in hits:
    o = va_to_off(h)
    note = ""
    for back in range(1, 12):
        oo = o - back
        if data[oo] == 0x6a:
            note = f" push {data[oo + 1]}"
            break
        if data[oo] == 0xb8:
            note = f" eax={struct.unpack_from('<I', data, oo + 1)[0]}"
            break
    print(hex(h), note)

print("\npush 17 / eax=17 before 4c48e0:")
for h in hits:
    o = va_to_off(h)
    for back in range(1, 15):
        oo = o - back
        if data[oo] == 0x6a and data[oo + 1] == 0x17:
            print("  push 17 at", hex(h))
        if data[oo] == 0xb8 and struct.unpack_from("<I", data, oo + 1)[0] == 0x17:
            print("  mov eax,17 at", hex(h))

print("\n4c48b0 region:")
for va in range(0x4c48b0, 0x4c4930):
    o = va_to_off(va)
    b = data[o]
    line = hex(va)
    if b == 0x6a:
        line += " push " + str(data[o + 1])
    elif b == 0xe8:
        rel = struct.unpack_from("<i", data, o + 1)[0]
        line += " call " + hex(va + 5 + rel)
    elif b == 0xa3:
        imm = struct.unpack_from("<I", data, o + 1)[0]
        line += " mov [" + hex(imm) + "], eax"
    if "push" in line or "call" in line or "mov [" in line:
        print(line)
