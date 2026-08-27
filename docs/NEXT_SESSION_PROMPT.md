# Next session prompt

Copy-paste into a fresh session. Written at the end of the Task 2 preparation
session, 2026-08-27. Supersedes `NEXT_SESSION_PROMPT.2026-08-26.md`.

---

Continue work on the Synth.is DAW plugin. Task 1 (webview) passed. Task 2's
prerequisites are now cleared and sfizz builds and links, but **no engine code
exists** — nothing in `src/` references sfizz, so the linker drops the archive.
This session writes the sampler engine and answers what Task 2 actually asked.

Use the filesystem MCP and the shell. Be critical; I want pushback, not
validation. Report failures as findings, not as things to work around.

## Context to load

- `kromosynth-plugin/docs/STAGE0_FINDINGS.md` — read **§E in full** first; it
  is this session's starting state. §C and §D are the earlier Task 2 material.
  The TASK 1 VERDICT at the top is still accurate.
- `kromosynth-plugin/cmake/Sfizz.cmake` — rewritten and exercised. Two upstream
  workarounds live here with the reasoning inline. Do not remove either without
  reading §E2 and §E3.
- `kromosynth-plugin/src/` — `PluginProcessor` and `PluginEditor` are marked
  DISPOSABLE and mean it. `processBlock` clears the buffer.
- `kromosynth-vi/src/services/sfzGenerator.js` and `sampleMatrix.js` — what our
  SFZ contains, and the defect in §E6.2.
- `kromosynth-projects/Evolutionary Sound Discovery Platform/daw-plugin/WP_SYNTHIS_PLUGIN.md`
  — **stale in known ways.** Six Task 1 findings and now eight more from §E are
  not reconciled into it. STAGE0_FINDINGS.md is the newer truth.

## Repository state — uncommitted

Nothing from the last session is committed. Björn commits and pushes.

```
kromosynth-plugin:    M .gitmodules, M cmake/Sfizz.cmake, A third_party/sfizz
                      + docs/STAGE0_FINDINGS.md (§E appended)
                      + docs/NEXT_SESSION_PROMPT*.md
kromosynth-recommend: M src/routes/internalRoutes.js   (§E6.1, not deployed to hub)
```

There is a stray untracked `third_party/sfizz/external/invoke.hpp/` left from
the mis-pinned first checkout. Safe to delete.

## Build

```
cmake --preset sampler && cmake --build --preset sampler
```

Both artefacts are on disk locally:

- velocity layers + round-robins:
  `~/Developer/apps/synth.is/kromosynth-vi/data/vi-artifacts/anonymous/b234fe6d-8423-489c-b60d-275b4471b069/`
- XY morph:
  `~/Developer/apps/synth.is/kromosynth-vi/data/vi-artifacts/ymn5rj11wx4kuhk/3f2477c2-b260-46ff-a08f-28cde501a84f/`

## TASK — the sampler engine

**Split the work in two, and do the first half first.** Most of what Task 2
asks is answerable at load time with no audio device and no DAW:

**Half 1 — load-time conformance dump.** Load each SFZ through
`sfz::Sfizz` and report: how many regions, groups and curves survived parsing;
which opcodes were dropped; whether `<curve>` blocks at index 10-12 were
accepted. Check `sfizz.hpp` for what diagnostics the public API actually
exposes rather than assuming an unknown-opcode accessor exists. Deterministic,
fast, and it answers "what did sfizz silently ignore".

**Half 2 — audible behaviour.** Round-robin cycling, velocity switching, XY
morph, voice counts. Needs MIDI and a real render.

**Propose the engine design before writing it.** Constraints: `loadSfzFile`
does disk I/O and allocates, so it must not run on the audio thread. For a
disposable first pass, `suspendProcessing(true)` around a message-thread load is
acceptable if documented as not the Stage 1 design. Take the SFZ path from a
CMake cache variable compiled in as a define, so switching artefacts is a
reconfigure and not a code edit. No UI, no webview bridge, no engine
abstraction — there is still only one engine.

## Open predictions to test, from §E8

- Stacked `amplitude_oncc` on one `<group>`: multiply or sum? This is the whole
  XY morph question. `generateXYMorphSFZ` asserts multiplication as fact but was
  verified against sforzando/Decent Sampler, not sfizz.
- `<curve>` with sparse `vNNN` points: interpolated between defined points, or
  zero between them?
