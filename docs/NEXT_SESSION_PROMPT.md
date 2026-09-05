# Next session — kromosynth-plugin

Previous prompt preserved as `NEXT_SESSION_PROMPT.2026-09-05.md`.

## Read this first: two checks, each worth a day

**1. Compare the AU host process start time against the install time before
believing anything about plugin behaviour.**

```
ps -eo lstart=,comm= | grep AUHostingServiceXPC | grep -v grep
```

`scripts/build.sh install` does this automatically and prints

```
*** THE RUNNING AU HOST PREDATES THIS BUILD ***
```

GarageBand hosts AU plugins out of process and that process loads the binary
ONCE. Reopening the plugin *window* looks like a restart and is not — a new
instance runs inside the same loaded image, and `processor_constructed` appears
in the log every time, which looks exactly like proof of fresh code.

This cost four rounds of debugging on 2026-09-04, including a confident
diagnosis of a WKWebView page-caching problem **that does not exist**. It fired
again on the 2026-09-05 install, correctly.

The same class of error caught the render services: source deployed, processes
restarted *before* the deploy. Check process start against file mtime there too.

**2. Before trusting a gate, check that it CAN fail on the change in front of
you.**

New on 2026-09-05, and it cost most of a session's measuring. The forced-release
fade was changed from linear to exponential. `--clicks` came back bit-identical
on five elites and the articulation run came back identical on all 33 — and
neither result meant anything, because:

- `--clicks` measures `boundary` in the first 5 ms after an event, and forcing
  does not engage until 1.5 s after note-off.
- the articulation summary reports the phase MAXIMUM, which is dominated by
  audio from before any fade began. Its own source comment says so.

`forced stops = 1` proved the new code ran. Both gates were simply blind to it.
A gate that returns "identical" for a change you know you made is telling you
about the gate.

---

## Where things stand

Session A is half done and the half that landed is unproven by ear.

| commit | what |
|---|---|
| `b5a0bda` | previous session's prompt rotation — revert point for EVERYTHING |
| `c8550bf` | A1: `writeParameter`, the non-waking write. Unconditionally correct |
| `520b122` | native ADSR binding, adoption, exponential forced fade. THE EXPERIMENT |

`git reset --hard c8550bf` reverts the experiment and keeps A1.

Installed 2026-09-05 14:43 with `--backup`, signed, not notarised.

### The gate that still means something

```
faust-probe <wasm> <json> --plan X.plan --ref X.ref.f32     # 33/33, maxAbsDiff 0.0
```

Re-run at `520b122`: **33/33, maxAbsDiff 0.000000e+00**. The bar is zero, not a
tolerance. Plans and refs live in
`kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/probe/out/gate0`
(33 `.ref.f32` against 120 `.plan`).

**Rebuild `faust_probe` before running it.** It links `FaustEngine.cpp`
directly, so a stale binary tests the old engine — the same trap as the AU host.

```
cmake --build build/faust --target faust_probe --config Release
```

---

## THE OPEN VERDICT

**Does the native ADSR binding stay?** Not answerable by statistics; it needs
the ear. The listening test, in order:

1. `7d8517f2` — binds natively (att 1.2, dec 1, sus 0.85, rel 3). The Attack
   knob should adopt ~1.2 s on load. Turning it DOWN should speed up the onset.
   That single gesture is the whole justification for the change: a multiplying
   envelope can never shorten an attack.
2. `0fbe73fb` — binds natively AND ignores its gate. The regression candidate.
   The multiplier used to pull the level down through the 1.5 s before the
   forced fade; now nothing does.
3. `28a614a9` — does not bind. Must be unchanged.
4. Pre-registered check 3, not yet run: delete `measured.local.json` beside an
   instrument to force a re-measure, and compare `raw_peak` against the value
   already in the append-only log from before this build. It should not move,
   because `panic()` calls `init()` and that resets every slider — but that is a
   reading, and the check exists because a reading was wrong once already.

If it stays, write the verdict into this file with what was heard. If it goes,
`git reset --hard c8550bf` and record WHY next to the measurements below, which
remain true either way.

---

## What was measured on 2026-09-05

All four over the 30 installed instruments in `~/Music/Synth.is/Instruments`,
driving the wasm modules directly from Node with the double-precision math
import table.

**1. `init()` restores every slider to its declared init. 139/139 non-MIDI
sliders, no exceptions.**

Two consequences. A bound parameter must be written EVERY BLOCK, because
`init()` runs whenever a voice idles, is force-stopped, or the engine panics,
and a write-on-change design would lose its value at the next voice recycle.
And the failure A2 was written for cannot happen by the route assumed:
`measure()` calls `panic()` at the top of every pitch iteration, so no write
before it survives into it, and the measurement was always taking the genome's
evolved values. Adoption is still needed, for the other end — playback must use
the values the measurement was taken at, or the trim and the 21-point sweep
table are levelling an instrument nobody is hearing.

**2. `setParamValue` does not clamp. 139/139 stored a value one unit below their
declared minimum verbatim.**

The host's Attack knob reaches 0.0 s; `att` is declared 0.0005..4 on 15 of the
16 that carry it. The caller clamps, in both directions.

**3. Gate articulation against the ADSR set**, using the engine's own 6 dB decay
test at 440 Hz, full velocity, 48 kHz / block 512:

```
                            2 s hold    8 s hold
all four att/dec/sus/rel      2/16        4/16
everything else               3/14        6/14
```

The set the binding switches the multiplier off for is slightly BETTER
articulated than the set it leaves alone. That is what makes Option A defensible
rather than merely convenient.

