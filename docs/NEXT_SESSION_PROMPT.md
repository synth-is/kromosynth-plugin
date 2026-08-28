# Next session prompt

Copy-paste into a fresh session. Written at the end of the plugin-engine
session, 2026-08-27 (third session that day). Supersedes
`NEXT_SESSION_PROMPT.2026-08-27b.md`.

---

Continue work on the Synth.is DAW plugin. Task 1 (webview) passed. Task 2
(SFZ conformance) is answered by measurement. **Task 3, the plugin engine, is
written and works** — see §I of `STAGE0_FINDINGS.md`.

The archive is no longer dropped: `nm` on the Debug AU went from 0 to 17 sfizz
symbols. The AU renders audio under `auval` at every sample rate it tests, for
both artefacts on disk.

Use the filesystem MCP and the shell. Be critical; I want pushback, not
validation. Report failures as findings, not as things to work around.

## Context to load

- `docs/STAGE0_FINDINGS.md` — read **§I in full** first; it is this session's
  starting state. §F and §G are the SFZ format answers and are still accurate.
  §C (audition stays in the webview, the plugin plays instruments) is the scope
  decision that still matters. The TASK 1 VERDICT at the top is still accurate.
- `src/SamplerEngine.{h,cpp}` — the engine. The header comment is the
  thread-safety contract and the reason the render is chunked; both are load-
  bearing and both are read out of vendored source with line numbers.
- `src/PluginProcessor.cpp` — the OFF-thread barrier lives in `prepareToPlay`
  and is the one part of that file that is **not** disposable.
- `tools/sfz-probe/main.cpp` — still the reference for direct sfizz use, and
  now also the regression test for `kromosynth-vi` (§H3).
- `cmake/Sfizz.cmake` — two upstream workarounds with the reasoning inline. Do
  not remove either without reading §E2 and §E3.
- `kromosynth-projects/Evolutionary Sound Discovery Platform/daw-plugin/WP_SYNTHIS_PLUGIN.md`
  — **stale in known ways.** Six Task 1 findings plus everything in §E, §F, §G,
  §H and now §I are unreconciled into it. STAGE0_FINDINGS.md is the newer truth.

## Repository state

