#!/usr/bin/env python3
"""Compare a hardware recording of Audrey II with the plugin's reference render.

The instrument is a noise-driven, saturating feedback loop, so two runs never match
sample by sample. We compare statistics instead: level, spectral balance (1/3-octave
bands), spectral centroid, stereo width and level stability. The spread between two
halves of the *same* recording is reported as the "run-to-run" yardstick: differences
smaller than that cannot be told apart from normal variation.

Single pair:  compare.py hw.wav ref.wav [-o report.png]
Batch:        compare.py --hw-dir recordings/ --ref-dir reference/ -o report_dir/
              (files are paired by name; recordings may have a suffix, e.g. p01_mid_drone_hw.wav)
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

BANDS = 10 ** (np.arange(-14, 14) / 10.0) * 1000.0           # 1/3-octave centres, ~ 40 Hz .. 20 kHz
BANDS = BANDS[(BANDS >= 40) & (BANDS <= 16000)]


def load(path, target_sr=48000):
    sr, x = wavfile.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / float(np.iinfo(x.dtype).max)
    elif x.dtype.kind == "u":
        x = (x.astype(np.float64) - 128.0) / 128.0
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    x = x[:, :2]
    if sr != target_sr:
        g = math.gcd(sr, target_sr)
        x = signal.resample_poly(x, target_sr // g, sr // g, axis=0)
    return x


def band_levels(mono, sr=48000):
    f, p = signal.welch(mono, sr, nperseg=16384, noverlap=8192)
    out = []
    for c in BANDS:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        m = (f >= lo) & (f < hi)
        out.append(10 * np.log10(p[m].sum() * (f[1] - f[0]) + 1e-20) if m.any() else -200.0)
    return np.array(out)


def centroid(mono, sr=48000):
    f, p = signal.welch(mono, sr, nperseg=16384)
    return float((f * p).sum() / (p.sum() + 1e-20))


def metrics(x, sr=48000):
    mono = x.mean(axis=1)
    rms = float(np.sqrt((x ** 2).mean()))
    win = sr  # 1 s level track
    n = len(mono) // win
    track = np.array([20 * np.log10(np.sqrt((mono[i * win:(i + 1) * win] ** 2).mean()) + 1e-12) for i in range(n)])
    l, r = x[:, 0], x[:, 1]
    corr = float(np.corrcoef(l, r)[0, 1]) if l.std() > 1e-9 and r.std() > 1e-9 else 1.0
    return dict(
        rms_db=20 * np.log10(rms + 1e-12), peak_db=20 * np.log10(np.abs(x).max() + 1e-12),
        centroid=centroid(mono), corr=corr, level_std=float(track.std()) if n > 1 else 0.0,
        bands=band_levels(mono), track=track)


def window(x, start, dur, sr=48000):
    a = int(start * sr)
    b = min(len(x), a + int(dur * sr))
    return x[a:b]


FLOOR_DB = 50.0  # bands more than this far below a signal's loudest band are treated as "empty"


def band_diff(a, b):
    """Spectral-shape difference after removing the overall level offset.

    The drone is very tonal: most 1/3-octave bands hold only numerical or recording noise.
    Each signal is floored at FLOOR_DB below its own loudest band so those bands cannot
    produce fake differences, and the offset is the median over the bands that matter.
    Returns (rms, worst, per-band difference, mask of bands that matter)."""
    fa, fb = np.maximum(a, a.max() - FLOOR_DB), np.maximum(b, b.max() - FLOOR_DB)
    valid = (a > a.max() - FLOOR_DB) | (b > b.max() - FLOOR_DB)
    d = fa - fb
    d = d - np.median(d[valid])
    dv = d[valid]
    return float(np.sqrt((dv ** 2).mean())), float(np.abs(dv).max()), d, valid


def compare(hw, ref, start, dur):
    hw_w, ref_w = window(hw, start, dur), window(ref, start, dur)
    mh, mr = metrics(hw_w), metrics(ref_w)
    # run-to-run yardstick: two halves of the hardware recording
    half = len(hw_w) // 2
    h1, h2 = metrics(hw_w[:half]), metrics(hw_w[half:])
    rr_rms, rr_max, _, _ = band_diff(h1["bands"], h2["bands"])
    rms, mx, d, valid = band_diff(mh["bands"], mr["bands"])
    return dict(hw=mh, ref=mr, shape_rms=rms, shape_max=mx, rr_rms=rr_rms, rr_max=rr_max,
                level_off=mh["rms_db"] - mr["rms_db"], diff=d, valid=valid)


def tolerance(c):
    return min(6.0, max(3.0, c["rr_rms"] + 1.5))


def verdict(c):
    # Anything within the recording's own run-to-run spread (+1.5 dB slack, 3..6 dB) counts as a match.
    tol = tolerance(c)
    if c["shape_rms"] <= tol:
        return "MATCH"
    if c["shape_rms"] <= 2 * tol:
        return "CLOSE"
    return "DIFFERENT"


def report_text(name, c):
    h, r = c["hw"], c["ref"]
    lines = [
        f"== {name}",
        f"  level      hw {h['rms_db']:7.1f} dBFS   plugin {r['rms_db']:7.1f} dBFS   offset {c['level_off']:+.1f} dB"
        "   (level depends on your interface gain: judge the spectrum first)",
        f"  peak       hw {h['peak_db']:7.1f} dBFS   plugin {r['peak_db']:7.1f} dBFS",
        f"  centroid   hw {h['centroid']:7.0f} Hz     plugin {r['centroid']:7.0f} Hz",
        f"  L/R corr   hw {h['corr']:7.2f}        plugin {r['corr']:7.2f}",
        f"  level wobble (std of 1 s levels)  hw {h['level_std']:.2f} dB   plugin {r['level_std']:.2f} dB",
        f"  spectral shape difference: rms {c['shape_rms']:.1f} dB, worst band {c['shape_max']:.1f} dB"
        f"   (run-to-run spread of the recording itself: rms {c['rr_rms']:.1f} dB)",
        f"  verdict: {verdict(c)}",
    ]
    return "\n".join(lines)


def plot(name, c, out):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(1, 3, figsize=(15, 4), gridspec_kw={"width_ratios": [2.2, 1.4, 1.4]})
    h, r = c["hw"], c["ref"]
    ax[0].semilogx(BANDS, h["bands"], "o-", label="hardware", color="#d9534f")
    ax[0].semilogx(BANDS, r["bands"] + c["level_off"], "o-", label="plugin (level-matched)", color="#5cb85c")
    ax[0].set_title(f"{name}: 1/3-octave spectrum"); ax[0].set_xlabel("Hz"); ax[0].set_ylabel("dB")
    ax[0].grid(True, which="both", alpha=.3); ax[0].legend()
    d = np.where(c["valid"], c["diff"], np.nan)
    tol = tolerance(c)
    ax[1].semilogx(BANDS, d, "o-", color="#555")
    ax[1].axhspan(-tol, tol, color="#5cb85c", alpha=.15, label="tolerance")
    ax[1].axhline(0, color="k", lw=.5); ax[1].set_title("hardware - plugin (shape)"); ax[1].set_xlabel("Hz"); ax[1].set_ylabel("dB")
    ax[1].grid(True, which="both", alpha=.3); ax[1].legend()
    ax[2].plot(h["track"], label="hardware", color="#d9534f"); ax[2].plot(r["track"], label="plugin", color="#5cb85c")
    ax[2].set_title("level per second"); ax[2].set_xlabel("s"); ax[2].set_ylabel("dBFS"); ax[2].grid(alpha=.3); ax[2].legend()
    fig.suptitle(f"verdict: {verdict(c)}")
    fig.tight_layout(); fig.savefig(out, dpi=110); plt.close(fig)


def pair_files(hw_dir, ref_dir):
    pairs = []
    for ref in sorted(Path(ref_dir).glob("*.wav")):
        cands = sorted(Path(hw_dir).glob(ref.stem + "*.wav"))
        if cands:
            pairs.append((ref.stem, cands[0], ref))
        else:
            print(f"(no recording found for {ref.stem})", file=sys.stderr)
    return pairs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hw", nargs="?"); ap.add_argument("ref", nargs="?")
    ap.add_argument("--hw-dir"); ap.add_argument("--ref-dir")
    ap.add_argument("-o", "--out", help="PNG (single pair) or directory (batch)")
    ap.add_argument("--start", type=float, default=5.0, help="analysis start in s (skip the build-up)")
    ap.add_argument("--dur", type=float, default=12.0, help="analysis length in s")
    a = ap.parse_args()

    if a.hw_dir and a.ref_dir:
        pairs = pair_files(a.hw_dir, a.ref_dir)
    elif a.hw and a.ref:
        pairs = [(Path(a.hw).stem, Path(a.hw), Path(a.ref))]
    else:
        ap.error("give hw.wav ref.wav, or --hw-dir and --ref-dir")

    rows = []
    for name, hwp, refp in pairs:
        c = compare(load(hwp), load(refp), a.start, a.dur)
        print(report_text(name, c)); print()
        rows.append((name, c))
        if a.out:
            out = Path(a.out)
            if a.hw_dir:
                out.mkdir(parents=True, exist_ok=True); out = out / f"{name}.png"
            plot(name, c, out)
    if len(rows) > 1:
        print("summary:")
        for name, c in rows:
            print(f"  {name:20s} shape rms {c['shape_rms']:4.1f} dB  (rr {c['rr_rms']:.1f})  {verdict(c)}")


if __name__ == "__main__":
    main()
