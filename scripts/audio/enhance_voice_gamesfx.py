"""Enhance Warcraft II Remastered Gamesfx voice WAVs (vanilla format preserved).

Chain: trim → gentle rumble cut → mud trim → warm presence → harsh tame →
light compression → loudness-matched blend. Goal: clearer voice, same volume,
no harsh/shiny artifacts on 22050 Hz mono game VO.
"""
from __future__ import annotations

import argparse
import json
import math
import wave
from pathlib import Path

import numpy as np
from scipy import signal

# v3: clarity without extra loudness or harsh highs ("blikkerig").
DRY_WET = 0.30
HIGHPASS_HZ = 82.0
MUD_FREQ = 290.0
MUD_Q = 1.0
MUD_CUT_DB = -1.8
WARMTH_FREQ = 2200.0
WARMTH_Q = 1.2
WARMTH_BOOST_DB = 2.2
HARSH_FREQ = 5200.0
HARSH_Q = 1.6
HARSH_CUT_DB = -3.2
COMPRESS_DRIVE = 1.38
RMS_MATCH_MAX_DB = 0.55
PEAK_DB = -2.2
MAX_PEAK_BOOST = 1.05

SKIP_DIRS = frozenset({"Misc", "Spells", "Bldg"})


def read_wav(path: Path) -> tuple[np.ndarray, int, int]:
    with wave.open(str(path), "rb") as w:
        ch = w.getnchannels()
        sr = w.getframerate()
        sw = w.getsampwidth()
        nframes = w.getnframes()
        raw = w.readframes(nframes)
    if sw != 2:
        raise ValueError(f"{path}: unsupported sample width {sw}")
    data = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    if ch > 1:
        data = data.reshape(-1, ch)
    else:
        data = data.reshape(-1, 1)
    return data, sr, ch


def write_wav(path: Path, data: np.ndarray, sr: int, ch: int) -> None:
    if data.ndim == 1:
        data = data.reshape(-1, 1)
    pcm = np.clip(data, -1.0, 1.0)
    pcm_i16 = (pcm * 32767.0).astype(np.int16)
    interleaved = pcm_i16.reshape(-1) if ch == 1 else pcm_i16.reshape(-1)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(ch)
        w.setframerate(sr)
        w.setsampwidth(2)
        w.writeframes(interleaved.tobytes())


def trim_silence(x: np.ndarray, sr: int, thresh: float = 0.004, pad_ms: int = 24) -> np.ndarray:
    mono = x.mean(axis=1)
    idx = np.where(np.abs(mono) > thresh)[0]
    if idx.size == 0:
        return x
    pad = int(sr * pad_ms / 1000)
    start = max(0, int(idx[0]) - pad)
    end = min(len(mono), int(idx[-1]) + pad + 1)
    return x[start:end]


def _sos_filter(x: np.ndarray, sos: np.ndarray) -> np.ndarray:
    out = np.empty_like(x)
    for c in range(x.shape[1]):
        out[:, c] = signal.sosfilt(sos, x[:, c])
    return out


def highpass(x: np.ndarray, sr: int, cutoff: float = HIGHPASS_HZ) -> np.ndarray:
    sos = signal.butter(2, cutoff, btype="high", fs=sr, output="sos")
    return _sos_filter(x, sos)


def peaking_eq(x: np.ndarray, sr: int, freq: float, q: float, gain_db: float) -> np.ndarray:
    if abs(gain_db) < 0.05:
        return x
    w0 = 2.0 * math.pi * freq / sr
    cos_w0 = math.cos(w0)
    sin_w0 = math.sin(w0)
    alpha = sin_w0 / (2.0 * q)
    a = 10 ** (gain_db / 40.0)
    b0 = 1 + alpha * a
    b1 = -2 * cos_w0
    b2 = 1 - alpha * a
    a0 = 1 + alpha / a
    a1 = -2 * cos_w0
    a2 = 1 - alpha / a
    sos = np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])
    return _sos_filter(x, sos)


def high_shelf(x: np.ndarray, sr: int, freq: float, gain_db: float) -> np.ndarray:
    if abs(gain_db) < 0.05:
        return x
    w0 = 2.0 * math.pi * freq / sr
    cos_w0 = math.cos(w0)
    sin_w0 = math.sin(w0)
    a = 10 ** (gain_db / 40.0)
    alpha = sin_w0 / 2.0 * math.sqrt((a + 1.0 / a) * (1.0 / 0.707 - 1.0) + 2.0)
    b0 = a * ((a + 1) + (a - 1) * cos_w0 + 2 * math.sqrt(a) * alpha)
    b1 = -2 * a * ((a - 1) + (a + 1) * cos_w0)
    b2 = a * ((a + 1) + (a - 1) * cos_w0 - 2 * math.sqrt(a) * alpha)
    a0 = (a + 1) - (a - 1) * cos_w0 + 2 * math.sqrt(a) * alpha
    a1 = 2 * ((a - 1) - (a + 1) * cos_w0)
    a2 = (a + 1) - (a - 1) * cos_w0 - 2 * math.sqrt(a) * alpha
    sos = np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])
    return _sos_filter(x, sos)


def mono_rms(x: np.ndarray) -> float:
    mono = x.mean(axis=1)
    return float(np.sqrt(np.mean(mono * mono)))


