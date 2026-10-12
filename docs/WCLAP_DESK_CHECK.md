# WCLAP everywhere: desk check (synthis-aa4.10)

Desk check only, 2026-10-12. No code was written or built and nothing here was
measured. Facts about *this* repo were read from the files named. Facts about
WCLAP, wclap-bridge, Surge XT, openDAW and Apple's terms are from the task
description and general knowledge. The agent had no network access, so they
are **UNVERIFIED** and each is marked. Check them against upstream before the
numbers below are used for planning.

Probe engine, per the 2026-10-11 change: the Surge XT WCLAP on a headless Node
host (not vita-node or Vital).

## 0. What the plugin already has (this is why the estimate is small)

- `src/FaustEngine.cpp` (1988 lines) is already a wasm host inside the
  plugin: wasmtime v48.0.1 on macOS/Windows, wasm3 on iOS
  (`-DKROMOSYNTH_WASM3=ON`), one runtime/module/memory per voice, a voice
  allocator, imports linked by hand (`Link` table). `FaustEngine.h` says
  outright that the class "is the seam" and that a second backend gets
  abstracted when a second consumer exists. A WCLAP engine is that consumer.
- The wasmtime C API is already fetched, pinned and SHA256-verified
  (`cmake/Wasmtime.cmake`).
- `src/SamplerEngine.cpp` (sfizz) is the **primary iOS engine** (CMakeLists.txt
  ~l.124), because wasm3 on an A15 gives sampled Faust instruments a median of
  2 voices (`AUV3_IOS_FINDINGS.md` C49/R1). SFZ playback on iOS is shipped
  plumbing, not a plan.
- Acquisition, the Library and quota-aware delete already exist
  (`ArtifactAcquisition`, `InstrumentLibrary`, `ShellPage`).

## 1. Effort: wclap-bridge in the Synth.is plugin

Premise (UNVERIFIED): wclap-bridge is a C library that embeds a wasm runtime
(Wasmtime), instantiates a WCLAP (a CLAP plugin compiled to wasm32 with its
own WASI-ish host imports) and exposes it as a native CLAP plugin. Whether it
exposes a library API (not just a built CLAP/VST3 wrapper) that we can link
and drive from `processBlock` is the first thing to confirm.

| Step | Days | Notes |
|---|---|---|
| Read wclap-bridge, pin a version, vendor it per `VENDORING.md` (submodule + `add_subdirectory`, or a pinned prebuilt like wasmtime) | 1 | Unknown: its Wasmtime version. We pin 48.0.1 and have one `libwasmtime.a`; two versions in one binary will collide on symbols. Aligning is the main build risk. |
| `WclapEngine` class beside `FaustEngine`/`SamplerEngine`: load module, `clap_plugin` init/activate/process, audio+event buffers | 3 | Same shape as FaustEngine's per-voice host; but a CLAP plugin is polyphonic itself, so one instance, not one per voice. |
| MIDI/MPE and parameter events into CLAP event lists; param enumeration; state save/load (`clap.state` stream) | 2 | State is what makes "open in the Synth.is plugin plays the exact module the map plays": the discovered settings travel as state. |
| Engine switching in `PluginProcessor` (Faust/Sampler/Wclap), preset and artefact format change, acquisition of `.wclap` + state | 3 | Artefact format and the sentinel/outbox flow in `ArtifactAcquisition.h` need a new kind. Server side (render/serve the bundle) is outside this repo. |
| Editor: WCLAP GUI. Surge XT's GUI is native; in a WCLAP it is a web UI (`clap.webview`-style) | 3 to 5 | Largest unknown. JUCE `WebBrowserComponent` is already used for the shell page, so hosting a plugin webview is plausible but untested. Fallback: generic parameter UI, which is 1 day. |
| CPU/RTF measurement on desktop; null test against a Node-host render; gate integration | 2 | Task text says ~1.5x native CPU per WebCLAP. Gate has a null test for FaustEngine; a Wclap equivalent needs a Node-host reference render (Surge XT on the headless host) to diff against. |
| Windows build + Studio CI, AU/VST3 host smoke (Reaper, Live) | 2 | Mirrors `STAGE0_FINDINGS.md` effort. |

**Total: about 14 to 18 working days for desktop AU/VST3**, with the editor
as the swing factor (11 if the generic parameter UI is acceptable at first).
Add 3 to 5 for Windows if it slips beyond smoke.

Unknowns, in order of risk:
1. wclap-bridge's API surface and licence (UNVERIFIED; see section 2).
2. Wasmtime version/symbol clash with our pinned 48.0.1.
3. Real-time safety: Wasmtime must not allocate or trap-unwind on the audio
   thread; FaustEngine's existing discipline helps but a larger plugin
   (Surge XT) has far larger memory and warm-up. Instantiate off-thread.
4. Memory per instance (Surge XT is large; wasm linear memory up to 4 GiB
   reserved address space on 64-bit, fine on desktop, an issue on iOS).
