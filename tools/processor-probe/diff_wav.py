#!/usr/bin/env python3
"""Sample-level comparison of two float32 WAVs written by processor-probe.

    diff_wav.py a.wav b.wav

Prints key=value lines and exits 0 when the two are sample-identical, 1 when
they differ, 2 when either cannot be read. Standard library only: the harness
machines have no numpy.
"""
import array
import math
import struct
import sys


def read(path):
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit("not a WAV: " + path)
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif tag == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    if fmt is None or samples is None:
        raise SystemExit("no fmt/data chunk: " + path)
    if fmt[0] not in (3, 0xFFFE) or fmt[5] != 32:
        raise SystemExit("expected 32-bit float: " + path)
    out = array.array("f")
    out.frombytes(samples[: len(samples) // 4 * 4])
    if sys.byteorder == "big":
        out.byteswap()
    return out, fmt[1], fmt[2]


def main():
    if len(sys.argv) != 3:
        print("usage: diff_wav.py a.wav b.wav", file=sys.stderr)
        return 2
    (a, ca, ra), (b, cb, rb) = read(sys.argv[1]), read(sys.argv[2])
    print("frames_a=%d\nframes_b=%d" % (len(a) // ca, len(b) // cb))
    if (ca, ra) != (cb, rb) or len(a) != len(b):
        print("shape_differs=1")
        return 1

    n = len(a)
    max_abs, sum_sq, sum_ref, first, differing = 0.0, 0.0, 0.0, -1, 0
    peak_a = peak_b = 0.0
    for i in range(n):
        x, y = a[i], b[i]
        peak_a = max(peak_a, abs(x))
        peak_b = max(peak_b, abs(y))
        d = abs(x - y)
        if d > 0.0:
            differing += 1
            if first < 0:
                first = i
            if d > max_abs:
                max_abs = d
        sum_sq += (x - y) ** 2
        sum_ref += x * x

    rms_diff = math.sqrt(sum_sq / n)
    rms_ref = math.sqrt(sum_ref / n)
    print("peak_a=%.6f\npeak_b=%.6f" % (peak_a, peak_b))
    print("differing_samples=%d" % differing)
    print("max_abs_diff=%.6e" % max_abs)
    print("rms_diff=%.6e" % rms_diff)
    print("rms_diff_db=%s" % ("-inf" if rms_diff == 0 or rms_ref == 0
                              else "%.2f" % (20 * math.log10(rms_diff / rms_ref))))
    print("first_diff_seconds=%s" % ("none" if first < 0 else "%.4f" % (first // ca / ra)))
    return 0 if differing == 0 else 1


sys.exit(main())