def match_rms(x: np.ndarray, ref: np.ndarray, max_boost_db: float = RMS_MATCH_MAX_DB) -> np.ndarray:
    rx = mono_rms(x)
    rr = mono_rms(ref)
    if rx < 1e-9 or rr < 1e-9:
        return x
    max_ratio = 10 ** (max_boost_db / 20.0)
    return x * min(rr / rx, max_ratio)


def compress(x: np.ndarray, drive: float = COMPRESS_DRIVE) -> np.ndarray:
    return np.tanh(drive * x) / np.tanh(drive)


def apply_peak_limit(x: np.ndarray, dry: np.ndarray) -> np.ndarray:
    peak_target = 10 ** (PEAK_DB / 20.0)
    dry_peak = float(np.max(np.abs(dry)))
    cap = max(peak_target, dry_peak * MAX_PEAK_BOOST)
    mx = float(np.max(np.abs(x)))
    if mx > cap:
        x = x * (cap / mx)
    return x


def enhance(data: np.ndarray, sr: int) -> np.ndarray:
    x = trim_silence(data, sr)
    dry = x.copy()

    wet = highpass(x, sr)
    wet = peaking_eq(wet, sr, MUD_FREQ, MUD_Q, MUD_CUT_DB)
    wet = peaking_eq(wet, sr, WARMTH_FREQ, WARMTH_Q, WARMTH_BOOST_DB)
    wet = peaking_eq(wet, sr, min(HARSH_FREQ, sr * 0.42), HARSH_Q, HARSH_CUT_DB)
    wet = compress(wet)
    wet = match_rms(wet, dry, max_boost_db=0.25)

    out = dry * DRY_WET + wet * (1.0 - DRY_WET)
    return apply_peak_limit(out, dry)


def iter_voice_wavs(root: Path) -> list[Path]:
    files: list[Path] = []
    if not root.is_dir():
        return files
    for sub in sorted(root.iterdir()):
        if not sub.is_dir() or sub.name in SKIP_DIRS:
            continue
        for wav in sorted(sub.glob("*.wav")):
            files.append(wav)
    return files


def rel_under_gamesfx(path: Path, gamesfx_root: Path) -> str:
    return path.relative_to(gamesfx_root).as_posix()


def measure_delta(dry: np.ndarray, out: np.ndarray) -> dict[str, float]:
    d = dry.mean(axis=1)
    o = out.mean(axis=1)
    n = min(len(d), len(o))
    if n <= 1:
        return {"rms_delta_db": 0.0, "corr": 1.0}
    d, o = d[:n], o[:n]
    rms_d = float(np.sqrt(np.mean(d * d)))
    rms_o = float(np.sqrt(np.mean(o * o)))
    rms_delta_db = 20.0 * math.log10(rms_o / rms_d) if rms_d > 0 and rms_o > 0 else 0.0
    if np.std(d) < 1e-9 or np.std(o) < 1e-9:
        corr = 1.0
    else:
        corr = float(np.corrcoef(d, o)[0, 1])
    return {"rms_delta_db": round(rms_delta_db, 2), "corr": round(corr, 4)}


def main() -> None:
    parser = argparse.ArgumentParser(description="Enhance War2 Gamesfx voice WAVs.")
    parser.add_argument("--input", type=Path, required=True, help="Vanilla Gamesfx root.")
    parser.add_argument("--output", type=Path, required=True, help="Output mirror.")
    parser.add_argument("--report", type=Path, default=Path("enhance-report.json"))
    args = parser.parse_args()

    input_root = args.input.resolve()
    output_root = args.output.resolve()
    report_path = args.report.resolve()

    if not input_root.is_dir():
        raise SystemExit(f"Input not found: {input_root}")

    entries: list[dict] = []
    errors: list[str] = []

    for src in iter_voice_wavs(input_root):
        rel = rel_under_gamesfx(src, input_root)
        dst = output_root / rel
        try:
            data, sr, ch = read_wav(src)
            dur_before = data.shape[0] / sr
            dry = trim_silence(data, sr)
            out = enhance(data, sr)
            dur_after = out.shape[0] / sr
            write_wav(dst, out, sr, ch)
            delta = measure_delta(dry, out)
            entries.append(
                {
                    "file": rel,
                    "sr": sr,
                    "channels": ch,
                    "duration_before_s": round(dur_before, 4),
                    "duration_after_s": round(dur_after, 4),
                    "duration_delta_pct": round((dur_after - dur_before) / dur_before * 100, 2) if dur_before else 0,
                    "bytes_in": src.stat().st_size,
                    "bytes_out": dst.stat().st_size,
                    **delta,
                }
            )
            print(f"OK {rel}  dB {delta['rms_delta_db']:+.1f}  r={delta['corr']:.3f}")
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{rel}: {exc}")
            print(f"FAIL {rel}: {exc}")

    avg_db = round(sum(e["rms_delta_db"] for e in entries) / max(len(entries), 1), 2)
    report = {
        "input": str(input_root),
        "output": str(output_root),
        "processed": len(entries),
        "avg_rms_delta_db": avg_db,
        "chain": {
            "dry_wet": DRY_WET,
            "highpass_hz": HIGHPASS_HZ,
            "mud_cut_db": MUD_CUT_DB,
            "warmth_boost_db": WARMTH_BOOST_DB,
            "harsh_cut_db": HARSH_CUT_DB,
            "rms_match_max_db": RMS_MATCH_MAX_DB,
        },
        "errors": errors,
        "entries": entries,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Report: {report_path}  (avg {avg_db:+.1f} dB)")
    if errors:
        raise SystemExit(f"{len(errors)} file(s) failed")


if __name__ == "__main__":
    main()
