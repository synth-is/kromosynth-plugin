# Next session — kromosynth-plugin, iOS track

**Written 2026-09-07, after the first session that put the plugin on a phone.**
Previous version archived as `NEXT_SESSION_PROMPT_IOS.2026-09-06b.md`. Most of
what that document predicted turned out to be wrong in the optimistic direction
on one point and the pessimistic direction on another; both corrections are
below and both are measured.

---

## 1. What is true now, on hardware

An **iPhone 13 Pro Max (A15, iOS 18.7)** runs the plugin as a standalone iOS
app, acquires an instrument from synth.is, loads it, and plays it polyphonically
from a hardware MIDI controller. Verified by measurement, not by impression:

- `instrument_load ok=1 regions=12 tail_s=8.300`
- `on=105 off=105 zero_length=0 max_per_block=3` — chords arriving as chords
- over a million audio blocks rendered, no dropouts

**Faust genomes do not play on iOS yet.** The build is sampler-only, and the
plugin now says so honestly instead of handing `instrument.wasm` to sfizz and
reporting an SFZ parse error.

### The two corrections that matter most

**iOS silicon is NOT slower than an M1 Pro.** Median **1.04×** over ten elites,
range 0.94–1.16. `EXPERIMENT_01_3`'s "iOS silicon is strictly slower" is false.
The consequence is large: **the Mac is a validated proxy for the phone at a
fixed runtime**, so runtime questions can be settled without a device.

**"One to two voices" was a sampling artefact, not the catalogue.** Gate 0
bundled ten elites weighted toward the slow end, correctly for the question it
asked. Measured across all 120:

| cap at ≥4× | share |
|---|---|
| 1 | 0.8% |
| 2 | 39.2% |
| 4 | 16.7% |
| **8+** | **43.3%** |

**≥2 voices 99.2% · ≥4 voices 60.0% · ≥8 voices 43.3%.** Nothing in the archive
fails to sustain a voice. That is what makes the wasm3 backend worth writing.

See `IOS_VOICE_CAP_DISTRIBUTION_2026-09-07.md`.

## 2. The wasm3 backend: DONE, and playing on the phone

**An evolved Faust genome plays live on an iPhone 13 Pro Max.** Verified on
device, not inferred:

```
instrument_load kind=faust ok=1 dsp="faustgenome_mtrbviv5_4" voices=1 gate=1
faust_measure   rtf=16.0 voices=2 raw_peak=4.221519
                c3=4.2215 a4=1.0027 a5=1.1262 spread_db=12.5
test_note       active=faust blocks_so_far=79520
```

Everything upstream worked unchanged: the engine measured the genome on the
phone and sized its own polyphony, and the three-pitch sweep ran — the levelling
that exists because single-pitch measurement once produced 38 dB errors. None of
that code knows which interpreter is underneath.

### Acceptance, and it is the strong kind

**wasm3 is BIT-IDENTICAL to wasmtime on all 120 elites.** `maxAbsDiff
0.000000e+00` throughout, and the per-elite record diffs clean against the
wasmtime baseline. Not "close enough" — the same samples. So nothing in
`ARTEFACT_CONTRACT` had to be relaxed for iOS: peak, trim, articulation and
`hasGate` carry over intact.

The desktop is provably untouched: its own null test still matches `a827d39`.

**Run `./scripts/null-test.sh` after any change to `FaustEngine.cpp`.** It
diffs a per-elite record rather than counting passes, because 120/120 with a
different maxAbsDiff has still changed the audio. It covers anything that
changes a sample; it does not cover voice stealing under real MIDI, host
threading, the editor or acquisition.

### What the commits are

- `a827d39` baseline + `scripts/null-test.sh`, no source touched
- `fa51149` math imports generated from one `FE_MATH_EXPRESSIONS` list
- `3fad670` the wasm3 backend itself
- `dde5235` `cmake/Wasm3.cmake`, and iOS now REQUIRES `KROMOSYNTH_WASM3`
- `c96c372` exactly one runtime linked, and `runtimeId()` reports which

### Three things worth carrying forward

**`runtimeId()` now returns `wasm3-0.9.1-<os>-<arch>`.** §3.2 gates an
artefact's stored `rtf` on that string, and an rtf measured under Cranelift is
meaningless under an interpreter — 5.56× out, so a cap taken from it would be
five times too generous. **This sharpens B2:** peaks *do* transfer
(bit-identical), so only `rtf` needs runtime identity. The contract gates both
on one string because one render produced both numbers.

**Link order.** wasm3 wants `m3_LinkRawFunction` AFTER `m3_LoadModule`; an
unloaded module returns "attempting to use module that is not loaded".
`probe/gate0/host_wasm3.c` had it right, and checking the working reference
settled in one look what reasoning about the API had not.

**Per-voice load cost is a non-issue.** wasm3 parses per voice where wasmtime
compiled once, so `setVoiceCount` was expected to get expensive. Measured on the
Mac: 0.60–2.55 ms per voice, **20 ms worst case for a full eight-voice reload**.
Imperceptible. Cost tracks module complexity rather than size — a 31 KB
`cppnwave` parses faster than a 21 KB `deep_pad`. wasmtime's per-voice cost
FALLS with voice count (0.54 → 0.25 ms) while wasm3's stays flat (2.67 → 2.55),
which is exactly the shape the impl comment predicted.

