# Next session prompt

Copy-paste into a fresh session. Written at the end of the Task 2 session,
2026-08-27 (second session that day). Supersedes
`NEXT_SESSION_PROMPT.2026-08-27.md`.

---

Continue work on the Synth.is DAW plugin. Task 1 (webview) passed. **Task 2 is
answered, both halves** — see §F and §G of `STAGE0_FINDINGS.md`. Every open
question about what sfizz does with our SFZ is now settled by measurement.

What is still true from the last three sessions: **nothing in `src/` references
sfizz, so the linker drops the archive.** That was a deliberate reordering, not
an oversight (§F1). This session writes the plugin engine and closes it.

Use the filesystem MCP and the shell. Be critical; I want pushback, not
validation. Report failures as findings, not as things to work around.

## Context to load

- `docs/STAGE0_FINDINGS.md` — read **§F and §G in full** first; together they
  are this session's starting state. §E is the sfizz vendoring and build story
  and is still accurate. §C (audition stays in the webview, the plugin plays
  instruments) is the scope decision that still matters. The TASK 1 VERDICT at
  the top is still accurate.
- `tools/sfz-probe/main.cpp` — **read this before writing any engine code.** It
  is a working, minimal, correct use of every sfizz call the engine needs:
  `setSampleRate`, `setSamplesPerBlock`, `loadSfzFile`, `noteOn`/`noteOff`/`cc`,
  `renderBlock`, `getNumActiveVoices`, `getNumRegions`. It will save you
  re-deriving the buffer layout and the freewheeling semantics, and its comments
  record why several things are the way they are.
- `cmake/Sfizz.cmake` — two upstream workarounds live here with the reasoning
  inline. Do not remove either without reading §E2 and §E3.
- `src/PluginProcessor.*` and `src/PluginEditor.*` — marked DISPOSABLE and mean
  it. `processBlock` clears the buffer.
- `kromosynth-projects/Evolutionary Sound Discovery Platform/daw-plugin/WP_SYNTHIS_PLUGIN.md`
  — **stale in known ways.** Six Task 1 findings and now everything in §E, §F
  and §G are unreconciled into it. STAGE0_FINDINGS.md is the newer truth.

## Repository state

`kromosynth-plugin` is **clean**. Two commits from the last session, not pushed:

```
1a1248c  Task 2 half 2: offline render settles the remaining format questions
f54bc1e  Task 2 half 1: sfz-probe load-time conformance dump
```

Björn pushes.

`kromosynth-recommend`: `M src/routes/internalRoutes.js` (§E6.1) still
uncommitted and still not deployed to the hub. The live service still returns
500s.

## Build

```
cmake --preset sampler && cmake --build --preset sampler
./build/sampler/tools/sfz-probe/sfz-probe dump   <file.sfz>
./build/sampler/tools/sfz-probe/sfz-probe render <file.sfz> [--note --vel --cc N=V --repeat --isolate ...]
./build/sampler/tools/sfz-probe/sfz-probe curve  <file.sfz> <cc> [...]
```

Always redirect stderr separately. sfizz reports parse errors and unsupported
`<control>` opcodes there and nowhere else (§F6.1).

Test artefacts:

- velocity layers + round robins:
  `~/Developer/apps/synth.is/kromosynth-vi/data/vi-artifacts/anonymous/b234fe6d-8423-489c-b60d-275b4471b069/`
- XY morph:
  `~/Developer/apps/synth.is/kromosynth-vi/data/vi-artifacts/ymn5rj11wx4kuhk/3f2477c2-b260-46ff-a08f-28cde501a84f/`

## TASK — the plugin engine

**Propose the design before writing it.** The shape agreed last session:

- A `SamplerEngine` owned by the processor. **No engine abstraction** — there is
  still only one engine, and the top-level `CMakeLists.txt` says not to grow one
  before there is a second.
- `loadSfzFile` on the message thread inside `suspendProcessing(true)`.
  Acceptable for a disposable first pass **only if documented as not the Stage 1
  design**.
- SFZ path from a CMake cache variable compiled in as a define
  (`KROMOSYNTH_SFZ_PATH`), so switching artefacts is a reconfigure and not a
  code edit. The harness takes argv instead, and that difference is deliberate
  (§F1) — do not "unify" them.
- `processBlock` translates the `MidiBuffer` and calls `renderBlock`.
- No UI, no webview bridge, no parameters, no state.

### Six things §F and §G established that the engine must honour

1. **Check `getNumRegions() > 0` after loading, not just the boolean.**
   `loadSfzFile` returns true when every region was culled for an unresolvable
   sample (§F11). Two different on-disk artefact layouts exist (§E5), so a
   broken `default_path` is a live failure mode, not a hypothetical. It would
   present as an instrument that loads fine and plays silence.
2. **Do NOT call `enableFreeWheeling()` in the plugin.** It makes the file pool
   block until background loading finishes, which is correct for the offline
   harness and exactly wrong on an audio thread.
3. **Budget five voices per note for XY instruments** (§G4). sfizz does not cull
   voices whose amplitude modulation has driven them to zero. A four-note chord
   is 20 voices before release tails overlap.
4. **`sendMessage` is documented RT** — same task as `renderBlock`. The harness
   gets away with it by being single-threaded and never rendering while
   querying. Do not call the introspection API from the message thread.
5. sfizz injects `amplitude` on cc7 and cc11 and `pan` on cc10 into **every**
   region (§F8.3). If host CCs are forwarded they will modulate. cc7 defaults to
   100/127 through a squared curve, so there is a standing gain of about 0.62.
6. `renderBlock` wants `numOutputs * 2` **planar** channel pointers, each
   `numFrames` long.

### Open questions, written down before measuring

