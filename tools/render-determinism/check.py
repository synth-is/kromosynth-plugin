#!/usr/bin/env python3
"""Determinism of re-rendering, against rules pre-registered before any render ran.

The pricing rule "re-rendering a form you own is free, forever" is a promise
that the sound you paid for comes back. This measures whether it does.

PRE-REGISTERED 2026-09-16, before the first render:

  R1  same genome, same params, twice       -> BIT-IDENTICAL. No tolerance.
  R2  duration is time-scaling, not redraw  -> 8s resampled to 4s vs the 4s
                                               render: normalised cross-
                                               correlation >= 0.98 AND spectral
                                               centroid within 2%.
  R3  pitch is transposition, not redraw    -> +12 semitones: spectral centroid
                                               ratio tracks the 2.0x frequency
                                               ratio within 5%.

R1 has no tolerance because a deterministic DSP graph has no legitimate source
of variation. R2 and R3 have tolerances stated in advance because resampling
and transposition both alter samples for reasons that are not redrawing.

A failed rule is a result. THE THRESHOLDS ABOVE HAVE NOT BEEN TOUCHED SINCE.

── THE FIRST RUN WAS VOID, and both reasons are recorded rather than edited away

The first run reported R2 FAIL (centroid error 175.83%) and R3 FAIL (ratio
1.0050 where 2.0 was expected). NEITHER IS A RESULT. Two things were wrong, and
both were with the measurement rather than with the system under test:

1.  THE INSTRUMENT. The centroid was amplitude-weighted over the whole
    spectrum including DC. The fixture's spectral argmax was bin 0, so the
    measure was reporting the DC offset's position, not the sound's brightness.
    That is why +12 semitones moved the centroid by 0.5%: the DC block does not
    transpose.

2.  THE FIXTURE. reachability-legacy-rhythm/best-genome.json renders with a
    mean DC offset of +0.7425 against a peak of 0.847, and 99.3% of its
    spectral energy below 20 Hz. It is a DC block with a little audio riding on
    it, and it cannot answer a question about timbre under any measure.

    Worth keeping as a finding in its own right: this is the "best genome" of
    that run, and it corroborates the known DC-blindness of
    quality_musicality.py -- no offset term, so all four weighted measures
    reward DC. A scorer blind to DC selected a sound that is mostly DC.

Fixed by excluding bins below 20 Hz from the centroid and by choosing a fixture
with real audio content (reachability-legacy-rhythm-final: DC +0.0112 at peak
0.119, centroid 1094 Hz above 20 Hz). Fixing an instrument is legitimate;
retuning a threshold to rescue a failed rule is not, and the thresholds are as
first written.

── Reproducing

From kromosynth-cli/cli-app, for each (duration, note-delta) pair:

  node kromosynth.js render-audio --read-from-file <wrapped-genome.json> \
       --write-to-file /tmp/determinism/f_a1.wav --duration 4 --note-delta 0 \
       --velocity 1

The genome must be wrapped as {"genome": {...}} -- the CLI destructures
`genome.asNEATPatch` and fails with "Cannot destructure property" on a bare
genome file. `--play-on-default-audio-device=false` does NOT suppress playback
(meow reads the string "false" as truthy); use --no-play-on-default-audio-device,
which completes but leaves the process alive long enough to trip a 4-minute
MCP terminal timeout, so run it detached.
"""

import sys
import wave

import numpy as np
from scipy.signal import resample

BASE = "/tmp/determinism"

# Below this, a bin is DC or rumble and says nothing about brightness. The
# first run's whole failure was including bin 0.
CENTROID_FLOOR_HZ = 20.0


def read_wav(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        rate = w.getframerate()
        chans = w.getnchannels()
        raw = w.readframes(n)

    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0

    if chans > 1:
        x = x.reshape(-1, chans).mean(axis=1)

    return x, rate


def spectrum(x, rate):
    mag = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1.0 / rate)

    return mag, freqs


def centroid(x, rate):
    """Spectral centroid in Hz, ignoring everything below CENTROID_FLOOR_HZ."""
    mag, freqs = spectrum(x, rate)
    keep = freqs >= CENTROID_FLOOR_HZ

    total = mag[keep].sum()

    return float((freqs[keep] * mag[keep]).sum() / total) if total > 0 else 0.0


