# Locate the sound-variant tables in "Warcraft II.exe": find the string
# "Gamesfx\human\hwhat1.wav", the pointer(s) to it, and dump surrounding
# dwords to reveal the table layout (pointer list + count fields).
import struct

import pefile

EXE = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase

# map file <-> VA through sections
secs = []
for s in pe.sections:
    secs.append((base + s.VirtualAddress, s.get_data(), s.Name.decode(errors="ignore").strip("\x00")))

def find_string(needle: bytes):
    out = []
    for va, data, name in secs:
        i = 0
        while True:
            i = data.find(needle, i)
            if i < 0:
                break
            out.append((va + i, name))
            i += 1
    return out

def find_dword(value: int):
    imm = struct.pack("<I", value)
    out = []
    for va, data, name in secs:
        i = 0
        while True:
            i = data.find(imm, i)
            if i < 0:
                break
            out.append((va + i, name, data, i))
            i += 4 - (i % 4) if False else 1
    return out

def read_dword(va: int):
    for sva, data, _ in secs:
        off = va - sva
        if 0 <= off <= len(data) - 4:
            return struct.unpack_from("<I", data, off)[0]
    return None

def read_cstr(va: int, cap=64):
    for sva, data, _ in secs:
        off = va - sva
        if 0 <= off < len(data):
            end = data.find(b"\x00", off, off + cap)
            if end < 0:
                end = off + cap
            return data[off:end].decode(errors="replace")
    return "?"

for probe in (b"Gamesfx\\human\\hwhat1.wav", b"Gamesfx\\knight\\knwhat1.wav",
              b"Gamesfx\\knight\\knyessr1.wav"):
    locs = find_string(probe + b"\x00")
    print(f"\n=== string {probe.decode()} at {[hex(v) for v, _ in locs]}")
    for sva, secname in locs:
        refs = find_dword(sva)
        for rva_addr, rsec, rdata, roff in refs:
            print(f"  pointer at {hex(rva_addr)} ({rsec}); context dwords:")
            ctx_start = roff - 24
            for k in range(-6, 14):
                off = roff + k * 4
                if off < 0 or off + 4 > len(rdata):
                    continue
                val = struct.unpack_from("<I", rdata, off)[0]
                note = ""
                if base <= val < base + 0x01000000:
                    s = read_cstr(val)
                    if s and (".wav" in s.lower() or s.isprintable() and len(s) > 3):
                        note = f'-> "{s}"'
                print(f"    [{hex(rva_addr + k*4)}] {val:#010x} {note}")