- `setSampleRate` and `setSamplesPerBlock` are documented **OFF** — they cannot
  be called while another thread is in an RT function. JUCE calls
  `prepareToPlay` before `processBlock`, but hosts change sample rate
  mid-session. What is the correct guard?
- Voice count under a real chord in a real host, versus §G4's harness number.
- **Release-build cold-open time.** Debug measured 1532 ms under `auval` and
  hosts time out during scans. `libsfizz_internal.a` is 37.5 MB in Debug. This
  is the number most likely to block Stage 1.
- Whether AU and VST3 diverge now that there is audio. §A3.1c found they diverge
  on UI layout.
- Does the sampler actually sound right in a host, or only in the harness? §C
  warns about exactly this A/B trap: the webview plays through the system
  output at the page's sample rate, the plugin through the host's bus. A user
  who auditions then loads will compare them.

## Constraints

- sfizz pinned at tag **1.2.3**, verified at configure time. `SFIZZ_USE_SNDFILE`
  stays OFF and two assertions in `cmake/Sfizz.cmake` enforce it. Do not vendor
  libsndfile.
- **liquidsfz is deferred, not rejected** (§F2). libsndfile is not optional in
  it and it is autotools. Do not add it without revisiting §F2.
- macOS AU + VST3 only. No Windows, Linux, iOS, CLAP.
- Universal builds with the sampler hard-error by design (§E2). Do not "fix"
  that by removing the guard.
- **No Faust engine.** Gated on `EXPERIMENT_01_DSP_EXECUTION.md`, which is Gate
  0 in WP §7 and is still at "protocol, pre-registered" — the measurement has
  not been taken. WP Stage 1 says *deliberately no Faust engine yet*. Note the
  plugin already plays Faust-derived sounds today, as samples, because the
  render already happened; Stage 2 buys live parametric control, not access.
- No auth, no quota, no local library.
- No dependencies beyond JUCE and sfizz without asking.
- Do not adopt the `KROMOSYNTH_BROWSER_INSET` workaround. Recorded diagnostic,
  not a fix.
- git commits fine; do not push.

## Standing rules

- Propose significant changes before writing them.
- State hypotheses as hypotheses until log data settles them.
- **When a diagnostic and the thing it measures can both be broken, verify the
  diagnostic first.** Five instances now across four sessions. §F9 and §G7 are
  the most recent three, and two of them nearly became sfizz bug reports.
- **Never round-trip a measurement through its own display string.** §F9: a
  velocity reconstruction parsed its own `%g` output and manufactured a
  band overlap that did not exist.
- **Build the minimal control first.** §G7.2: a two-region control file settled
  in one run what three full-artefact runs had confused. It was built fourth. It
  should have been built first.
- A statistic you compare for exact equality cannot be normalised (§G7.3). Use a
  scale-invariant ratio and a tolerance instead.
- Read the actual tree you are reasoning about (§E1).
- Every async callback crossing the C++/JS or C++/host boundary needs
  `Component::SafePointer` or equivalent.
- Non-ASCII in C++ string literals is mangled. ASCII or `juce::CharPointer_UTF8`.
- Selective git staging; never `git add -A` in a repo with unrelated WIP.

## Two `kromosynth-vi` decisions, both now measured rather than argued

Neither is in the plugin. Both change every future artefact.

- **§E6.2 `calculateKeyRange` double-covers boundary keys.** Measured from
  sfizz's parsed ranges: of 49 keys in the XY instrument, 46 match 5 regions and
  **3 match 10** (MIDI 54, 66, 78). `octave` sampling is the only mode that
  triggers it and it is the default in `requestFullVI`.
- **§G5: 10 of 21 round-robin pairs in the RR artefact are byte-identical.** 42
  WAVs, 32 distinct checksums. On those keys the feature is decorative — sfizz
  alternates correctly between two copies of the same file. Not uniform across
  velocity bands (055 has identical v1 and v3 but a genuinely varying v2), which
  argues against "round robins were never implemented" and toward something
  conditional in the render path. **Unexplained.**

## Also outstanding, cheap

- **Logic.** The one untested host that matters: AU-only, so it inherits the
  §A3.1c resize finding at its worst, and it shares `AUHostingServiceXPC` with
  GarageBand, so it settles whether the storage boundary is the DAW or the
  hosting process.
- "Host still alive after removing the plugin" — last unticked row of the
  GarageBand matrix.
- `git clone --mirror` of sfizz onto own storage (§E4.5). Availability
  insurance, independent of the engine decision, about 22 MB.
- Deploy the `kromosynth-recommend` fix to the hub (§E6.1).
- `.github/workflows/build.yml` runs a preset that does not exist
  (`stage0-release`). Low priority: the repo has no remote, so CI has never run.
  Fix before adding one.

## Reconciliation debt, still growing

`WP_SYNTHIS_PLUGIN.md` has absorbed none of the findings now in
STAGE0_FINDINGS.md. The most consequential:

- §A2 signing moves earlier than Stage 1.
- §C audition routing — **decided**: audition stays in the webview, the plugin
  plays instruments, the quota gate fires at acquisition. The WP does not say
  so. No instrument trial for now. Whole-VI preview is a separate project and a
  platform question before it is a playback one.
- §A3.1c AU and VST3 diverge on UI layout.
- §E4 the sfizz maintenance premise in §3 is false — the library repository is
  archived, and §9's fallback is in better health than the engine it chose.
- **§G2 contradicts §D2 and vindicates the generator.** WP §3's ARIA-divergence
  caution does not apply to our stacked `amplitude_oncc`: sfizz multiplies,
  exactly as `generateXYMorphSFZ` asserts.
- **§G4 is a new constraint on the XY design.** Five voices per note, always.