def dc_report(x, rate):
    mag, freqs = spectrum(x, rate)
    low = mag[freqs < CENTROID_FLOOR_HZ].sum() / mag.sum() * 100.0

    return x.mean(), np.abs(x).max(), low


def ncc(a, b):
    """Normalised cross-correlation at zero lag, both mean-removed.

    Zero lag rather than a best-lag search: a time-scaled render should align
    at zero by construction, and searching for the best offset would let a
    smeared result pass by finding the one shift where it happens to agree.
    """
    n = min(len(a), len(b))
    a = a[:n] - a[:n].mean()
    b = b[:n] - b[:n].mean()

    denom = np.sqrt((a * a).sum() * (b * b).sum())

    return float((a * b).sum() / denom) if denom > 0 else 0.0


def pct(expected, got):
    return abs(got - expected) / expected * 100.0 if expected else float("inf")


a1, rate = read_wav(f"{BASE}/f_a1.wav")
a2, _ = read_wav(f"{BASE}/f_a2.wav")
b8, _ = read_wav(f"{BASE}/f_b8.wav")
c12, _ = read_wav(f"{BASE}/f_c12.wav")

print("fixture: reachability-legacy-rhythm-final (legacy-2input, gen 293)")
print(f"rate={rate}  4s={len(a1)}  8s={len(b8)}  +12={len(c12)} samples")

mean, peak, low = dc_report(a1, rate)
print(f"fixture sanity: DC={mean:+.4f}  peak={peak:.3f}  "
      f"energy<{CENTROID_FLOOR_HZ:.0f}Hz={low:.1f}%")
print()

verdicts = []

# ── R1 ──────────────────────────────────────────────────────────────────────
same = bool(np.array_equal(a1, a2))
peak_diff = float(np.abs(a1 - a2).max()) if len(a1) == len(a2) else float("inf")

print("R1  reproducibility: same genome, same params, twice")
print(f"      samples equal      : {same}")
print(f"      peak abs diff      : {peak_diff:.3e}")
print(f"      -> {'PASS' if same else 'FAIL'}   (rule: bit-identical, no tolerance)")
verdicts.append(("R1", same))
print()

# ── R2 ──────────────────────────────────────────────────────────────────────
b8_scaled = resample(b8, len(a1))

corr = ncc(a1, b8_scaled)
cen_4 = centroid(a1, rate)
cen_8 = centroid(b8, rate)
cen_8_scaled = cen_8 * 2.0   # halving duration doubles every frequency
cen_err = pct(cen_4, cen_8_scaled)

print("R2  duration is time-scaling, not redrawing: 8s resampled to 4s vs 4s")
print(f"      NCC                : {corr:.4f}       (rule: >= 0.98)")
print(f"      centroid 4s        : {cen_4:.1f} Hz")
print(f"      centroid 8s        : {cen_8:.1f} Hz")
print(f"      8s scaled to 4s    : {cen_8_scaled:.1f} Hz")
print(f"      centroid error     : {cen_err:.2f}%        (rule: <= 2%)")
r2 = bool(corr >= 0.98 and cen_err <= 2.0)
print(f"      -> {'PASS' if r2 else 'FAIL'}")
verdicts.append(("R2", r2))
print()

# ── R3 ──────────────────────────────────────────────────────────────────────
cen_nd12 = centroid(c12, rate)
ratio = cen_nd12 / cen_4 if cen_4 else float("inf")
ratio_err = pct(2.0, ratio)

print("R3  pitch is transposition, not redrawing: +12 semitones")
print(f"      centroid 0         : {cen_4:.1f} Hz")
print(f"      centroid +12       : {cen_nd12:.1f} Hz")
print(f"      ratio              : {ratio:.4f}       (expected 2.0)")
print(f"      ratio error        : {ratio_err:.2f}%        (rule: <= 5%)")
r3 = bool(ratio_err <= 5.0)
print(f"      -> {'PASS' if r3 else 'FAIL'}")
verdicts.append(("R3", r3))
print()

print("-" * 62)
for name, ok in verdicts:
    print(f"  {name}  {'PASS' if ok else 'FAIL'}")

sys.exit(0)