### The measurement deliberately NOT done at the end of a long session

**The voice cap under real load, with a pre-registered rule.** The 60%-clear-four
figure is derived from single-voice RTF in an isolated harness. What is unknown
is whether it holds with the plugin doing everything else it does — editor,
webview, acquisition, MIDI. That needs the rule written before the measurement,
not after: three of today's numbers had to be retracted (a truncated 64-of-120
histogram, a biased ten-elite sample over-read as the catalogue, a scaling model
9% wrong), and each came from moving fast on something that looked routine.

Worth adding to the same session: the load-cost check above repeated **on
device**, and against `FaustEngine` rather than the gate0 host.

## 3. What to pick up next

In rough order of value:

- **AUv3**, and the App Group container work it forces: an extension cannot read
  the container app's Documents, so `InstrumentLibrary::root()` and the
  acquisition session both have to move. `root()` is deliberately the only place
  that resolves the path, so that is one edit.
- **Device calibration.** Every predicted cap is calibrated to one A15, warm. A
  short calibration render on first launch gives a device factor, after which
  `cap = rtf1 / 4 × factor` is honest on hardware nobody has measured. This is
  also what **B3** was reaching for: no per-instrument load-time render.
- **Acquisition-time prediction.** Show the predicted voice count for *this*
  device before someone commits. Not a verdict — nothing "cannot run", 0 of 120
  elites fail to sustain a voice — a number. Offer the sampled format alongside
  as an alternative rather than a failure path.
- **The vi panel override.** `VirtualInstrumentPanel.jsx:110`,
  `const isLive = allLive && !tooManyLive`, decides for the user: a Faust
  substrate sound can only ever be rendered live. `ARTEFACT_CONTRACT` §6 already
  argued for on-demand sampling and noted the quota is keyed on the *entry*, so
  a second format is already free. What was never built is the way to ask.
- **The optional tidy-up.** Now that two runtimes work, the seam could be
  extracted properly — `cmake/Wasmtime.cmake`'s "second real consumer" exists.
  Entirely optional, and it must be its own commit with a null test either side.

## 4. Open findings not on the critical path

- **Velocity layers render identically for legacy CPPN genomes.** `legacy-2input`
  has no velocity input node; velocity only multiplies the "extra" input. Modern
  schemes (`hybrid-phase` 6-input, `phase-modulated` 4-input) carry velocity as
  a real input node. So the layer count should be a genome-aware **default**
  under the existing user control, not a fixed 3. Unmeasured: whether a modern
  genome's velocity input produces an *audible* difference.
- **Faust genomes take a different velocity route** — `setParam('gain', velocity)`
  — so layers do differ, but only in level unless `gain` feeds a nonlinearity.
- **The `OFF/*` engine-option cells still fail.** Making them build means
  deciding what the shell API returns with no engine, which is a design question.
- **Acquisition outcomes are not logged.** The failure that produced "Could not
  create the instrument library directory" left `acquisition_queued` and then
  nothing. On a phone the log is the only diagnostic surface.

## 5. Traps, each of which cost time once

- **`xcodebuild -target <name>` does not depend on ZERO_CHECK.** A changed
  CMakeLists is not picked up, and any Info.plist read back afterwards is the
  *previous* build's. Two verifications were invalid this way and produced a
  confident wrong diagnosis. Re-run `cmake -S . -B <dir>` before trusting a
  plist after a build-file edit.
- **`MAX_ELITES` truncation.** The archive pass silently measured 64 of 120 and
  printed a perfectly plausible histogram. Fixed and now warns, but the lesson
  generalises: a capped list that looks reasonable is worse than one that fails.
- **The harness leaks ~2.5 MB per voice instance.** `m3_FreeRuntime` is only
  called under `--loadonly`. Fine for a CLI, and it shapes what the archive pass
  can do in one process.
- **Simulator numbers do not count.** `EXPERIMENT_01_DSP_EXECUTION` §2. Use it
  for UI flows; it runs host-architecture code.
- **Device numbers taken back-to-back are HOT.** The archive pass reads ~18%
  below cold, matching the soak's 17.8%. Do not apply rule 4's derate twice.
- **iOS will not launch an app on a locked device.** `run-on-device.sh` detects
  this specifically; everything else about that script is headless.

## 6. Tooling that exists now

- `scripts/null-test.sh` — 120-elite bit-exactness against the baseline
- `probe/gate0-ios/` — the device harness, four passes, fully headless via
  `--pass=`, `run-on-device.sh` builds/installs/runs/pulls
- `probe/gate0-interp/` — the Faust interpreter comparison, rejected 0.49×
- `rebaseline.mjs`, `select-elites.mjs`, `select-archive.mjs`
- In the plugin itself: `blocksSeen`, the `midi_in` diagnostic line, and the
  shell's **Test note** button — which is how "does iOS make sound" got answered
  at all, since the standalone app has no keyboard and no MIDI source unless one
  is attached.

## 7. Out of scope

The desktop track: `NEXT_SESSION_PROMPT.md`.
