# Next session — kromosynth-plugin

Previous prompt preserved as `NEXT_SESSION_PROMPT.2026-09-04.md`.

## Read this first: the check that would have saved a day

**Compare the AU host process start time against the install time before
believing anything about plugin behaviour.**

```
ps -eo lstart=,comm= | grep AUHostingServiceXPC | grep -v grep
```

`scripts/build.sh install` now does this automatically and prints

```
*** THE RUNNING AU HOST PREDATES THIS BUILD ***
```

GarageBand hosts AU plugins out of process and that process loads the binary
ONCE. Reopening the plugin *window* looks like a restart and is not — a new
instance runs inside the same loaded image, and `processor_constructed` appears
in the log every time, which looks exactly like proof of fresh code.

This cost four rounds of debugging on 2026-09-04, including a confident
diagnosis of a WKWebView page-caching problem **that does not exist**. If a fix
appears to have no effect, check this before forming any theory.

The same class of error caught the render services: source deployed, processes
restarted *before* the deploy. Check process start against file mtime there too.

---

## Where things stand

Stage 2 works and is now playable rather than merely audible. Since the last
prompt the plugin gained levelling that matches the site's preview, a real
limiter, host parameters, an envelope, instrument removal, grouping, background
measurement with an on-disk cache, and tree links back to the web app.

### The gate to re-run after any engine change

```
faust-probe <wasm> <json> --plan X.plan --ref X.ref.f32     # 33/33, maxAbsDiff 0.0
```

Still 33/33 through every change below. The bar is zero, not a tolerance.

### The second gate, added this session

```
faust-probe <wasm> <json> [note vel voices] --clicks
```

Reports `peak`, `boundary` and `steady` per phase: first note, reuse, pitch
change, switching, stealing, and a played melody. **Read it as: boundary far
above steady is a click; steady alone is the genome's waveform.**

Its first version reported one "largest jump" and flagged a sawtooth's cycle
reset as a defect. A large jump *inside* a waveform is normal; a large jump at
the note boundary is not.

This tool found three faults in one attempt each, after two that came from
guessing — including a stuck note that would have been very hard to find by ear.
**Use it before reasoning about anything audible.**

---

## What was learned that changes how to work here

**Validate on the population being played, not the one on disk.** The gate0
elites have a median pitch spread of 3.8 dB; the TIMBRE-seeded runs reach 29 dB
and beyond. A levelling policy validated on gate0 looked fine and failed
immediately in use. `kromosynth-render/src/validate-levelling.js` evaluates
policies against shared measurements over a population and takes sound ids on
the command line — point it at the run in use.

**Five levelling designs shipped and four were wrong.** Three-point
interpolation, a short sweep window, `tanh` as a ceiling, and a table that did
not cover a keyboard. Each was plausible and each was disproved only after Björn
heard it. What broke the pattern was measuring before designing.

**A test that cannot fail proves nothing.** The genome-routing fix was
"verified" by sending an inline genome — the path that works with or without the
fix. The failing callers send a `genomeId`. `test-faust-render-by-id.js` exists
precisely for that path.

**Check the shape the consumer actually receives.** Two bugs from one habit: the
tree button and the grouping both gated on fields that do not exist in the JS
object the page gets. The sidecar has `source.soundIds` (array); the page gets
`source.soundId` (singular, taken from `[0]` by `readSidecar`).

---

## Next: Session A — finish the controls

Two prerequisites, both discovered by reading rather than by shipping. Neither
is optional and both are cheap once named.

### A1. A non-waking parameter write

`FaustEngine::setParameter` does not just write:

```cpp
for (auto& v : impl_->voices) {
    setParam (v, byteOffset, value);
    v.idle = false;
    v.quietBlocks = 0;
    v.noteDriven = false;
}
```

