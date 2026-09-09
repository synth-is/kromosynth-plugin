# Next session — kromosynth-plugin, iOS track

**Written 2026-09-07, after the first session that put the plugin on a phone.**
Previous version archived as `NEXT_SESSION_PROMPT_IOS.2026-09-06b.md`. Most of
what that document predicted turned out to be wrong in the optimistic direction
on one point and the pessimistic direction on another; both corrections are
below and both are measured.

**Amended 2026-09-08** with the interface item in §3 and the sign-in finding in
§4. Neither came from a measurement; both are constraints the AUv3 session
should know about before it decides where the editor's chrome lives.

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

- **AUv3**, and the App Group container work it forces. See §3a — it starts in
  the Apple developer portal, not in the code.
- **The unified chrome — decide it WITH AUv3, not before.** The plugin shows two
  visibly different surfaces: the local shell (`ShellPage.h`) and synth.is under
  a `< Library` strip. The proposal is one navigation strip across both, so the
  instrument library becomes the home tab of the same chrome that carries Sound
  Feed, Sound Garden, Instruments and Biomes — the seam between "plugin view"
  and "website" goes away without the two-component split going away.

  **The web half is done and is INERT until this repo opts in**
  (`kromosynth-desktop` `efe5386`, 2026-09-08). `NavigationBar` takes a
  `hideNav` prop, and `App.jsx`'s seventeen byte-identical inline header
  wrappers are now one `NavigationChrome`.

  It is gated on **`?chrome=host`**, read by a new `hostDrawsChrome()` beside
  `isPluginHost()` — deliberately not the existing client marker. Being inside
  the plugin says nothing about whether the plugin has chrome, and the build
  shipping today has none: keying off `client=synthis-plugin` would have
  stripped the site surface of any way between Sound Feed, Sound Garden,
  Instruments and Biomes, leaving only `< Library`. So **the plugin opts in when
  its strip exists**, and until `showSite()` and `navigateSiteTo()` append that
  parameter nothing changes in any host. Skew resolves safely in both
  directions: an old plugin against a new deploy keeps its header, a new plugin
  against an old deploy shows two, which is ugly rather than broken.

  Appending the parameter is therefore the LAST step of the plugin half, not
  the first. Both call sites already append `?client=synthis-plugin`, so it is
  the same two edits.

  **AUv3 decides whether the strip can exist at all.** An AUv3 view is often a
  short band rather than a full screen, and the shell header already carries
  `flex-wrap` because four controls plus a title do not fit across a phone. A
  permanent strip spends vertical space that is already contested, so on iOS it
  is probably icon-only or collapsible — and that follows from the extension's
  real bounds, not from a desktop editor.

  **Two things the chrome must not evict.** §6's **Test note** is how "does iOS
  make sound" got answered at all, since the standalone app has no keyboard and
  no MIDI source unless one is attached; and **Panic** exists because 16 of 33
  measured genomes ignore their own gate while GarageBand offers none. Both
  live in the shell header a strip would replace.

  No webview is added by any of this: `site_` is already constructed with
  `withKeepPageLoadedWhenBrowserIsHidden()`, so both are resident today and the
  change is only which one is visible.
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

## 3a. AUv3: what is known before starting

> **Stage A is DONE and this section is partly superseded — see
> `AUV3_IOS_FINDINGS.md` (2026-09-08).** The App Group entitlement is live on
> device, the AUv3 instantiates in GarageBand and receives MIDI, and the
> extension bundle ID is `is.synth.plugin.pluginAUv3`. Three claims below were
> measured; two were wrong. `APP_GROUP_IDS` is multi-value, not one-value (see
> commit 58629e9). The `hostApplicationPath` log claim is false on iOS
> (finding C3). "The App Group container is where that lands" does not hold for
> the sign-in session (finding C2). The claims are left as written; the
> corrections live in the findings document.

**The first blocker is not code.** Today's Standalone build signs against
`iOS Team Provisioning Profile: *`, a wildcard App ID. **App Groups cannot be
attached to a wildcard App ID.** So before anything builds:

1. Register an explicit App ID for the container app (`is.synth.plugin`) in the
   Apple developer portal, team `ATA8MQ4L7K`.
2. Register an explicit App ID for the AUv3 extension. JUCE derives the
   extension's bundle ID from the app's; check what it actually emits rather
   than guessing, because a mismatch fails at install with a provisioning error
   that names neither cause.
3. Register an App Group, conventionally `group.is.synth.plugin`, and enable it
   on both App IDs.
4. Regenerate profiles. `-allowProvisioningUpdates` handles the fetch once the
   portal side exists; it cannot create the App Group.

Doing this first is the point. Writing the container-sharing code and then
discovering the entitlement cannot be signed wastes the code.

### What is already in place

- `FORMATS` is per-platform in `src/CMakeLists.txt` and currently `Standalone`
  on iOS. AUv3 is added there, and JUCE builds the extension INSIDE the
  Standalone target — which is why standalone-first was not a detour.
- JUCE has `APP_GROUPS_ENABLED` and `APP_GROUP_IDS` as `juce_add_plugin`
  arguments. Both are one-value args; see the list around
  `JUCEUtils.cmake:2072`. **Read the built `Info.plist` and the `.entitlements`
  to confirm they took** — `BACKGROUND_AUDIO` was silently ignored today
  because the real keyword is `BACKGROUND_AUDIO_ENABLED`, and a wrong keyword
  produces no error at all.
- **Memory does not bind.** 51.5 MB total process RSS at four Faust voices,
  ~4.3 MB per voice. `EXPERIMENT_01_4.1` §1.4's worry that an extension's
  budget would bite before throughput is answered: it does not.
- **Editor sizing is already fixed.** iOS sizes to the display's `userArea`;
  an AUv3 gets whatever the host gives it, so the 320-point lower bound matters
  more here than in the standalone app.

### The code change, and it is smaller than it looks

`InstrumentLibrary::root()` is deliberately the **only** place that resolves the
library path. On iOS it currently returns `Documents/Instruments`; for an
extension it becomes
`containerURLForSecurityApplicationGroupIdentifier("group.is.synth.plugin")`.
One function, and its comment already says so.

The harder half is the acquisition session. **WKWebView storage is
per-process** — a GarageBand AU context and a Reaper context do not share it —
so sign-in state has to be treated as host-scoped, and an instrument acquired
in the standalone app must be visible to the extension without a second
sign-in. The App Group container is where that lands.

### Two traps specific to extensions

- **An AUv3 runs in its own process, with its own log file.** The per-host log
  naming already keys on `hostApplicationPath`, so this should work — but check
  that the extension can WRITE where it thinks it can. A log that silently goes
  nowhere would remove the only diagnostic surface, which on iOS is already
  thin: acquisition outcomes are still not logged (§4).
- **`--terminate-existing` does not run the destructor.** Anything logged only
  at teardown will not appear. The `midi_in` line and `blocksSeen` were built
  around that once already.

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
- **Sign-in may not cross the AUv3 process boundary — unmeasured.** WKWebView
  storage is per-process, already recorded on the desktop track as "users sign
  in once per DAW". An AUv3 extension and its container app are separate
  processes with separate `WKWebsiteDataStore`s, so a session established in the
  standalone app probably does not reach the extension — and acquisition needs
  one, since `PLUGIN_ACQUISITION_AUTH_DECISION.md` requires an account. The
  phone has only run the standalone app so far, so this is a prediction rather
  than a finding. Settle it in the same session as the App Group work, which is
  already reasoning about the container/extension split. The unified chrome does
  not cause this, but it puts it on screen: the strip reads signed-out in one
  host and signed-in in another.

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
