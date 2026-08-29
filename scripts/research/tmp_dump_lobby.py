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


def dump(va, n=80):
    off = va_to_off(va)
    if off is None:
        print(f"{hex(va)}: not found")
        return
    chunk = data[off:off+n]
    print(f"{hex(va)}:")
    for i in range(0, len(chunk), 16):
        line = chunk[i:i+16]
        print("  " + " ".join(f"{b:02x}" for b in line))


for va in [0x533020, 0x4D1380, 0x4D1280, 0x4D1680, 0x533100, 0x5367c0, 0x5367c6, 0x525a10]:
    dump(va)