5. Sample-rate/block-size contract and a bit-exact comparison to the Node host
   (the repo's `-50.5 dB` powf lesson: host math imports may differ).
6. Whether WCLAP's threading imports (wasm threads / shared memory) need
   host support beyond what wclap-bridge provides.

Other paths from the task, for sizing only:
- aa4.23 / aa4.25 (the plugin as native and as a WCLAP in openDAW) are separate
  beads. Hosting guests *from inside* a WCLAP build of our plugin is nested
  wasm: we cannot ship wasmtime there. That needs a host-side API to
  instantiate sibling WCLAPs (UNVERIFIED that WCLAP offers one). Flag as a
  blocker for "hosting the guests either way" in openDAW; desk-check says
  **unknown, ask the WCLAP author**.
- "Any other native DAW": wclap-bridge's own CLAP/VST3 wrapper loads the same
  WCLAP. Zero work for us beyond packaging, and the licence of that wrapper
  matters (section 2).

## 2. Licence checklist

Facts from this repo: Synth.is is AGPL-3.0-or-later; JUCE is used under its
AGPLv3 option; wasmtime Apache-2.0 WITH LLVM-exception, wasm3 MIT, sfizz
BSD-2 (`THIRD_PARTY_NOTICES.md`). Evolved `.dsp`/`.wasm` artefacts carry no
Synth.is copyright claim (`ARTEFACT_LICENCE_DECISION.md` s.1). That decision
covers *our evolved Faust* artefacts. It says nothing about third-party
engine WCLAPs, which carry their own licence. Do not extend it to them
without a new decision.

### Cross-cutting

- [ ] Surge XT is GPL-3.0 (UNVERIFIED but widely known). Any WCLAP of it is a
  GPLv3 binary. AGPL-3.0 and GPL-3.0 may be combined (GPLv3 s.13, AGPLv3
  s.13), and our plugin is AGPL. Combining is fine; each component keeps its
  own terms.
- [ ] Every other engine WCLAP: record licence per engine in a registry before
  it is offered. A proprietary or non-commercial engine cannot be served
  by this scheme at all. Decide: GPL-family/permissive only.
- [ ] wclap-bridge's licence, and the WCLAP/wclap-host-js licences: UNVERIFIED.
  Must be read. If wclap-bridge is not AGPL-compatible (e.g. a
  non-commercial or GPL-2-only licence) it cannot be linked into the
  plugin. This is a gate for section 1, step 1.
- [ ] `THIRD_PARTY_NOTICES.md` entry for each of wclap-bridge, its Wasmtime if
  bundled, and each engine WCLAP, with verified licence text path.

### Web (Synth.is web app serving WCLAPs, wclap-host-js)

- [ ] Serving a GPL binary to a browser is distribution (task description's
  position; GPLv3 treats it as conveying). Required: a prominent notice, a
  written source offer or a link to the exact corresponding source for the
  exact build served, licence text, and no extra restrictions (terms of
  service must not forbid redistribution of the WCLAP).
- [ ] The web app *itself*: whether loading a GPL module into the page makes
  the page one "combined work" is the contested question the task asked
  about. Two clean options: (a) treat the host as a separate program
  talking to the module over the WCLAP/CLAP ABI, which is an
  arm's-length boundary much like running a plugin in a DAW; this is the
  stronger argument but untested in court and not legal advice; (b) keep
  the host open (publish wclap-host-js glue and our loader under AGPL/GPL).
  Recommendation: do (b) for the loader/glue at minimum. It costs nothing
  and removes the question. **Needs a lawyer read before launch; this note
  is not that.**
- [ ] The AGPL network clause applies to modified AGPL code we run as a
  service; the existing site-wide source offer should link to the WCLAP
  sources too.
- [ ] Export "open in openDAW": we hand over a GPL bundle + state; bundle
  carries the licence and source link. openDAW's own licence/terms are
  UNVERIFIED; see the outreach bead.

### Desktop (AU/VST3/CLAP installer)

- [ ] Installer includes the licence text and source offer for each bundled
  WCLAP (the installer already carries a wasm3 source offer, commit b02c178).
- [ ] If WCLAPs are *downloaded* at run time rather than bundled, the source
  offer must still be reachable from the plugin UI (About page).
- [ ] VST3: JUCE under AGPL is fine for VST3 (the VST3 SDK is MIT here);
  nothing new from WCLAP.
- [ ] Wasmtime version clash (section 1) is a licence-neutral build issue.

### iOS (app and AUv3 appex)

- [ ] **App Store terms vs GPL/AGPL.** The widely held view (FSF, and the
  VLC precedent) is that the App Store's usage rules add restrictions
  GPLv3 s.10 forbids, so GPL-covered code *from third parties* cannot be
  shipped there unless every copyright holder permits it. UNVERIFIED for
  Apple's current terms. The repo already ships JUCE-AGPL to iOS
  (`IOS_PRIVACY_MANIFEST.md`, `AUV3_IOS_FINDINGS.md`), so this is an open
  question for the whole app, not specific to WCLAP. Check what 59r.1 and
  the 0.9.0 licence decision concluded before relying on it.
- [ ] Consequence for this task: **do not ship GPL engine WCLAPs inside the
  App Store binary.** The SFZ route below ships *rendered audio*, not the
  engine, which avoids the question for the engine's code (the audio
  output of a GPL synth is not covered by the synth's licence; confirm
  per engine, e.g. bundled factory wavetables can have separate terms).
