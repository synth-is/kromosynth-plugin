# Next session prompt

Copy-paste into a fresh session. Written at the end of the Task 1 session,
2026-08-26.

---

Continue work on the Synth.is DAW plugin. Task 1 (webview) is complete and
passed; this session is Task 2 (sfizz) plus one decision that gates it.

Context to load:
- /Users/you/Developer/apps/synth.is/kromosynth-plugin/docs/STAGE0_FINDINGS.md
  — read the TASK 1 VERDICT at the top in full, then §C and §D. §C is the
  decision below; §D is the Task 2 material inventory.
- /Users/you/Developer/apps/synth.is/kromosynth-plugin/ — the repo. Read
  CMakeLists.txt, cmake/Sfizz.cmake (written, never exercised), CMakePresets.json.
- /Users/you/Developer/apps/synth.is/kromosynth-projects/Evolutionary Sound
  Discovery Platform/daw-plugin/WP_SYNTHIS_PLUGIN.md — §3 (the two engines) and
  §5. NOTE: six findings from Task 1 have not been reconciled into this document.
  It is stale in known ways; STAGE0_FINDINGS.md is the newer truth.
- /Users/you/Developer/apps/synth.is/kromosynth-vi/src/services/sfzGenerator.js
  and sampleMatrix.js — what our SFZ actually contains.

DECIDE FIRST, do not test: §C's audition routing.
The web app plays through its own Web Audio context, out the system default
device — not the plugin bus, not host-routed, not recordable. §5 assumes
audition behaves like a plugin. It does not. Two options:
  (a) audition stays in the webview; sfizz is a playback engine for downloaded
      artefacts only.
  (b) the webview browses, the plugin plays; sfizz becomes the primary audition
      path, load-bearing far earlier than Stage 1 assumes, with different
      latency and voice-count requirements.
"Does sfizz work" has a different answer under each. Push me to decide before
you embed anything.

PREREQUISITES, both cheap, both known:
1. The test machine's audio device was at 24 kHz. Confirm it is 44.1 or 48 kHz
   before judging any audio.
2. No artefact on disk has velocity layers > 1 or round-robins. The two SFZ
   generators are mutually exclusive: generateXYMorphSFZ forces velLayers[0]
   and roundRobin:0; generateSFZ supports velocity layers, round-robins and
   layer/split/round-robin multi-sound modes but no XY morph. Task 2 therefore
   needs TWO artefacts. The XY-morph one already exists at
   kromosynth-vi/data/vi-artifacts/ymn5rj11wx4kuhk/3f2477c2-b260-46ff-a08f-28cde501a84f/
   The other must be rendered.

TASK 2 — sfizz:
Embed sfizz as a static library and play one real VI produced by kromosynth-vi.
Report what the SFZ actually needed, what sfizz silently ignored, and whether
round-robins and velocity layers behaved.

Propose the sfizz integration BEFORE writing it. cmake/Sfizz.cmake exists and
is unexercised — review it critically rather than trusting it.

Falsifiable predictions already on record in §D. Test them, and report when they
are wrong:
- Round-robin indexing is correct (seq_position is 1-based; verified by reading,
  not by running).
- XY morph will misbehave under sfizz. generateXYMorphSFZ relies on two
  amplitude_oncc bindings on one <group> MULTIPLYING. The source comment asserts
  this as fact but was verified against sforzando/Decent Sampler, not sfizz.
  Also unverified against sfizz: <curve> at index 10-12 with sparse vNNN points,
  amplitude_curvecc, set_ccN defaults, ampeg_sustain_oncc72 with negative depth.
- WP §3's cutoff=0 caution does not apply to our output — the generator never
  emits cutoff at all.
- No loop_mode, so holding a key past the sample duration produces silence.

KEEPABLE, build properly:
- cmake/Sfizz.cmake, submodule pinning, target configuration, CI

DISPOSABLE, write fast and mark as such:
- voice handling, MIDI routing, anything that looks like an abstraction over
  "the two engines". There is still only one engine.

CONSTRAINTS:
- sfizz as a library, BSD-2-Clause, pinned to tag 1.2.3. Do NOT vendor
  libsndfile — SFIZZ_USE_SNDFILE=OFF, dr_libs default. cmake/Sfizz.cmake
  re-checks the variable after add_subdirectory because a renamed option would
  silently fall back to the LGPL path.
- macOS AU + VST3 only. No Windows, Linux, iOS, CLAP.
- No Faust engine — gated on EXPERIMENT_01_DSP_EXECUTION.md.
- No auth, no quota, no local library.
- No dependencies beyond JUCE and sfizz without asking.
- Do not adopt the KROMOSYNTH_BROWSER_INSET workaround. It is a recorded
  diagnostic, not a fix.
- git commits fine; do not push.

STANDING RULES learned in Task 1, carry them forward:
- Every async callback crossing the C++/JS or C++/host boundary needs
  Component::SafePointer or equivalent. A four-line lifetime bug produced a
  host-level "may cause the system to become unstable" modal.
- Non-ASCII in C++ string literals is mangled. ASCII or juce::CharPointer_UTF8.
- State hypotheses as hypotheses until log data settles them. Two confident
  diagnoses were wrong this session and both were caught by instrumented logs,
  not reasoning.
- When a diagnostic and the thing it measures can both be broken, verify the
  diagnostic first. Two checks written this session returned false results.

Report failures as findings, not as things to work around. Use filesystem MCP.
Be critical — I want pushback, not validation.

---

## Optional, if the plugin thread is not the priority

Higher-value alternative: reconcile STAGE0_FINDINGS.md into
WP_SYNTHIS_PLUGIN.md. Six findings change the WP and none are in it yet — most
consequentially §A2 (signing moves earlier than Stage 1), §C (audition routing),
and §A3.1c (AU and VST3 diverge on UI layout). Generating six more findings
against a document that has not absorbed the first six widens the gap.

## Also outstanding, cheap

- Logic. The one untested host that matters: AU-only, so it inherits the resize
  finding at its worst, and it shares AUHostingServiceXPC with GarageBand — so
  it settles whether the storage boundary is the DAW or the hosting process.
- Release-build cold-open time. Debug measured 1532 ms under auval; hosts time
  out during scans.
- "Host still alive after removing the plugin" — the last unticked row of the
  GarageBand matrix.
