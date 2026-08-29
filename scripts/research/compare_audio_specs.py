"""Compare vanilla vs mod WAV specs."""
import struct
import os
from pathlib import Path

VANILLA_FOOTMAN = Path(r"C:\Program Files (x86)\Warcraft II Remastered\x86\Data\Gamesfx\Human")
VANILLA_KNIGHT = Path(r"C:\Program Files (x86)\Warcraft II Remastered\x86\Data\Gamesfx\Knight")
MOD_FOOTMAN = Path(r"C:\Users\mauri\Projects\war_2mod\mod\assets\audio\human_footman")
MOD_KNIGHT = Path(r"C:\Users\mauri\Projects\war_2mod\mod\assets\audio\human_knight")

FOOTMAN = [
    "Hpissed1.wav", "Hpissed2.wav", "Hpissed3.wav", "Hpissed4.wav",
    "Hpissed5.wav", "Hpissed6.wav", "Hpissed7.wav",
    "Hready.wav",
    "Hwhat1.wav", "Hwhat2.wav", "Hwhat3.wav", "Hwhat4.wav", "Hwhat5.wav", "Hwhat6.wav",
    "Hyessir1.wav", "Hyessir2.wav", "Hyessir3.wav", "Hyessir4.wav",
]
KNIGHT = [
    "Knpissd1.wav", "Knpissd2.wav", "Knpissd3.wav",
    "Knready.wav",
    "Knwhat1.wav", "Knwhat2.wav", "Knwhat3.wav", "Knwhat4.wav",
    "Knyessr1.wav", "Knyessr2.wav", "Knyessr3.wav", "Knyessr4.wav",
]

CODECS = {1: "PCM", 3: "IEEE float"}


def read_wav(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 44 or data[:4] != b"RIFF":
        return {"error": "not RIFF"}

    info = {"size": len(data)}
    pos = 12
    fmt = None
    data_bytes = 0

    while pos + 8 <= len(data):
        chunk_id = data[pos:pos + 4]
        chunk_sz = struct.unpack_from("<I", data, pos + 4)[0]
        chunk_data = data[pos + 8:pos + 8 + chunk_sz]
        if chunk_id == b"fmt ":
            audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample = struct.unpack_from(
                "<HHIIHH", chunk_data, 0
            )
            fmt = {
                "codec": CODECS.get(audio_format, f"fmt{audio_format}"),
                "channels": channels,
                "sample_rate": sample_rate,
                "byte_rate": byte_rate,
                "block_align": block_align,
                "bits_per_sample": bits_per_sample,
            }
        elif chunk_id == b"data":
            data_bytes = chunk_sz
        pos += 8 + chunk_sz
        if chunk_sz % 2:
            pos += 1

    if not fmt:
        info["error"] = "no fmt chunk"
        return info

    info.update(fmt)
    if fmt["byte_rate"]:
        info["duration_ms"] = round(data_bytes / fmt["byte_rate"] * 1000, 1)
    else:
        info["duration_ms"] = None
    info["data_bytes"] = data_bytes
    return info


def fmt_row(name, v, m):
    def s(d, k):
        if "error" in d:
            return d["error"]
        return d.get(k, "?")

    same_fmt = (
        v.get("codec") == m.get("codec")
        and v.get("channels") == m.get("channels")
        and v.get("sample_rate") == m.get("sample_rate")
        and v.get("bits_per_sample") == m.get("bits_per_sample")
    )
    flag = "OK" if same_fmt and "error" not in v and "error" not in m else "DIFF"

    return {
        "file": name,
        "flag": flag,
        "v_size": v.get("size"),
        "m_size": m.get("size"),
        "v_codec": s(v, "codec"),
        "m_codec": s(m, "codec"),
        "v_ch": s(v, "channels"),
        "m_ch": s(m, "channels"),
        "v_rate": s(v, "sample_rate"),
        "m_rate": s(m, "sample_rate"),
        "v_bits": s(v, "bits_per_sample"),
        "m_bits": s(m, "bits_per_sample"),
        "v_dur": s(v, "duration_ms"),
        "m_dur": s(m, "duration_ms"),
    }


def compare_group(label, vanilla_dir, mod_dir, files):
    print(f"\n{'='*80}")
    print(f"{label}")
    print(f"{'='*80}")
    rows = []
    for f in files:
        vp = vanilla_dir / f
        mp = mod_dir / f
        if not vp.exists():
            v = {"error": "missing vanilla"}
        else:
            v = read_wav(vp)
        if not mp.exists():
            m = {"error": "missing mod"}
        else:
            m = read_wav(mp)
        rows.append(fmt_row(f, v, m))

    hdr = (
        f"{'File':<14} {'Flag':<5} "
        f"{'V_size':>7} {'M_size':>7} "
        f"{'V_codec':<6} {'M_codec':<6} "
        f"{'V_ch':>4} {'M_ch':>4} "
        f"{'V_Hz':>6} {'M_Hz':>6} "
        f"{'V_bit':>4} {'M_bit':>4} "
        f"{'V_ms':>7} {'M_ms':>7}"
    )
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        print(
            f"{r['file']:<14} {r['flag']:<5} "
            f"{str(r['v_size']):>7} {str(r['m_size']):>7} "
            f"{str(r['v_codec']):<6} {str(r['m_codec']):<6} "
            f"{str(r['v_ch']):>4} {str(r['m_ch']):>4} "
            f"{str(r['v_rate']):>6} {str(r['m_rate']):>6} "
            f"{str(r['v_bits']):>4} {str(r['m_bits']):>4} "
            f"{str(r['v_dur']):>7} {str(r['m_dur']):>7}"
        )

    diff_fmt = sum(1 for r in rows if r["flag"] == "DIFF")
    print(f"\nFormat match: {len(rows) - diff_fmt}/{len(rows)}  |  Format differs: {diff_fmt}")


compare_group("Human Footman", VANILLA_FOOTMAN, MOD_FOOTMAN, FOOTMAN)
compare_group("Human Knight", VANILLA_KNIGHT, MOD_KNIGHT, KNIGHT)

# Summary of unique vanilla formats
print("\n" + "=" * 80)
print("VANILLA FORMAT SUMMARY (all mod files)")
print("=" * 80)
all_specs = set()
for d, files in [(VANILLA_FOOTMAN, FOOTMAN), (VANILLA_KNIGHT, KNIGHT)]:
    for f in files:
        p = d / f
        if p.exists():
            w = read_wav(p)
            if "error" not in w:
                all_specs.add((w["codec"], w["channels"], w["sample_rate"], w["bits_per_sample"]))
for s in sorted(all_specs):
    print(f"  {s[0]}, {s[1]}ch, {s[2]} Hz, {s[3]} bit")

mod_specs = set()
for d, files in [(MOD_FOOTMAN, FOOTMAN), (MOD_KNIGHT, KNIGHT)]:
    for f in files:
        p = d / f
        if p.exists():
            w = read_wav(p)
            if "error" not in w:
                mod_specs.add((w["codec"], w["channels"], w["sample_rate"], w["bits_per_sample"]))
print("\nMOD FORMAT SUMMARY")
for s in sorted(mod_specs):
    print(f"  {s[0]}, {s[1]}ch, {s[2]} Hz, {s[3]} bit")
