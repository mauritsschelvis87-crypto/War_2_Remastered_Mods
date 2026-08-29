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
    name = data[o : o + 8].split(b"\0")[0].decode()
    vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
    sections.append((name, va, vsize, rawptr, rawsize))


def va_to_off(va):
    rva = va - image_base
    for name, sva, vsize, rawptr, rawsize in sections:
        if sva <= rva < sva + vsize:
            return rawptr + (rva - sva)
    return None


def dump(va, n=32):
    off = va_to_off(va)
    chunk = data[off:off+n]
    print(f"{hex(va)}: " + " ".join(f"{b:02x}" for b in chunk))
    if chunk[0] == 0xE8:
        rel = struct.unpack_from("<i", chunk, 1)[0]
        target = va + 5 + rel
        print(f"  call -> {hex(target)}")


for va in [0x4D128E, 0x4D12B4, 0x4D1315, 0x533103]:
    dump(va)
