import struct
import sys

path = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
data = open(path, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
opt = e_lfanew + 24
magic = struct.unpack_from("<H", data, opt)[0]
if magic != 0x10B:
    sys.exit("not pe32")
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


def off_to_va(off):
    for name, sva, vsize, rawptr, rawsize in sections:
        if rawptr <= off < rawptr + rawsize:
            return image_base + sva + (off - rawptr)
    return None


target = 0x536270
hits = []
for name, sva, vsize, rawptr, rawsize in sections:
    if name != ".text":
        continue
    for i in range(rawptr, rawptr + rawsize - 5):
        if data[i] != 0xE8:
            continue
        rel = struct.unpack_from("<i", data, i + 1)[0]
        va = off_to_va(i)
        if va is None:
            continue
        if va + 5 + rel == target:
            hits.append(va)

print("append callers:", len(hits))
for h in hits:
    print(hex(h))

# Also find refs to ": " at 0x836970 (push imm32)
needle = struct.pack("<I", 0x00836970)
print("push :  sites:")
idx = 0
while True:
    j = data.find(needle, idx)
    if j < 0:
        break
    va = off_to_va(j)
    # only interesting if preceded by push (0x68)
    if va and j > 0 and data[j - 1] == 0x68:
        print(hex(va - 1))
    idx = j + 1
