# Locate the victory/defeat end-game popup construction in "Warcraft II.exe".
# The frontend registers screens in static descriptors next to the name
# strings ('victory', 'victory_btn', 'defeat', 'defeat_btn' @ ~file 0x447774).
# Find the descriptor layout and every code xref to those strings so we can
# identify the screen constructor (candidate hook site for an added
# "Observe" button under the Exit Game button).
import struct

import pefile

EXE = r"C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase

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
            # only exact string starts (preceded by NUL or start)
            if i == 0 or data[i - 1] == 0:
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
            out.append((va + i, name))
            i += 1
    return out

def read_dword(va: int):
    for sva, data, _ in secs:
        off = va - sva
        if 0 <= off <= len(data) - 4:
            return struct.unpack_from("<I", data, off)[0]
    return None

def read_cstr(va: int, cap=48):
    for sva, data, _ in secs:
        off = va - sva
        if 0 <= off < len(data):
            end = data.find(b"\x00", off, off + cap)
            if end < 0:
                end = off + cap
            return data[off:end].decode(errors="replace")
    return "?"

for probe in (b"victory", b"victory_btn", b"defeat", b"defeat_btn",
              b"exit_game", b"returntogame", b"pause_menu", b"quittomenu"):
    hits = [h for h in find_string(probe + b"\x00")]
    for va, sec in hits:
        refs = find_dword(va)
        ref_desc = ", ".join(f"{r:08X}({n})" for r, n in refs[:12])
        print(f"str {probe.decode():<16} @ {va:08X} [{sec}] refs: {ref_desc}")

# Dump the descriptor block that follows the victory/defeat strings so the
# layout (name ptr + handler fn ptrs) becomes visible.
print("\n--- descriptor dwords after 'victory' string block ---")
vic = find_string(b"victory\x00")
if vic:
    va0 = vic[0][0]
    # descriptors sit within ~0x100 bytes after the strings
    for off in range(0, 0x120, 4):
        v = read_dword(va0 + 0x30 + off)
        if v is None:
            break
        note = ""
        if v and 0x400000 <= v <= 0x900000:
            s = read_cstr(v)
            if s and all(32 <= ord(c) < 127 for c in s) and len(s) > 2:
                note = f"-> '{s}'"
            elif any(sva <= v < sva + len(d) for sva, d, n in secs if n == ".text"):
                note = "-> .text fn?"
        print(f"{va0 + 0x30 + off:08X}: {v:08X} {note}")