That is **right** for `measure()`, which drives the DSP directly and never
issues a note-on, and **wrong** for anything called per block. Written every
block it would keep all voices non-idle forever — destroying the CPU saving and
the allocator's "prefer a silent voice" rule that fixed retrigger crackle — and
clear `noteDriven`, which disables the pitch compensation *and* the envelope,
since both key off that flag.

Add a second entry point that writes without touching the flags.

### A2. Adoption before measurement

A parameter defaulting to 0, written to a slider before `measure()` runs, means
peak, trim, articulation and the 21-point pitch table are all measured from an
instrument with the plugin's defaults instead of the genome's evolved values.

The load path must read the slider inits into the parameters **first**, then
measure.

This is why the ADSR binding was reverted on 2026-09-04. That version also set
`envReleaseCoef` to 0.0 calling it "neutral" — the release stage multiplies by
it, so that silences a voice on the first sample after note-off. **Neutral for a
multiplier is 1.0.**

### A3. Then the two features, which are otherwise ready

**ADSR binding.** `att`, `dec`, `sus`, `rel` are ordinary Faust sliders on 16–18
of 30 measured instruments, reachable by `setParameter` exactly like `freq` and
`gate`. Driving them directly can SHORTEN an attack, which the multiplying
envelope can never do. All four or none: a genome exposing only `rel` would have
release driven natively while attack and decay silently did nothing.

**Macro pool.** Renaming after construction **works in AU — verified**: the knob
reads "Macro 1" then becomes DRV, DAMP or ATT per instrument. `updateHostDisplay`
with `withParameterInfoChanged` is honoured by GarageBand. `RenamableParameter`
is already in `PluginProcessor.cpp` and currently renames only.

Sizing, measured across 22 seeds in `kromosynth/faust-genome/seeds*.js`: 31
distinct slider names, 6–10 per seed. Eight cover almost everything — `lvl` (22
seeds), `dec` (18), `res` (18), `att` (17), `rel` (17), `sus` (16), `cut` (15),
`drv` (15). The other 23 appear in 1–3 seeds each, so the pool needs a fallback
from the start rather than being assumed complete.

Exclude `freq`, `gain`, `gate` — already MIDI-driven, on 30/24/29 of 30.

### Pre-registered checks for Session A

1. Null test 33/33, maxAbsDiff 0.0.
2. `--clicks`: voices still reach idle; no new boundary jump above steady.
3. **A re-measure after adoption produces the same peak as before it.** This is
   the one the reverted version would have failed.

---

## Then: Session B — what makes iOS plausible

Framed wrongly at first as "emit `pitchPeaks` into the artefact". It is three
things, and only the first is a contract change.

**B1. Per-rate peak tables.** Peaks are portable across runtimes — the null test
has wasmtime and V8 bit-identical over 33 elites — but NOT across sample rates:
up to 3.8 dB at one pitch between 48 kHz and 44.1 kHz. The server measures at
48 k, hosts commonly run 44.1 k, so one stored table is not valid.

**B2. Split the contract's trust rule.** `ARTEFACT_CONTRACT` §3.2 gates
everything on `rtfPlatform` matching. Peaks should gate on **sample rate**
alone; only `rtf` needs runtime identity. The rule conflated them because one
render produces both.

**B3. Ask whether `rtf` is needed at load at all.** Its only consumer is the
voice cap. It is genuinely platform-specific — V8/wasmtime ratios 0.48–0.99, no
usable scaling factor — so a stored value cannot predict the plugin's. But the
cap could start conservative and adapt from real CPU during playback, which
needs no synthetic render and reflects the host's actual block size and load.

If B3 works, a matching-rate artefact needs **no load-time render at all** —
1.2–4.8 s becomes instant, which on an interpreter is the difference between
viable and not. iOS prohibits JIT, so wasmtime cannot run there; wasm3 lost to
wasmtime across the archive, so load cost is the binding constraint.

---

## Deployment state (2026-09-04)

