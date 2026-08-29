import struct

path = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
data = open(path, "rb").read()
e = struct.unpack_from("<I", data, 0x3C)[0]
base = struct.unpack_from("<I", data, e + 24 + 28)[0]
ns = struct.unpack_from("<H", data, e + 6)[0]
so = struct.unpack_from("<H", data, e + 20)[0]
sec = e + 24 + so
secs = []
for i in range(ns):
    o = sec + i * 40
    vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
    secs.append((va, rawptr, rawsize))


def va_to_off(va):
    rva = va - base
    for sva, rp, rs in secs:
        if sva <= rva < sva + rs:
            return rp + (rva - sva)
    return None


def off_to_va(off):
    for sva, rp, rs in secs:
        if rp <= off < rp + rs:
            return base + sva + (off - rp)
    return None


def dump(va, n=80):
    o = va_to_off(va)
    print(hex(va), data[o:o + n].hex(" "))


dump(0x547260)
dump(0x5aba00)
idx = 0
needle = struct.pack("<I", 0x0095D280)
while True:
    j = data.find(needle, idx)
    if j < 0:
        break
    va = off_to_va(j)
    if va and 0x500000 <= va <= 0x560000:
        print("95d280 ref", hex(va))
    idx = j + 1

# search lobby_chat_history string push
needle = struct.pack("<I", 0x00844B88)
j = data.find(needle)
while j >= 0:
    va = off_to_va(j)
    if va and 0x520000 <= va <= 0x525000:
        print("lobby_chat_history push at", hex(va))
    j = data.find(needle, j + 1)
