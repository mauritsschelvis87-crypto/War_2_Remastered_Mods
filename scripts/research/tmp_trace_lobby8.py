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


def annotate(start, end):
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


print("=== 523690 callback 24 ===")
annotate(0x523690, 0x523720)

print("\n=== 523640 callback 23 ===")
annotate(0x523640, 0x523690)

print("\n=== 51f980 race handler ===")
annotate(0x51f980, 0x51fa80)

print("\ncallers 53b550 area:", [hex(h) for h in find_callers(0x53b550)[:8]])

# who dispatches callback table 844200
for g in [0x844200]:
    needle = struct.pack("<I", g)
    idx = 0
    while True:
        j = data.find(needle, idx)
        if j < 0:
            break
        va = image_base + (j - sections[0][4])  # wrong
        idx = j + 1

# search ff 24 85 with 844200
for name, sva, vsize, rawptr, rawsize in sections:
    if name != ".text":
        continue
    for i in range(rawptr, rawptr + rawsize - 7):
        if data[i:i+3] == bytes([0xff, 0x24, 0x85]):
            imm = struct.unpack_from("<I", data, i + 3)[0]
            if imm == 0x844200:
                from_off = i
                # get va
                for n2, sva2, vs2, rp2, rs2 in sections:
                    if rp2 <= from_off < rp2 + rs2:
                        print("callback dispatch at", hex(image_base + sva2 + (from_off - rp2)))
