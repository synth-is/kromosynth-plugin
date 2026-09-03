# Voice behaviour: allocation, retrigger, levelling, clicks

**Status:** measured · **Tool:** `faust-probe <wasm> <json> [note vel voices] --clicks`

Everything here is about the seam between how the SITE plays a sound and how
the PLUGIN plays it. The site renders one note, offline, into a fresh
processor. The plugin plays live, polyphonically, reusing voices. Every
difference in sound lives on that seam, and none of it is the wasm runtime —
the null test is 33/33 bit-identical against the platform reference.

---

## 1. Measuring a click instead of arguing about one

A click is a discontinuity: one sample far from the last. `--clicks` drives the
seams — first note, reuse, pitch change, switching while sounding, stealing,
and a played melody — and reports the largest jump per phase.

It reports TWO numbers, and the first version reported one, wrongly. A
sawtooth at 0.4 peak jumps 0.8 at every cycle reset, legitimately, and a single
"largest jump" flagged that as a defect. What marks a click is a jump at the
note BOUNDARY that the rest of the note does not also produce, so the first
5 ms after each event are measured separately from everything after them.

Read it as: `boundary` far above `steady` is a click; `steady` alone is the
genome's waveform.

## 2. Voice allocation — silent voices first

`noteOff` sets `note = -1` IMMEDIATELY while the voice is still ringing. A
drone never decays at all, and anything else gets 1.5 s of grace before the
forced release. So "free" and "silent" are different questions, and the
allocator used to ask the wrong one: it took the first voice with `note < 0`,
which after one note is voice 0, still sounding.

Playing repeatedly therefore hammered one voice while the rest sat idle and
already zeroed. Measured on `01M1JD666ZTWGTCT4W2GDTVQMF` with the same melody:

| voices available | boundary jumps |
|---|---|
| 1 (reuse unavoidable) | 0.93, 0.97 |
| 8 (silent voices preferred) | 0.0002 – 0.018 |

Order is now: an idle voice, then the oldest released one, then steal the
oldest. An idle voice has been through `initVoice()`, so a note starting on it
is indistinguishable from the first note after a load — which is exactly what
the browser preview does every time.

## 3. A voice is zeroed on its way to idle

Only the forced-stop path used to call `init()`. A voice that simply went quiet
kept its filter and delay state, and the next note on it started on top:

| phase | before | after |
|---|---|---|
| first note (fresh) | 0.0006 | 0.0006 |
| same note again (reuse) | **1.660** | **0.0006** |

Zeroing at the idle transition is safe SPECIFICALLY because the voice is idle.
A genome that self-oscillates from a zeroed struct — `elite_16_deep_perc_c7-31`
does — cannot restart while nothing calls `compute()` on it, which the idle
flag already guarantees. Re-initialising a voice that is still being computed
would not be safe, and that is why `panic()` does it only alongside the flag.

## 4. Levelling matches the preview, per note

See `faust-genome-substrate/PITCH_LEVEL_2026-09-03.md` for the measurements.
In the plugin:

- **Output gain** is attenuate-only against the a4 reference, matching
  `renderFaustGenome`. A genome below full scale is left alone.
- **Per-note correction** scales every key to that reference, from the measured
  12-point table. Applied at the VOICE OUTPUT, not through the `gain`
  parameter: `gain` is the genome's own widget and an evolved program may route
  it into a nonlinearity, so scaling there could change timbre rather than only
  level.
- **Ramped, not stepped**, over 5 ms. A voice picked while still ringing would
  otherwise have its correction changed underneath sounding audio — up to a
  29 dB instantaneous step.
- **Soft ceiling** (`tanh`) on the summed output, because voices add and a
  resonance narrower than the sweep spacing can hide between table entries.
  OFF by default: `measure()` renders through the same path and would report a
  peak the ceiling invented, and the null test compares raw samples.

## 5. OPEN — stealing still clicks

When every voice is busy, the oldest is stolen and retriggered on top of itself.
Measured boundary jumps around **1.0–1.2**.

Reachable in ordinary playing: caps are per-genome (`capAt4x`, often 4–8), and
drones accumulate held-plus-ringing notes because they never decay.

The fix is a short fade before retrigger — ramp the stolen voice out over ~2 ms,
`initVoice()`, then start the pending note. It needs a deferred note start per
voice, and it costs the stolen note's tail, which is why it has not been done
speculatively. Two cheaper things that are NOT the fix and should not be
mistaken for it: raising the cap only postpones it, and a longer release fade
changes what the instrument sounds like.

## 6. Things that are not the cause, verified

- **The wasm runtime.** wasmtime and the browser's engine agree bit-identically
  over 33 elites.
- **The soft ceiling.** `tanh` is smooth; it cannot introduce a discontinuity.
- **Block size.** Peak is a function of sample rate only — 512 and 1024 agree
  exactly at both 44100 and 48000.