- `set_cc85=64` / `set_cc86=64` honoured? **Free first test** — if not, both CCs
  default to 0 and the XY instrument opens with Center silent and West plus
  South at full. Loud, unambiguous, needs no measurement.
- Voice count under XY morph: all five groups match every note regardless of CC
  position. Does sfizz cull zero-amplitude voices? Decides between 1 and 5
  voices per note.
- `ampeg_sustain_oncc72=-80` with negative depth, and `amplitude_curvecc`.
- Round-robins: **do not test on a boundary key.** With `fifth` sampling there
  are none in the new artefact, which is why it was rendered that way. See §E6.2.

## Worth building in: run both engines

§E4 — the sfizz repository is archived, and liquidsfz (the WP §9 fallback) is
actively maintained. Do not switch engines on that basis alone. But the
load-time conformance dump is cheap to point at **both**, and two independent
implementations disagreeing about stacked `amplitude_oncc` would settle the XY
question by measurement rather than argument. Propose it before building it;
it adds a dependency and the constraint below still applies.

## Constraints

- sfizz pinned at tag **1.2.3**, verified at configure time. `SFIZZ_USE_SNDFILE`
  stays OFF and two assertions in `cmake/Sfizz.cmake` enforce it. Do not vendor
  libsndfile.
- macOS AU + VST3 only. No Windows, Linux, iOS, CLAP.
- Universal builds with the sampler hard-error by design (§E2). Do not "fix"
  that by removing the guard.
- No Faust engine — gated on `EXPERIMENT_01_DSP_EXECUTION.md`.
- No auth, no quota, no local library.
- No dependencies beyond JUCE and sfizz without asking.
- Do not adopt the `KROMOSYNTH_BROWSER_INSET` workaround. Recorded diagnostic,
  not a fix.
- git commits fine; do not push.

## Standing rules

- Propose significant changes before writing them.
- State hypotheses as hypotheses until log data settles them. Three confident
  diagnoses were wrong across the last two sessions.
- **When a diagnostic and the thing it measures can both be broken, verify the
  diagnostic first.** Last session: a scanner was validated against a known-bad
  file before its zero-result was believed, and a proposed libsndfile check
  would have false-positived had it not been checked against the source.
- Read the actual tree you are reasoning about. §E1 — half a session's
  conclusions were drawn from the wrong commit.
- Every async callback crossing the C++/JS or C++/host boundary needs
  `Component::SafePointer` or equivalent.
- Non-ASCII in C++ string literals is mangled. ASCII or `juce::CharPointer_UTF8`.
- Selective git staging; never `git add -A` in a repo with unrelated WIP.

## Also outstanding, cheap

- **Logic.** The one untested host that matters: AU-only, so it inherits the
  §A3.1c resize finding at its worst, and it shares `AUHostingServiceXPC` with
  GarageBand, so it settles whether the storage boundary is the DAW or the
  hosting process.
- Release-build cold-open time. Debug measured 1532 ms under `auval`; hosts
  time out during scans.
- "Host still alive after removing the plugin" — last unticked row of the
  GarageBand matrix.
- `git clone --mirror` of sfizz onto own storage (§E4.5). Availability
  insurance, independent of the engine decision, about 22 MB.
- Decide what to do about §E6.2, the `calculateKeyRange` overlap. It is in
  `kromosynth-vi` and changing it changes every future artefact.
- Deploy the `kromosynth-recommend` fix to the hub (§E6.1). The live service is
  still returning 500s.
- `.github/workflows/build.yml` runs a preset that does not exist
  (`stage0-release`). Low priority: the repo has no remote, so CI has never run.
  Fix before adding one.

## Reconciliation debt, growing

`WP_SYNTHIS_PLUGIN.md` has absorbed none of the fourteen findings now in
STAGE0_FINDINGS.md. The most consequential: §A2 signing moves earlier than
Stage 1, §C audition routing (**now decided** — see below), §A3.1c AU and VST3
diverge on UI layout, and §E4 the sfizz maintenance premise in §3 is false.

**§C is decided and the WP does not say so.** Sound audition stays in the
webview; the plugin plays instruments. The quota gate fires at acquisition,
when an instrument lands on the device, which removes the "on disk but not
owned" problem entirely. No instrument trial for now. Whole-VI preview is a
separate project and a platform question before it is a playback one.