- [ ] The Safari web app route involves no App Store, so section "Web"
  applies instead.
- [ ] Apple's rule against downloading executable code (guideline 2.5.2) is a
  second reason not to fetch WCLAPs into the iOS app at all, and iOS
  forbids JIT, so wasm in the AUv3 is already restricted to the wasm3
  interpreter (see `FaustEngine.h`).
- [ ] Sample content for SFZ: none bundled today (`ARTEFACT_LICENCE_DECISION.md`
  s.4). Rendered engine patches become user-downloaded content.

## 3. SFZ rendition via kromosynth-vi for iOS AUv3

**Yes, with one blocker, and a scope limit.**

Why yes: the iOS AUv3 already plays SFZ through sfizz as its primary engine.
`kromosynth-vi` already renders a sound to multi-pitch, multi-velocity WAVs
and packs an SFZ ZIP (`README.md`; `doc/xy-morph-sfz.md`;
`doc/faust-sampled-absolute-tuning-2026-10-05.md` shows the pipeline measured
in tune to within 0.03 cents). The plugin loads whatever `.sfz` it is given.

Blocker (small, a new backend): `kromosynth-vi` renders **genomes** through
render-socket (`renderFaust`, CPPN). It has no renderer that takes an engine
WCLAP plus state. A Surge XT render step has to be added: drive the headless
Node WCLAP host through a pitch/velocity grid, write WAVs, and hand them to
the existing SFZ generator (`sfzGenerator.js`). Estimate **3 to 5 days**, in
kromosynth-vi plus the Node host, most of which is a deterministic note-off
tail and loop-point policy. This also rides on the Node host existing, which
the probe bead covers.

Scope limit: SFZ freezes the patch. The user loses live knobs, modulation and
any non-note-triggered behaviour, and the zip grows with the pitch/velocity
grid. Round-robin and sustained or evolving patches need looping or a long
tail. This is the same trade as every sampled instrument on iOS and is what
iOS requires anyway (no JIT; C49's finding that the live wasm3 path is a
v1 blocker for chords).

## 4. Quota boundary

What the repo says: acquisition costs an extraction from the monthly quota,
and deleting an acquired file warns that it "removes a file the user spent
extraction quota" (`InstrumentLibrary.h:225`, `ShellPage.h:217`). A
re-download of an already-acquired artefact is free (`RenderOutbox.h:93`).

Stated boundary for engine patches:

| Event | Counts as an extraction? |
|---|---|
| Browsing the map, previewing, playing a module in the web app, tweaking its settings in wclap-host-js | No |
| Discovering a patch (the search itself) | No |
| **Loading a patch into a plugin instance in a DAW session** (the first time for that user and patch) | **Yes**, one extraction. This is the task's stated rule |
| Re-opening the same session, a second instance of the same patch, a re-download or reinstall of an already-acquired patch | No (matches the existing re-download rule). Key it on (user, patch id), not on instances |
| Exporting a patch file for the free native plugin or a WCLAP bundle + state for openDAW | Needs a decision. Recommend **yes** if it delivers the patch outside the quota-gated plugin, otherwise the quota is bypassed |
| SFZ rendition for iOS | Yes, one extraction for the patch; the render itself is server work, and the SFZ is a different delivery format of the same patch, so do not charge it twice |
| The free native plugin playing a patch file offline | No further charge; the quota is spent at acquisition. State clearly it is an honour-system boundary once a file leaves our servers |

Open point for the owner: does a patch in the GPL-licensed WCLAP route make
the *patch data* (settings state) free to share? The settings state is ours
(`ARTEFACT_LICENCE_DECISION.md` s.1 says no copyright is claimed on evolved
artefacts). So the quota is a service-metering rule, not a licence one. Say so
in the terms, or the AGPL/GPL "no further restrictions" clause will be read
against it.

## 5. Recommendations and next beads (not filed; need owner approval)

1. Read wclap-bridge's licence and API first (half a day). Stop if it is not
   AGPL-compatible or has no linkable API.
2. Keep the host open (web loader + glue AGPL) and take a lawyer's read on
   the GPL-module question before launch.
3. Keep GPL engine WCLAPs out of the iOS binary; deliver SFZ there.
4. Build the Node-host to SFZ step in kromosynth-vi (3 to 5 days).
5. Then the plugin `WclapEngine` (14 to 18 days).
