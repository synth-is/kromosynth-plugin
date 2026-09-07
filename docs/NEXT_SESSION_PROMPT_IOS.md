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

## 2. The job in progress: a wasm3 backend for FaustEngine

**Two commits in, both verified, both individually revertable.**

- `a827d39` — the baseline and `scripts/null-test.sh`. No source touched.
- `fa51149` — math imports generated from one `FE_MATH_EXPRESSIONS` list.
  120/120 bit-identical.

### The rule this work runs under

**`./scripts/null-test.sh` after every step.** It links `FaustEngine.cpp`
directly and diffs a per-elite record against the committed baseline —
`PASS 120 FAIL 0`, `maxAbsDiff 0.000000e+00` throughout. It *diffs* rather than
counts, because 120/120 with a different maxAbsDiff has still changed the audio.

It covers anything that changes a sample. It does **not** cover voice stealing
under real MIDI, host threading, the editor or acquisition. A green run means
the audio path is unchanged, not that the plugin is fine.

### The approach, and why it is additive rather than a refactor

The wasm3 path goes in behind `#if KROMOSYNTH_WASM3`, leaving every existing
wasmtime line **textually unchanged**, so the desktop compiles the same tokens
it compiles today — unchanged by construction rather than by belief.

The tidy abstraction comes *after* wasm3 works, as a separate optional commit.
That is also what `cmake/Wasmtime.cmake` asks for: the abstraction gets built
"when there is a second real consumer rather than an imagined one", and until
wasm3 runs it is still imagined. Refactoring first would have been abstracting
on a guess.

### The seam is six operations

Only three exports are used — `init`, `compute`, `memory` — plus the 21-name
math import table.

| operation | wasmtime | wasm3 |
|---|---|---|
| create N voices from bytes | 1 module → N instances | N runtimes, each parsing its own |
| link math imports | `linker_define_func_unchecked` | `m3_LinkRawFunction` |
| find `init` / `compute` | `instance_export_get` | `m3_FindFunction` |
| memory base + size | `memory_data` / `_size` | `m3_GetMemory` |
| call | `func_call_unchecked` | `m3_CallV` |
| destroy | `store_delete` | `m3_FreeRuntime` |

**The first row is the one that cannot be papered over.** `FaustEngine.cpp`'s
impl comment already records it: wasmtime compiles one module and shares it
across every voice, while `m3_LoadModule` **consumes** the module and the
runtime owns the instance's memory, so each voice must parse its own copy. A
seam shaped as "module, then instances" fits wasmtime and lies about wasm3.

So shape it **per voice**: create N voices from these bytes / memory for voice i
/ call compute on voice i. That is also exactly how `host_wasm3.c` is written,
which makes it a working reference for the wasm3 half rather than a sketch.

Consequence to expect: per-voice load cost scales with voice count on wasm3
where it does not on wasmtime. `host_wasm3.c` measured ~3 ms, so it is small,
but `setVoiceCount` becomes a more expensive operation.

### What does NOT move

Voice allocation and stealing, the 2 ms fade, per-pitch levelling, the trim
table, the multiplying envelope, the forced release. All runtime-agnostic. Do
not duplicate any of it — `FaustEngine.h` records that duplicating the two
note-start routes is how pitch correction and the envelope came to disagree.

### Vendoring

wasm3 is dependency-free C and already on disk at
`faust-genome-substrate/probe/out/wasm-runtime/wasm3`. `probe/gate0/build.sh`
shows the exact source list and the four files to exclude
(`m3_api_uvwasi.c`, `m3_api_meta_wasi.c`, `m3_api_tracer.c`, `m3_api_wasi.c`).
Verified to cross-compile clean for `arm64-apple-ios15.0`, with a build
configuration that resolves **identically** to the macOS build the reference
numbers came from.

### Acceptance

Null-test wasm3 against the same 120 references **on the Mac**. Both runtimes
execute the same module bytes, and the math imports are now shared, so
bit-identical is the expected result. Anything else is a bug worth finding, not
a tolerance to widen. The phone is the last step, not the first.

## 3. After the backend

- **Device calibration.** Everything predicted is calibrated to one A15, warm.
  A short calibration render on first launch gives the device's factor, after
  which `cap = rtf1 / 4 × factor` is honest on hardware nobody has measured.
  This is also what **B3** was reaching for: no per-instrument load-time render.
- **Acquisition-time prediction.** Show the predicted voice count for *this
  device* before someone commits. Not a verdict — nothing "cannot run" — a
  number. And offer the sampled format alongside, as an alternative rather than
  a failure path.
- **The vi panel override.** `VirtualInstrumentPanel.jsx:110`,
  `const isLive = allLive && !tooManyLive`, decides for the user: a Faust
  substrate sound can only ever be rendered live. `ARTEFACT_CONTRACT` §6 already
  argued for on-demand sampling and noted the quota is keyed on the *entry*, so
  a second format is already free. What was never built is the way to ask.
- **AUv3**, and it brings the App Group container work: an extension cannot read
  the container app's Documents, so `InstrumentLibrary::root()` and the
  acquisition session both have to move. `root()` is deliberately the only place
  that resolves the path, so that is one edit.

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