**4. The verdict is MONOTONE IN HOLD LENGTH, and this is the finding with the
longest reach.** Five instruments go from respecting their gate to ignoring it
between a 2 s hold and an 8 s one, and none go back: 5/30 becomes 10/30.

`measure()` holds for 2 s. The ELITE_ARTICULATION "16 of 33" figure that the
whole forced-release design rests on was taken at a comparable window. A player
holding a chord for eight seconds meets twice as many instruments that will not
stop as any of our measurements describe.

Scripts are throwaway and were not committed. Rewriting them is ~40 lines each;
the pattern is in the commit messages for `c8550bf` and `520b122`.

---

## Next

### A-i. Replace pre-registered check 2

It cannot fail on a fade-shape change. What it needs to sample is the FADE
REGION: render a force-stopping genome, note-off, and read the envelope of the
0.2 s after `forcing` engages. The two things to assert are that it still
terminates (an exponential never reaches zero; the code clamps at 1e-4) and that
the voice still reaches idle and zeroes its DSP struct afterwards.

### A-ii. Adoption versus session restore

Adoption MOVES four host parameters on every instrument load. So a reopened
session has its restored ADSR overwritten by the genome's defaults, and
automation on those lanes fights the load. Options: a per-instrument
"already adopted" flag in the plugin state, or adopting only when the load came
from the shell rather than from `setStateInformation`. Decide by playing it.

### A-iii. The macro pool, which is thinner than it looked

Measured 2026-09-05: with `att`/`dec`/`sus`/`rel` reserved for the named knobs
and `freq`/`gain`/`gate` excluded as MIDI-driven, what remains per instrument is
usually THREE (`lvl`, `res`, `drv`), one for `faust-cppnwave` (`waveRatio`), and
NONE for `3f584745` and `52786f80`. A pool of eight is mostly empty.

The sizing from the seeds still holds — 31 distinct names across 22 seeds, 6–10
per seed, `lvl` 22, `dec` 18, `res` 18, `att` 17, `rel` 17, `sus` 16, `cut` 15,
`drv` 15 — but four of the top eight are now spoken for. Decide whether the pool
is worth four knobs rather than eight, and keep the fallback: assign by
preference order, then fill from whatever else the genome publishes, because the
other 23 names appear in 1–3 seeds each.

Renaming after construction **works in AU — verified**. `RenamableParameter` is
in `PluginProcessor.cpp` and currently renames only.

### B. The hold-length question

Measurement 4 above. Before changing anything, decide whether `measure()`'s 2 s
hold is the right window for the peak it is taken for — it may be right for
level and wrong for articulation, in which case they want separate windows
rather than one compromise.

---

## Then: Session B — what makes iOS plausible

Unchanged from the previous prompt. Three things, only the first a contract
change.

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

## Deployment state (2026-09-05)

| repo | state |
|---|---|
| kromosynth-plugin | `520b122` local, **not pushed** |
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

- **`docs/VOICE_BEHAVIOUR.md` has four stale claims now.** It says the plugin's
  envelope cannot reach the genome's own sliders — disproved twice over, first
  by reading and now by `520b122` shipping it; §5 lists voice stealing as open
  (fixed in `cd8a638`); §4 describes a 12-point 48–81 table and `tanh` as the
  ceiling (both superseded); and the forced fade it describes as linear is now
  exponential. Correct these *with* the work that disproves them.
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
  question in a host that does not cache like GarageBand. It is already being
  built and installed alongside the AU.

---

## Facts that stay load-bearing

1. **Math imports compute in DOUBLE.** faustwasm binds `_powf` to `Math.pow`; a
   C host calling `powf()` diverges by −50.5 dB on a math-heavy elite.
2. **wasmtime v48.0.1**, pinned with SHA256 in `cmake/Wasmtime.cmake`.
3. **Peak is a function of SAMPLE RATE, not block size.** 512 and 1024 agree to
   every digit at both 44100 and 48000; the two rates do not, and not by a
   uniform scaling.
4. **`init()` resets every slider to its declared init.** 139/139 measured. This
   is why bound parameters are written every block and why no write can
   contaminate `measure()`.
5. **`setParamValue` does not clamp.** 139/139 measured. The caller clamps.
6. **16 of 33 archive elites ignore their gate** — at a 2 s hold. The figure
   grows with hold length; see measurement 4.
7. **The site is attenuate-only** — `renderFaustGenome` scales down above full
   scale and never boosts, so a quiet elite sounds quiet. The plugin matches
   this; it used to normalise to 0.25 and disagreed with the audition by 31 dB
   at the pitch the preview renders.
8. **A correction may only ever attenuate.** Allowing boost gave a worst
   delivered peak of 20.8 across 12 timbre-run genomes at every semitone;
   forbidding it gives 14.9.
9. **The forced-release DECISION reads the genome's RAW output**, before
   `compGain` and before the envelope — `peakRecent` is taken from `src[i]`. So
   nothing done to the envelope changes WHEN a voice is force-stopped, only what
   is heard before the fade starts.
10. **`JUCE_FORCE_USE_LEGACY_PARAM_IDS`** — note the spelling, and it must be
    `PUBLIC`. It silently did nothing twice: once `PRIVATE` (the AU wrapper
    compiles into the *format* target, which links the shared-code target rather
    than being it), once misspelled. A misspelled feature macro compiles, links,
    installs and does nothing. Verified with `auval`, not by reading.
11. **Neutral for a multiplier is 1.0.** The reverted 2026-09-04 ADSR binding set
    `envReleaseCoef` to 0.0 calling it neutral; the release stage multiplies by
    it, so that silenced a voice on the first sample after note-off.
