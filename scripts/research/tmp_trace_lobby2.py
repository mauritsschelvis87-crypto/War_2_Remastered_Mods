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
        elif b == 0xe9:
            rel = struct.unpack_from("<i", data, o + 1)[0]
            line += " jmp " + hex(va + 5 + rel)
        elif b == 0xeb:
            rel = data[o + 1]
            if rel > 127:
                rel -= 256
            line += " jmp short " + hex(va + 2 + rel)
        if any(x in line for x in ("push", "call", "jmp")):
            print(line)


for site in [0x520500, 0x5209c0]:
    print(f"\n=== context {hex(site)} ===")
    annotate_range(site, site + 0x80)

for site in [0x52053d, 0x520a04]:
  hits = find_callers(site)
  print(f"\ncallers into func containing {hex(site)}:")
  # find func start
  off = va_to_off(site)
  start = off
  for i in range(off, off - 0x2000, -1):
        if data[i] == 0x55 and data[i + 1] == 0x8B and data[i + 2] == 0xEC:
            start = i
            break
  func = off_to_va(start)
  print("  func start", hex(func))
  callers = find_callers(func)
  print("  callers of func:", [hex(c) for c in callers[:15]])

# 5362E0 lobby layout - chat_history widget path at 536abd
print("\n=== lobby layout chat area 536a80-536b20 ===")
annotate_range(0x536a80, 0x536b20)

# 536570 area compose + refresh
print("\n=== lobby layout slot refresh 536520-536580 ===")
annotate_range(0x536520, 0x536580)