`kromosynth-plugin` has **four** unpushed commits and no remote configured at
all — "Björn pushes" currently has nowhere to push to. Worth deciding whether
that is intentional (§E4.5's mirror question is adjacent).

`kromosynth-recommend`: `M src/routes/internalRoutes.js` (§E6.1) still
uncommitted and still not deployed to the hub. The live service still returns
500s.

`kromosynth-vi`: the §E6.2 `calculateKeyRange` fix is staged but **not
committed** — that repo has unrelated WIP (§H).

## Build

```
cmake --preset sampler         -DKROMOSYNTH_SFZ_PATH=/abs/path/instrument.sfz
cmake --build --preset sampler

cmake --preset sampler-release -DKROMOSYNTH_SFZ_PATH=/abs/path/instrument.sfz
cmake --build --preset sampler-release

auval -v aumu Krm0 Sthi
tail -f ~/Library/Logs/Synth.is/stage0-<host>.log
```

`sampler-release` is new this session. `release` cannot build the sampler: it
inherits `stage0` (sampler OFF) and sets universal ON, which `cmake/Sfizz.cmake`
hard-errors on. The cold-open question was unmeasurable, not unmeasured.

Empty `KROMOSYNTH_SFZ_PATH` is legal and means "engine built, nothing loaded".
A non-empty path that does not exist is a configure-time `FATAL_ERROR`.

Test artefacts:

- velocity layers + round robins:
  `~/Developer/apps/synth.is/kromosynth-vi/data/vi-artifacts/anonymous/b234fe6d-8423-489c-b60d-275b4471b069/`
- XY morph:
  `~/Developer/apps/synth.is/kromosynth-vi/data/vi-artifacts/ymn5rj11wx4kuhk/3f2477c2-b260-46ff-a08f-28cde501a84f/`
- deliberately broken (F11 negative control):
  `tools/sfz-probe/testdata/negative-control.sfz`

## TASK — pick from these, they are not ordered by size

**Propose the design before writing it.**

### 1. The AU leg of the Reaper render comparison.

`tools/reaper-render/make_projects.py` renders fixed MIDI through the plugin via
`REAPER -renderproject`. The VST3 leg works. The AU leg does not: five
hand-written `<AU>` FXCHAIN header formats all failed to instantiate anything
(§J2), and Reaper's AU serialisation is undocumented. It needs a project Reaper
saved itself -- a two-minute GUI step, not a code problem.

Note `pluginval` already shows AU and VST3 producing identical output with a
pinned seed (§J1), so this is confirmation rather than discovery.

### 1b. VST3 with audio -- LARGELY ANSWERED, see §J1.

`auval` is AU-only. §A3.1c already found the formats diverge on UI layout;
whether they diverge on audio is unknown.

**This is automatable with nothing new.** `REAPER -renderproject file.rpp`
renders and exits. Reaper 7.79 is installed and hosts both AU and VST3
in-process, so the same MIDI can go through both formats and the two WAVs can
be diffed numerically — a number rather than an impression, per the standing
rule. Live 11 Suite and Live 12 Beta are also installed for a second opinion.

The fiddly part is producing the `.rpp`. Hand-writing one with a correctly
identified VST3 is error-prone; the cheap move is to have Björn build the
project once in the GUI (two tracks, one AU, one VST3, one shared MIDI item,
same instrument) and commit it as a fixture. After that the comparison is a
shell command and repeats forever.

### 2. Does it sound right, or only measure right?

Everything in §I is a number. Nobody has listened to it in a DAW. §C warns
about exactly this A/B trap: the webview plays through the system output at the
page's sample rate, the plugin through the host's bus, and a user who auditions
then loads will compare them. **Note §I3's finding before doing this** —
chunking is not bit-identical, so block size is very slightly audible, and any
A/B has to hold it fixed. The standing rule about A/B listening applies: prefer
a rendered diff over an impression, which task 1 above now makes cheap.

### 3. `getTailLengthSeconds()` — DONE, see §J4.

Reads `ampeg_release` and its CC depths from sfizz at load time, caches in an
atomic, returns 8.300 for both artefacts. Costs 0.6 ms. The remaining question
is whether reporting the WORST case (8.3 s) rather than the default-CC case
(0.3 s, measured) is the right trade -- it makes a host render 8 s of silence
after every bounce.

### 4. Voice count under a real chord in a real host.

Left at sfizz's default 64 (`Config.h:37`), about twelve notes at §G4's five
voices each. `setNumVoices` deliberately not called: voice stealing is audible
and the decision should follow a measurement. The Reaper render's four-note
chord did not misbehave, but `getNumActiveVoices` is not sampled during a
render, so this is still inferred. Adding a max-voices field to the render
stats would settle it with the harness that already exists.

### 5. Logic. Still the one untested host that matters.

AU-only, so it inherits §A3.1c's resize finding at its worst, and it shares
`AUHostingServiceXPC` with GarageBand, so it settles whether the storage
boundary is the DAW or the hosting process. Now also the cheapest way to get
tasks 2 and 4 at once.

## Constraints

- sfizz pinned at tag **1.2.3**, verified at configure time. `SFIZZ_USE_SNDFILE`
  stays OFF and two assertions in `cmake/Sfizz.cmake` enforce it. Do not vendor
  libsndfile.
- **liquidsfz is deferred, not rejected** (§F2).
- macOS AU + VST3 only. No Windows, Linux, iOS, CLAP.
- Universal builds with the sampler hard-error by design (§E2). `sampler-release`
  is a MEASURING build, native arch only, not a distributable one.
- **No Faust engine.** Gated on `EXPERIMENT_01_DSP_EXECUTION.md`, still at
  "protocol, pre-registered". The plugin already plays Faust-derived sounds
  today, as samples, because the render already happened; Stage 2 buys live
  parametric control, not access.
- **No engine abstraction** until there is a second engine. One concrete class.
- No auth, no quota, no local library. No UI, no webview bridge, no parameters,
  no state.
- No dependencies beyond JUCE and sfizz without asking. `pluginval` is a
  developer tool in the `sfz-probe` category — never shipped, never linked — and
  is the only way to turn §I4's barrier claim from a source reading into a
  measurement, because its higher strictness levels change sample rate and block
  size mid-session and call the plugin from unexpected threads. Available via
  `brew install --cask pluginval`.
- Do not adopt the `KROMOSYNTH_BROWSER_INSET` workaround. Recorded diagnostic,
  not a fix.
- git commits fine; do not push.

## Standing rules

- Propose significant changes before writing them.
- State hypotheses as hypotheses until log data settles them. §I4 is a source
  reading and is labelled one.
- **When a diagnostic and the thing it measures can both be broken, verify the
  diagnostic first.** Six instances now across five sessions. §I6 is the most
  recent: a once-only guard latched the render counter at zero and reported the
  exact failure it was built to detect, for a healthy build.
- **Never round-trip a measurement through its own display string** (§F9).
- **A proxy that shares no code with the thing it stands in for is not a
  measurement of it** (§I2). Whole-process wall clock for `sfz-probe` overstated
  the sfizz load cost by about 100x.
- **Build the minimal control first** (§G7.2).
- A statistic you compare for exact equality cannot be normalised (§G7.3).
- **Defensive code that never runs is not verified.** §I3: the chunk loop
  reported `max_chunks=1` until it was deliberately forced. Force it, or say it
  is untested.
- Read the actual tree you are reasoning about (§E1).
- Every async callback crossing the C++/JS or C++/host boundary needs
  `Component::SafePointer` or equivalent.
- **Non-ASCII in C++ string literals is mangled.** ASCII or
  `juce::CharPointer_UTF8`. Violated again this session, in a diagnostic string,
  and caught by reading the log rather than by remembering the rule.
- Selective git staging; never `git add -A` in a repo with unrelated WIP.

## Two `kromosynth-vi` items, both measured rather than argued

Neither is in the plugin.

- **§E6.2 / §H — FIXED**, staged but not committed. `calculateKeyRange` and its
  hand-copied duplicate `dsKeyRange` both now use
  `Math.ceil((a + b) / 2) - 1`. Verified three ways including by `sfz-probe`
  against regenerated output. **Every XY artefact currently on disk still has
  the overlap.** Regenerating them is a separate decision.
- **§G5: 10 of 21 round-robin pairs in the RR artefact are byte-identical.**
  42 WAVs, 32 distinct checksums. On those keys the feature is decorative. Not
  uniform across velocity bands, which argues against "round robins were never
  implemented" and toward something conditional in the render path.
  **Unexplained.**

## Also outstanding, cheap

- "Host still alive after removing the plugin" — last unticked row of the
  GarageBand matrix.
- `git clone --mirror` of sfizz onto own storage (§E4.5), about 22 MB. Related
  to the no-remote question above.
- Deploy the `kromosynth-recommend` fix to the hub (§E6.1).
- `.github/workflows/build.yml` runs a preset that does not exist
  (`stage0-release`). The repo has no remote, so CI has never run. Fix before
  adding one — and note there are now two more presets it could run.

## Reconciliation debt, still growing

`WP_SYNTHIS_PLUGIN.md` has absorbed none of the findings in STAGE0_FINDINGS.md.
The most consequential:

- §A2 signing moves earlier than Stage 1.
- §C audition routing — **decided**: audition stays in the webview, the plugin
  plays instruments, the quota gate fires at acquisition. The WP does not say
  so.
- §A3.1c AU and VST3 diverge on UI layout.
- §E4 the sfizz maintenance premise in §3 is false — the library repository is
  archived, and §9's fallback is in better health than the engine it chose.
- **§G2 contradicts §D2 and vindicates the generator.** sfizz multiplies
  stacked `amplitude_oncc`, exactly as `generateXYMorphSFZ` asserts.
- **§G4 is a new constraint on the XY design.** Five voices per note, always.
- **§I5 bounds what every automated result in this repo means.** `auval` exits
  0 for an instrument with zero surviving regions. It validates the AU
  contract, not that anything makes a sound.