| repo | state |
|---|---|
| kromosynth-plugin | pushed |
| kromosynth-render | `f601eff` deployed both machines, verified |
| kromosynth-vi | deployed |
| kromosynth-recommend | unchanged |

**Uncommitted and not mine:** `cleanupService.js` in `kromosynth-vi` — finished
work fixing a SQLite string-comparison bug where `created_at` is ISO and
`datetime('now')` is not, so the prune condition never fired on same-day rows.
It carries a deployment precondition in its comment: must not deploy while any
artifact belonging to a takeout entry still has `takeout_entry_id` NULL. Also
six untracked diagnostic scripts and `rendered_fix.bin` (384 KB of scratch —
should not be committed).

---

## Outstanding, smaller

- **`docs/VOICE_BEHAVIOUR.md` has three stale claims.** It says the plugin's
  envelope cannot reach the genome's own sliders (disproved); §5 lists voice
  stealing as open (fixed in `cd8a638`); §4 describes a 12-point 48–81 table and
  `tanh` as the ceiling (both superseded). Correct these *with* the work that
  disproves them, not as a tidy-up.
- **Spectrogram backfill** wants re-running now that Faust rendering works —
  `spectrogramBackfill.js` renders through the path that was failing, which is
  why live-instrument cards read "Play to view".
- **The P33/P67 relabelling decision.** Feature extraction is draining ~208
  sounds. Descriptor labels are corpus-relative, so new arrivals move the
  thresholds and relabel existing sounds — including labels already stamped into
  installed artefacts. `labelsAreCorpusRelative: true` is in the sidecar and
  nothing consumes it. Decide before coverage completes.
- **Descriptor labels are not seven independent axes.** Over 889 labelled
  sounds, brightness/noisiness/rolloff/width correlate 0.81–0.92 — one spectral
  axis read four ways. Only energy (−0.27) and richness (−0.09) add information.
  The plugin shows those three. 217 distinct tuples against 5 `sound_type`
  values, of which "textural" is 76%.
- **CLAP zero-shot tags** remain the way to get *orthogonal* information —
  material and gestural claims like metallic, breathy, plucked, which spectral
  descriptors cannot express. Not a granularity fix. Circular on prompt-seeded
  runs, where a sound evolved toward a prompt will match it.
- **iOS Gate 0 measurement** still open: path B, 8 voices, slowest decile, real
  hardware.
- **VST3** is nearly free from the same target and would test the macro-rename
  question in a host that does not cache like GarageBand.

---

## Facts that stay load-bearing

1. **Math imports compute in DOUBLE.** faustwasm binds `_powf` to `Math.pow`; a
   C host calling `powf()` diverges by −50.5 dB on a math-heavy elite.
2. **wasmtime v48.0.1**, pinned with SHA256 in `cmake/Wasmtime.cmake`.
3. **Peak is a function of SAMPLE RATE, not block size.** 512 and 1024 agree to
   every digit at both 44100 and 48000; the two rates do not, and not by a
   uniform scaling.
4. **16 of 33 archive elites ignore their gate** and sound indefinitely.
5. **The site is attenuate-only** — `renderFaustGenome` scales down above full
   scale and never boosts, so a quiet elite sounds quiet. The plugin matches
   this; it used to normalise to 0.25 and disagreed with the audition by 31 dB
   at the pitch the preview renders.
6. **A correction may only ever attenuate.** Allowing boost gave a worst
   delivered peak of 20.8 across 12 timbre-run genomes at every semitone;
   forbidding it gives 14.9.
7. **`JUCE_FORCE_USE_LEGACY_PARAM_IDS`** — note the spelling, and it must be
   `PUBLIC`. It silently did nothing twice: once `PRIVATE` (the AU wrapper
   compiles into the *format* target, which links the shared-code target rather
   than being it), once misspelled. A misspelled feature macro compiles, links,
   installs and does nothing. Verified with `auval`, not by reading.
