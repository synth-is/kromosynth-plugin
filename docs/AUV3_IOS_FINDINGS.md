# AUv3 on iOS — findings

Companion to `STAGE0_FINDINGS.md`, which is the macOS host log (GarageBand,
Reaper, Live, in-process and XPC). This one is iOS and AUv3 only. Same
convention: dated entries, newest at the top, wrong claims left standing with
the correction next to them rather than edited away.

**Environment:** iPhone 13 Pro Max (A15, iOS 18.7, UDID
`00008110-000120D9228A801E`), Xcode-beta 27.0 (27A5252f), iPhoneOS 27.0 SDK,
deployment target 16.0, JUCE 9.0.1, team ATA8MQ4L7K. Build dir
`build/ios-auv3`, Xcode generator, Release, wasm3 backend.

**Hosts tested:** GarageBand for iOS, AUM. **Not tested:** Cubasis, Loopy,
Drambo, Logic for iPad.

---

# C0. STAGE A VERDICT — the App Group entitlement is live end to end (2026-09-08)

Commit `58629e9` added `AUv3` to `FORMATS` and declared the App Group. Every
link in the chain is now measured rather than assumed:

| Link | Evidence |
|---|---|
| portal | two explicit App IDs, group assigned to both |
| profile | `iOS Team Provisioning Profile: is.synth.plugin` and `…pluginAUv3`, both carrying `application-groups` |
| signature | `codesign -d --entitlements` on BOTH bundles shows `group.is.synth.plugin` |
| integrity | `codesign --verify --deep --strict` — valid, satisfies its Designated Requirement |
| **runtime** | **iOS created the container on device at install: `Library/`, `Library/Caches`, `Library/Preferences`, writable** |

The runtime row is the one that matters. Every row above it could have passed
with the container still unreachable. Static checks prove the entitlement was
*requested and signed*; only the container on disk proves it is *effective*.

**The extension bundle ID was predicted from source before being measured.**
`juceaide/Main.cpp:324-334` appends `"." + <last dotted segment> + "AUv3"`;
`JUCEUtils.cmake:834-841` regex-derives the same string. Both said
`is.synth.plugin.pluginAUv3`. The built appex `Info.plist` agrees. Recorded
because a guessed identifier fails at install with an error that points at
provisioning rather than at the string.

**Install is non-destructive.** Same bundle ID and team, only the profile
changed, so iOS treats it as an update. Verified: the app's
`Documents` listing is 37 entries identical before and after
(`/tmp/auv3-stage-a/container-{before,after}.txt` at the time; re-derive rather
than trust that path). Worth keeping because a wiped container would have cost
re-downloads against extraction quota, not just time.

---

# C49. C48's R1 ANSWERED — two populations, and C47's sample is two weeks old (2026-09-24)

Answered from files already on disk. No phone, no build, no run. C48 is left
exactly as registered.

## R1: zero overlap, and by construction

The device log names instruments by **takeout artefact UUID** (35 distinct) and
by display name — `"Synth.is Z4DY6"`, `"P1CX2CQK 11.5s HS5BH"`. It contains the
string `elite_` **zero** times.

And `dsp="faustgenome_mtvqg98s_9"` cannot be joined either: `uniqueName()` in
`faust-renderer.js` is `faustgenome_${Date.now().toString(36)}_${_uid++}`, minted
on the render server at compile time. It is a timestamp, not a genome identity.
**So R1 as registered was not executable from the log**, which is worth knowing
before someone tries it again.

The populations differ by construction. `probe/gate0-ios/elites` is 120 `.wasm`
files exported from a probe run. The plugin's library is artefacts acquired
through the site. They overlap only by coincidence, and nothing recorded on
either side would reveal it.

**Per C48's own rule: R2 is void, and candidate 1 stands unrefuted.**
IOS_VOICE_CAP §6 named this limit in advance — "newer runs, and the
TIMBRE-seeded material in particular, are not represented."

## The prediction was wrong

C48 predicted >= 5 overlaps and a harness disagreement at ratio <= 0.5. There is
no overlap to test and R2 never ran. Same shape as C31 → C32: the bar
contradicted its author on the first reading.

**And C48's framing was too strong.** It called C47 "the outlier" and said one
of the readings "is not measuring what it is labelled as measuring". That was
wrong. The three readings are reconcilable without any of them being defective:
they measure different populations. Left standing above rather than edited.

## What C47 measured, restated

Splitting the log's `faust_measure` lines on `measure_ms`:

| | n | distinct rtf | rtf | median | voices median | ==1 |
|---|---|---|---|---|---|---|
| **fresh** (`measure_ms > 0`) | 25 | 22 | 5.2-24.4 | **8.6** | **2** | **48%** |
| cached (`measure_ms = 0`) | 124 | 24 | 4.7-24.4 | 8.2 | 1 | 60% |

The fresh row reproduces C47's published figures exactly. **C47 aggregated the
right subset**, which the entry does not say and should be credited with.

## But nothing was measured on 2026-09-24

The log is append-only and its filename is the **pull** date. `faust_measure`
lines span 09-10 to 09-24 across 22 process lifetimes. Fresh measurements by
date:

```
09-10  2     09-14  7     09-16  5
09-11  2     09-15  1     09-23  6
09-13  2
```

**09-24: 0 fresh, 18 cached.** So C47's session loaded instruments and read back
numbers minted between 09-10 and 09-23 by at least seven different binaries, in
unknown thermal states, in a log C39 already established cannot tell AUM from
GarageBand.

The three hot peaks are the clearest case. `415.699310` was measured once, on
09-10, `measure_ms=5311.7`. Every later appearance — 09-24 included — is
`measure_ms=0.0`. Same for `341.735443` (09-10) and `27.733810` (09-11). So the
session prompt's §3 is not a current observation of three hot genomes; it is two
measurements from a fortnight ago, replayed. They are still worth explaining,
and "the archive has genuinely shifted" is now the weaker reading of the two.

## The cache key does not include the plugin build

`loadCachedMeasurement` validates `schema`, `runtime`, `sampleRate`, `wasmBytes`
and `wasmHash`. `FaustEngine::runtimeId()` is the linked wasm3 version macro
plus OS and arch — **the runtime library, not this plugin's embedding of it.**

So two builds linking the same wasm3 share cached rtf figures even if one of
them changed how the module is executed: the host-function bindings at
`FaustEngine.cpp:87`, the measurement loop, the block size it measures at.

`PluginProcessor.cpp:1628` already reasons exactly this way for the CAP and
fixes that half by re-deriving from stored rtf, citing 2026-09-09. Its companion
claim — "rtf is the measurement and is worth trusting from disk" — is right for
a formula change and does not cover a change to the execution path, which is a
change to the measurement itself.

Not fixed here. The cheap remedy is a `KROMOSYNTH_MEASURE_EPOCH` constant in the
cache key, bumped deliberately when the execution path moves — which keeps the
cache doing its job and makes invalidation an explicit act rather than an
oversight.

## What this does to §1

**Not refuted, and narrowed.** C47 is a statement about the instruments in
Björn's acquired library — which is arguably the better population for a release
decision than the 120-elite bundle, since it is what a user actually holds. Half
of it being monophonic remains the right kind of reason to want a sampled path.

What is now known not to support it: the claim that this describes "the live
Faust archive". The 120-elite sweep says 60% of that archive carries four or
more voices on this phone, and both can be true at once.

Still open, unchanged: whether an acquired library generalises beyond this one,
and C32's divisor, which nothing here touches.

---

# C48. PRE-REGISTERED — C47 disagrees with two earlier measurements of the same archive on the same phone (2026-09-24)

Registered before the run. No new measurement exists at the time of writing, and
the session prompt's §1 — a month of work on a sampled Faust path — rests on the
reading this is about.

## The disagreement

| | date | instrument | result |
|---|---|---|---|
| `IOS_VOICE_CAP_DISTRIBUTION_2026-09-07` | 09-07 | 120 elites, wasm3, `probe/gate0/host_wasm3.c`, 512 frames | rtf min 5.79, **p25 12.90, median 22.96**, max 131.82; 99.2% >= 2 voices, 60% >= 4 |
| C32 | 09-10 | deadline meter in AUM, against C31's pre-registered bar | `cap=2` is **TOO TIGHT**; p50@cap 0.3 of a 5.33 ms deadline |
| C47 | 09-24 | 27 genomes in AUM, `faust_measure` | rtf **4.7-24.4, median 8.6** -> median cap 2, 48% monophonic |

Same phone, same A15, same wasm3 family, same archive nominally.

**C47's MAXIMUM is the 120-elite sweep's MEDIAN.** At most two of its 27 genomes
sit above 22.96. If the 27 were a draw from the 120 that is p = 379/2^27, about
**3e-6**, so this is not sampling noise and one of the two readings is not
measuring what it is labelled as measuring.

And C32 got there first from the other direction: a declared cap of 2 using 30%
of the deadline at two voices, under a bar registered in advance, which fired
rule 3 and contradicted its own author's prediction. C47's R1 rule is
`median capForRtf`, and `capForRtf` is the quantity C32 condemned. Pre-registering
a rule on a statistic already measured to be biased does not make the verdict
sound.

## A mechanism I had, and it is wrong

22.96 / 8.6 = **2.67**, and C19's comment cites `d3321de` at **2.3-2.9x** for
holding a real block cadence against rendering flat out. So the gap looked like
cadence: the sweep free-running, the plugin under a host.

**It is not.** `FaustEngine::measure` is free-running too, and says so in the
comment directly above `m.rtf = audioSeconds / elapsed` — 15 s of audio timed
with the audio path suspended by the barrier C21 measured at nine seconds. Both
sides of the comparison are free-running, so cadence cannot be the difference.

The ratio falling inside that band is a coincidence. Recorded because it was
persuasive and because booking it would have been C23's shape exactly: a ratio
present in a measurement is not evidence that anything applies it.

## What is left, none eliminated

1. **Different genomes.** The 27 are whatever is in Bjorn's plugin library; the
   120 are the re-measured archive bundle. IOS_VOICE_CAP §6 already names this
   limit — "newer runs, and the TIMBRE-seeded material in particular, are not
   represented" — and Stage 1.5 has since added four reverb templates
   (`dattorro-default`, `zita-rev`, `jpverb`, `greyhole`), which are not cheap.
2. **Different wasm3 embedding.** The sweep ran `host_wasm3.c` unmodified. The
   plugin has its own, binding libm through host functions
   (`FaustEngine.cpp:87`, the `U (fe_sqrt, "_sqrtf", ...)` list). Under an
   interpreter each of those is a trap out of the wasm frame, per call, and a
   genome calling `sqrt` or `sin` per sample pays it per sample. This one is
   ours.
3. **Contention and thermal.** The sweep is a dedicated app doing nothing else;
   the plugin measures inside AUM with a live graph and two WKWebViews. The
   sweep's figures are already hot (§4), so it is not a cold-versus-hot gap.

Block size is NOT a candidate and is worth stating as eliminated: the sweep used
512, `FaustEngine::measure` uses `impl_->maxBlockSize`, and AUM declares 2048
(C27). Larger blocks are cheaper per sample, so that pushes the plugin's figure
UP. It makes the gap harder to explain, not easier.

## The rule

**R1 — identity, first, because it voids R2.** Do any of C47's 27 genomes appear
among the 120 re-measured archive elites? Evidence is the device log's
`instrument_load dsp="..."` and the sidecars' `source.soundIds`, against the
`probe/gate0-ios` bundle manifest.

- **zero overlap** -> candidate 1 stands unrefuted, R2 is void, and C47 is a
  statement about this library rather than about the archive. Report it that way
  and stop.
- **one or more** -> R2.

**R2 — the ratio**, per overlapping genome:
`rtf(AUM, FaustEngine) / rtf(standalone sweep)`. Median across overlaps.

| median ratio | reading |
|---|---|
| **>= 0.8** | the two harnesses agree on the same genome. C47's sample really is that cheap, candidate 1 is the answer, and the archive has shifted. |
| **<= 0.5** | the harnesses disagree on the same genome. The gap is ours — candidate 2 or 3 — and C47's figures do not describe the archive under any sampling story. |
| 0.5-0.8 | partly both. Report as such; do not pick a half. |

**R3 — sample size.** At least **5** overlapping genomes for a verdict. Below
that, ratios but no verdict. The reference side is free, since the sweep already
measured all 120; the constraint is how much of the library is in it.

## The prediction, so it can be wrong

**R1 finds >= 5 overlaps and R2 lands <= 0.5** — the harnesses disagree. The
grounds are C32: a declared cap of 2 sitting at p50 0.3 of the deadline is an
independent instrument saying the plugin's own rtf understates this phone.

If R2 lands >= 0.8 instead, then C32 and this entry are both wrong about the
divisor and the archive has genuinely moved — which is also what C47's three
unexplained `raw_peak` readings of 415.70, 341.74 and 27.73 would be pointing
at, and the two would then be one finding rather than two.

## What this decides, and what it does not

**Decides:** whether "half the live Faust archive is monophonic on a phone" is a
statement about the archive. §1 of the session prompt treats it as the iOS
release blocker. If R2 fires the second branch, the blocker is a divisor already
condemned by C32, and the sampled Faust path keeps its own justification
(ARTEFACT_CONTRACT §6, polyphony on a phone, drones as pads) at its own priority
rather than as the thing holding a release.

**Does not decide** what the cap should be. That is C32's unresolved slope —
"the cap is too tight, and 'raise it to 8' is not yet supported" — and nothing
here touches it.

## Cost

R1 is a grep of the device log against a manifest. R2 is
`./run-on-device.sh archive`, ~90 s on device, already scripted (IOS_VOICE_CAP
§7). No build, and nothing to write first.

---

# C47. HALF THE LIVE FAUST ARCHIVE IS MONOPHONIC ON A PHONE (2026-09-24)

Measured on Björn's iPhone 13 Pro Max, in AUM, by loading live instruments and
reading `faust_measure` out of the device log. 27 distinct genomes.

## Pre-registered before the phone was touched

- **R1** median iOS `capForRtf` across >= 8 genomes. <= 2 makes sampled Faust
  instruments a v1 blocker for iOS; >= 4 means live-only ships. 3 is ambiguous
  and would be reported as such.
- **R2** the figures must come from `faust_measure` on the device, not
  `faust_declared platform="macos"` — a desktop number shown on a phone is the
  defect, not a measurement of the phone.
- **R3** >= 8 genomes, sample size stated. Below that, numbers but no verdict.

## Result

```
voices=1   13   48%
voices=2    6   22%
voices=3    2
voices=4    3
voices=5    3
                      median 2 voices    RTF 4.7 - 24.4, median 8.6
```

**R1: median 2. Sampled Faust instruments are a v1 blocker for iOS.** Half the
live archive cannot play a chord on this phone, which is not a performance
caveat but a different instrument.

**R2 passes**, and this refuted the leading hypothesis. Going in, the most
likely explanation for Björn's "the estimate doesn't reflect what one actually
gets" was that the phone was displaying a macOS RTF — `runtimeId()` embeds the
OS precisely because the same runtime on another target is a different
measurement, and iOS forbids JIT so it runs wasm3 where desktop runs wasmtime.
It is not that. The device measures for itself.

## So why the estimate still misleads

`capForRtf` is `floor(rtf / 4.2)` clamped 1..32, and `FaustEngine.h:241` says
outright that the cap is "DERIVED from rtf by capForRtf, never measured
independently". It takes a SINGLE-VOICE RTF and divides. Nobody has ever run
eight voices and checked that eight fit. The 1.05 per-voice allowance is
grounded in a 1-4% measurement, so the scaling is defensible — but an estimate
is still arithmetic on one number, and the UI presents it as a count.

## A prior that was right for the wrong reason

I predicted below 4 from an iPhone-versus-M-series CPU gap. The median is 2, so
the direction held — but these RTFs are 4.7-24.4 where EXPERIMENT_01's wasm3
archive median implied around 16.8, making the phone roughly half a Mac running
wasm3 rather than the larger gap I was reasoning toward. The number was right
and the model behind it was not, which is worth separating.

## Noticed in the same data, unexplained

Three genomes report `raw_peak` of 415.70, 341.74 and 27.73, against a guard in
FaustEngine that says "nothing in the archive exceeds 2.4; suspect the
measurement". 170x past its own threshold, drawing trims of -52.4 dB and
-50.7 dB. Either the archive has genuinely shifted or the measurement is wrong,
and the guard firing is the reason to look rather than a reason to relax.

Not the cause of the preview-versus-timeline level difference: the trim applies
to live Faust playback and a taken WAV is a server-side render that never sees
it. See the session prompt for what that turned out to be.

---

# C46. THE DRAIN WORKS, AND THE CONTAINER APP COULD NOT SEE WHAT IT DRAINED (2026-09-19)

C41 pre-registered this on 2026-09-15 and it had never been run. Measured on
the phone, iPhone 13 Pro Max, AUM.

## The pre-registered question: answered, yes

Take a render in AUM, swipe Synth.is out of the app switcher, open it cold:
`1G2WVT6P 4.0s G37QV`, 768 KB, appears in **Files → On My iPhone → Synth.is →
Renders**.

So the App Group outbox plus a cold-launch drain does deliver a render to a
place Files.app and iOS DAWs can reach, which is the whole of what C41 asked.

## The gap it exposed, which no desktop test could have

The Sounds tab in the **standalone app was empty**, and Refresh could not help,
while the file sat in Files.

`drain()` MOVES files out of the outbox into `Documents/Renders` — deliberately,
because a copy owes an answer to "what happens when the user deletes the one in
Documents" and every answer to that is bad. But `list()` and `resolve()` only
ever read the **outbox**. So the container app drained its own outbox at launch
and then looked only in the folder it had just emptied.

**Why it survived everything until the phone.** The appex is fine: AUM reads the
outbox and the file is there until a drain. macOS has no drain at all, because
`deliveredRoot()` and `outbox()` are the same directory. The bug existed only in
the one process on the one platform that had never been tested.

Fixed in `6e4ca1e`: both functions consider both roots via `searchRoots()`.

| | sees |
|---|---|
| container app | outbox + `Documents/Renders` |
| appex | outbox only — the other is a different sandbox |
| macOS | one directory; the second is skipped |

Two details that would each have been their own bug. Files are gathered from
every root and sorted **once**: sorting per directory and concatenating would
have put the list backwards after a drain, since the delivered folder holds
everything old and the outbox holds the newest. And `resolve()` now requires the
file to EXIST rather than merely be a legal child — without that, Play and Delete
worked in the appex and failed silently in the container app for the same sound.

Verified after the fix: the standalone app lists both takes newest-first, and
Play works on the drained one.

## THE REMAINING COST, stated plainly because it is not obvious

**The appex's Sounds list is "takes since you last opened the app".** Its count
shrinks when the user opens Synth.is, because the drain removes what it lists.
Björn saw exactly this: `Sounds 1` in AUM against `Sounds 2` in the app.

That is the sandbox and not a defect — the appex cannot read the container app's
`Documents` — but it is a confusing thing to ship, and it is a cost of the drain
rather than of the delivery.

The options, none taken yet:

- **Accept it.** The appex is where takes are made, so the recent ones are the
  ones that matter there.
- **Copy instead of move.** Restores the divergence question the move was chosen
  to avoid, and doubles disk use.
- **A File Provider extension**, which is the clean answer and removes the drain
  entirely: the App Group folder itself becomes visible in Files, both processes
  read one folder, and the two lists agree. Precedented — AudioShare does this.
  It is a whole extra target, so it is a decision rather than a fix.

## Also confirmed on the phone

The Library sub-tabs work on iOS, the Sounds tab lists takes in both processes,
and Play works through the `/render/<file>.wav` byte route — its first run on
iOS. There is **no drag strip**, which is correct: `RenderList` compiles out on
iOS because `performExternalDragDropOfFiles` is `jassertfalse` there.

## Still unanswered, and it is a judgement rather than a measurement

Whether the cold-launch staleness is tolerable **in use**. A render taken in AUM
does not reach Files until Synth.is is next opened, and backgrounding is not
enough. It works; whether it is annoying in the middle of real work is the
question C41 actually wanted answered, and one session of testing cannot settle
it. If it turns out to be annoying, the answer is a document picker presented
from the appex — immediate, no app launch — whose only unknown is whether a
modal presents cleanly from an AUv3 view, including in GarageBand's 736x242.

---

# C45. A NATIVE HANDLE CAN FLOAT OVER THE WEBVIEW, and we are not going to (2026-09-17)

Probe: `src/FloatingDragProbe.h`, behind `KROMOSYNTH_FLOAT_PROBE`, deleted in the
same commit as this entry. Everything below was measured by Björn in Reaper on
macOS.

## Why it was asked

Every route to dragging a render out of this plugin hits one wall, which JUCE
states in `juce_NSViewComponent.h`: a native view "will obliterate any JUCE
components that may overlap this component, but that's life". So a drag handle
cannot be drawn over the browser, and a webview is a single rectangle that
cannot have a corner cut out of it -- which forced either a full-width band
across the bottom, or a handle in its **own window**, where a separate NSWindow
composites above the webview rather than under it.

Two things rode on the answer: whether a corner panel was possible at all, and
whether a native list could **overlay** the page rather than replace it, which
would let any historical take be dragged directly.

## What it established

| | |
|---|---|
| Stays in the corner through resizes | **yes** |
| Hides when the plugin loses focus | **yes** (`isForegroundProcess()` + `isShowing()`) |
| Floats over other applications | **no** — the guard holds |
| **Drag from the floating window onto a Reaper track** | **YES** |
| Tracks a host window being moved | only by polling, and visibly less smoothly than a resize |
| Survives a host clipping the editor | **NO** |

The drag is the load-bearing positive and the reason the probe was worth
building. `getNSViewForDragEvent` takes `sourceComp->getWindowHandle()`, which
for a desktop component is its own NSView -- a different view, in a different
window, from the one the DAW knows about. Whether a drag started there reaches
the host was not answerable from the source, and it does.

## The two gaps, and why one of them decided it

**Window moves are not reported.** When the host window is dragged, no JUCE
component's bounds change; the peer moves. `getScreenBounds()` returns something
new and nothing announces it, so position can only be polled. At 12 Hz that is
noticeably less smooth than a resize, and no rate makes a guess into a
notification.

**A clipping host orphans the handle.** The editor has a minimum size, and a
host that lets its window get narrower than that CLIPS the editor rather than
resizing it -- and does not tell JUCE. The editor's bounds still describe its
full size; `ComponentPeer::getBounds()` is documented as relative to the parent
window when nested, so it cannot be intersected against; the only route to the
truth is the native view's frame through `getNativeHandle()`. So the handle
lands outside the visible window, over the host's own UI. Clamping to the
display stops it leaving the screen and cannot put it back inside the window.

## Decision: not floating

A **native list in the Library's Sounds sub-tab** gets the same direct drag with
none of this. The shell webview is already two parts -- a strip whose height the
page reports, and a body -- and `showSite` already shrinks it to the strip and
lets something else fill the body. A native list is that mechanism with a
component instead of a second webview: non-overlapping, same window, so no
polling, no focus guard, no clipping orphan, no lag, and zero height cost on
every other tab.

Preview still costs nothing. The native list sends a file name to the page via
`emitEventIfBrowserIsVisible` and the page plays `/render/<file>.wav` with
WebAudio to the system output. **Native for drag, page for playback** -- neither
doing the other's job, and no `AudioDeviceManager`.

## Recorded because I was wrong twice inside one probe

**I replaced the poll with `ComponentMovementWatcher` and called the original
reasoning an unnecessary hedge.** The poll existed because a plugin editor is
reparented into a window it does not own and cannot rely on callbacks. Björn
then reported that resizes tracked better and moves no longer tracked at all.
The hedge was covering a real case; the watcher is better at what it covers and
blind to exactly what the poll was for. Both are needed, which is what the file
ended up doing.

**I proposed per-row floating handles** for the list before thinking it through.
That is one window per visible take, each tracking independently, each needing
its own focus guard, all scroll-synced to an HTML list they cannot see -- which
would require the page to report every row's position on every frame. Björn
asked "are we thinking about multiple orange things", and the question was the
refutation.

## Untested, and left untested deliberately

A second plugin instance, and any host other than Reaper. Once the decision is
not to float, those cost effort to learn something we will not use. Noted so
that a future reader does not mistake absence for a pass.

---

# C44. RE-RENDERING RETURNS THE SOUND; DURATION AND PITCH DO NOT (2026-09-16)

Measured because the pricing rule "re-rendering a form you own is free,
forever" is a promise, and it was worth knowing whether the promise is true
before making it. Probe: `tools/render-determinism/check.py`.

## Pre-registered, before the first render, untouched since

| | rule | threshold |
|---|---|---|
| **R1** | same genome, same params, twice | bit-identical, **no tolerance** |
| **R2** | duration is time-scaling, not redrawing | NCC ≥ 0.98 **and** centroid within 2% |
| **R3** | pitch is transposition, not redrawing | centroid ratio tracks 2.0× within 5% |

R1 gets no tolerance because a deterministic DSP graph has no legitimate source
of variation. R2 and R3 get one because resampling and transposition both alter
samples for reasons that are not redrawing.

## R1 PASS, and it is the load-bearing one

Bit-identical. Peak absolute difference **0.000e+00**, through the WebGPU CPPN
backend, which was the plausible source of drift and is not one.

So: the sound you own does come back, the server can discard expired artefacts
and re-derive from the genome, and free re-render at the same parameters is a
promise that holds.

## R2 and R3 FAIL, structurally

```
R2   NCC 0.3493                    (rule >= 0.98)
     centroid 4s   1094.1 Hz
     centroid 8s   1466.4 Hz       (a pure stretch predicts 547)

R3   centroid 0    1094.1 Hz
     centroid +12  1195.1 Hz
     ratio 1.0923                  (resampling a fixed waveform predicts 2.0)
```

This reads as correct behaviour rather than a defect. **In a hybrid CPPN+DSP
substrate, duration cannot be a time-scaling.** The CPPN wavetable is drawn over
normalised time and scales; the as-NEAT graph's filters, delays and reverbs have
absolute time constants and do not. An 8-second render is a genuinely different
sound from a stretched 4-second one, and `noteDelta` changes what the graph does
rather than resampling its output.

**What it changes is what can be said, not what can be built.** "Re-render the
sound you own, free, at the same parameters" is measured and true. "The same
sound at any duration or pitch" is measured and false: you get a family of
related sounds.

The pricing decision -- charge per genome, re-rendering free at any duration and
pitch -- stands, and now stands as a deliberate choice made against this
evidence rather than as a claim that the renders are the same sound. Its
engineering consequence: **entitlement must key on the genome, not on the
artefact or the takeout entry**, because every new duration is a new artefact.

## THE FIRST RUN WAS VOID

It reported R2 and R3 failing too, and neither reading was a result. Two things
were wrong and both were the measurement rather than the system:

1. **The instrument.** The centroid was amplitude-weighted across the whole
   spectrum including DC. The fixture's spectral argmax was bin 0, so the
   measure reported where the offset sat, not brightness. Hence +12 semitones
   appearing to move the centroid 0.5%: a DC block does not transpose.
2. **The fixture.** `reachability-legacy-rhythm/best-genome.json` renders at
   **mean DC +0.7425 against peak 0.847, 99.3% of spectral energy below 20 Hz**.
   A DC block with a little audio riding on it, and unable to answer a question
   about timbre under any measure.

Point 2 is a finding on its own. That is the **best genome** of that run, and it
corroborates the DC-blindness of `quality_musicality.py` -- no offset term, so
all four weighted measures reward DC. A scorer blind to DC selected a sound that
is mostly DC.

Fixed by excluding bins below 20 Hz and choosing a fixture with real content
(`reachability-legacy-rhythm-final`: DC +0.0112 at peak 0.119, centroid
1094 Hz). Fixing an instrument is legitimate; retuning a threshold to rescue a
failed rule is not. The thresholds are as first written and the void readings
sit in the probe's header beside the corrected ones.

## Scope, stated rather than implied

One fixture, `legacy-2input`, the oldest scheme. **R1 is the result worth
generalising.** R2 and R3 want repeating on a phase-modulated 4-input genome and
on a Faust-substrate one -- FDN13, the sound actually taken through the plugin,
is Faust and was not tested here.

Two CLI traps found on the way, both in the probe's header: the genome must be
wrapped as `{"genome": {…}}` or `render-audio` dies destructuring
`genome.asNEATPatch`, and `--play-on-default-audio-device=false` does not
suppress playback because meow reads the string `"false"` as truthy.

---

# C43. WEBKIT DRAGS A LINK, NEVER A FILE (2026-09-16)

The renders list was going to be HTML -- a section beside Instruments in the
shell page, same palette, draggable rows. That is where a user looks for it and
it is what was asked for. It does not work, and the reason is precise enough to
be worth keeping.

## The question

Will WebKit put a usable local-file reference on the SYSTEM drag pasteboard for
a page whose origin is not `file://`?

If yes, the renders list is plain HTML and nothing native is needed. If no, the
drag has to be started by a native component and therefore has to live in a
rectangle no webview occupies.

## The probe

`tools/drag-spike/index.html`, served over `http://127.0.0.1:8765` and dragged
by hand. **SERVED, not opened as a file** -- a `file://` origin dragging a file
URL is the one case WebKit has never had reason to block, so opening it
directly would have passed and meant nothing. The page says so in red if its
origin is `file:`.

Four rows, the fourth a control:

| # | transfer | dropped on Finder | dropped on Ableton |
|---|---|---|---|
| 1 | `text/uri-list` = `file://…FDN13.wav` | `.fileloc` created | nothing |
| 2 | `DownloadURL` (Chromium convention) | bounced back | nothing |
| 3 | plain `<a href="file://…">` | **drag would not start** | nothing |
| 4 | **control**: `text/uri-list` = `https://synth.is/` | `.webloc` created | nothing |

## The answer, and the mechanism

**WebKit writes `public.url` -- a LINK -- and never `public.file-url`, a FILE.**
Different UTIs, and everything above follows from it. Finder's response to a
dropped link that happens to point at a file is a bookmark document, not a copy
of the audio.

The URL itself was intact, which is what makes this policy rather than a bug in
the probe:

```
$ plutil -p ~/Downloads/Users:.fileloc
{ "URL" => "file:///Users/bjornpjo/Music/Synth.is/Renders/Synth.is%20FDN13.wav" }
```

Row 3 is the policy refusing out loud: WebKit will not even BEGIN a drag
session for a `file://` anchor from a remote origin. Row 4 proves the gesture
and the page were fine throughout -- without it, rows 1 to 3 would have been
uninterpretable.

A custom scheme like `juce://juce.backend` is at least as restricted as
`http://localhost`, so this settles the shell page too without a second test.

## What it forces

Native, and in its own rectangle. Two JUCE facts, both from source rather than
memory:

- `juce_Windowing_mac.mm:192` needs `[[view window] currentEvent]` -- an
  NSEvent the window is dispatching RIGHT NOW. Only a real `mouseDrag` on a
  native component provides one. The `window.__JUCE__` bridge is asynchronous,
  so a native function called from `dragstart` arrives after the event is gone.
  **Loosening the site/shell trust boundary would therefore not have helped**,
  which is worth knowing before anyone proposes it: the boundary and this
  problem are independent, and only one of them was ever in the way.
- `juce_NSViewComponent.h`: a native view "will obliterate any JUCE components
  that may overlap this component, but that's life". So the strip cannot float
  over the browser; `resized()` trims its height off `bounds` BEFORE either
  webview is placed.

And one platform fact that settles the `#if`: `juce_Windowing_ios.mm:527` is
`jassertfalse; return false;` with the comment "no such thing on iOS!". Desktop
only is not a preference here.

## THE SECOND FINDING, WHICH IS NOT SETTLED

**Ableton rejected all four rows, INCLUDING the control.** So there is no
evidence that Live's arrange view accepts any inter-application drop of a URL,
and -- more to the point -- no evidence either way about whether it accepts
JUCE's native file drag.

That assumption was made before the probe and the probe did not test it.
`performExternalDragDropOfFiles` writes an `NSURL` as its pasteboard writer,
which yields `public.file-url` and is a different thing from what WebKit wrote;
plugins that drag samples into Live do work. But "different mechanism, and it
works for other people" is not a measurement of this one.

**So the gutter is built but not proven.** The observation that decides it is
narrow and should be made before anything else is built on top:

> Drag a chip onto a Finder window. Does the actual WAV get copied, or does a
> `.fileloc` appear?

A copied WAV means `public.file-url` reached the pasteboard and the mechanism
works. A `.fileloc` means the native path is no better than the HTML one and
the desktop answer is Reveal-in-Finder. Only then is Ableton worth trying.

---

# C42. THE RENDER VERB, and a branch that was there all along (2026-09-15)

C41's option 3, built. Also one correction to C41 and one thing that did not
need building at all.

## The web branch existed, and gave two different wrong answers

C41 says `Download WAV` has "no `isPluginHost()` branch at all". There is one,
in `downloadTakeoutEntry`, and it has been there since the acquire path shipped.
What it does depends on which surface the button is on:

| Surface | format sent | what happened in the plugin |
|---|---|---|
| feed card (`SoundRendersList`) | `'wav'`, computed explicitly | refused, with a readable message |
| Takeout page, instrument detail | `null` -> `'sfz'` | passed the gate, handed to the ZIP unpacker |

`InstrumentDownloadButton`'s single branch called `onDownload(entryId)` with no
format. Both handlers default `targetFormat` to null, and
`downloadInstrumentEntry` turns null into `'sfz'`. So a single WAV reached the
install gate wearing a sampler bundle's label, passed it, and produced "Archive
is empty or not a ZIP" -- the exact message the comment above that gate was
written to prevent.

Invisible in a browser, which is why it survived: the bytes come from
`/vi/download/{jobId}` whatever the POST body claims. **The format string is
only ever read as a DECISION inside the plugin.** Everywhere else it is
bookkeeping.

Worth keeping because of the shape rather than the bug. Looking from the plugin
end, "the branch is missing" and "the branch is there and one caller mislabels
its input" present identically: nothing useful happens. The first sends you to
write a branch; the second sends you to find the caller.

## The server needed nothing, and that was checked rather than hoped

- `POST /api/takeout/{id}/download` mints a ticket from `entry.artifactId`
  alone; the format never reaches `mintExtractionTicket`
  (`takeoutRoutes.js:549-611`).
- `GET /vi/plugin-file/:artifactId/:fileName` already sets
  `Content-Type: audio/wav` for a `.wav` and streams it with `Content-Length`
  (`vi.js:70`, the body it shares with `/vi/file`).
- A single-sample artefact's file is `VI_<6 chars>_<low>-<high>.wav`
  (`generateArtifactName`), which passes the plugin's `isSafeComponent`
  character rule unchanged.

So the whole of this is web plus plugin, and it works on macOS as well as iOS.
Only the `Documents` drain is iOS-specific.

## A verb, not a query parameter

`synthis://render`, beside `acquire` and `auth`. The argument is version skew in
one direction only: `parseSentinel` already answers nullopt for an unknown verb
and `pageAboutToLoad` blocks it, so **an old plugin against a new deploy does
nothing**. An unrecognised query PARAMETER is ignored instead, and that plugin
would unpack the WAV as a ZIP -- reintroducing the defect above through the
mechanism chosen to avoid it.

Same reasoning `hostDrawsChrome` already follows for `?chrome=host`.

## Move, not copy

`<container>/Renders` is an OUTBOX both processes can write. The container app
moves each file into `Documents/Renders` at launch.

A copy owes an answer to "what happens when the user deletes or moves the one in
Documents", and every answer is bad: re-copy it next launch (surprising),
remember the deletion (a database), or diverge silently (worst). A move leaves
one file, owned by the user, and no question.

**What it costs, recorded now rather than discovered later:** a sound rendered in
AUM is not in Files until Synth.is is next opened COLD -- a backgrounded app
does not drain -- and the appex can never list what it delivered, because it no
longer has it.

Both are accepted deliberately, to find out whether that staleness is tolerable.
**The iOS-conventional answer is a document picker in export mode from the
appex**, which is what audio apps do for bounces: no shared folder, no sync, no
staleness. It was not built first because it carries an unknown -- whether a
modal presents cleanly from an appex hosted by AUM, and more so in GarageBand's
736x242 -- and that unknown needs the phone. The outbox has none, and the
sentinel, the download and the naming are all reused by a picker unchanged.

## What is verified, and what is not

**Verified**, `acquire-probe selftest`, 105/105 including seventeen new checks:
both verb spellings (custom scheme and the `__synthis_plugin` https fallback),
the auth verb not being mistaken for an artefact request, `fileNameFor`'s five
branches, both paths refusing each other's work before touching disk, and a
failed render fetch leaving no staging directory behind. `build/desk-verify`
compiles clean; the web app builds and adds no eslint findings.

**NOT verified: the drain.** It is gated on `wrapperType_Standalone` and needs a
device -- and the staleness above is the thing to judge when it runs, not
whether it runs.

## Also

`InstrumentLibrary`'s resolver now answers with the CONTAINER, and `root()`
appends `Instruments`. Same three paths as before. The point is that the outbox
resolves through the same branch: an appex writing renders into its private
Documents while reading instruments from the App Group would be two halves of
the plugin disagreeing about which sandbox they are in, and the symptom would be
a render that silently never arrives.

---

# C41. FILES.APP HIDES AN EMPTY FOLDER, and the library was never lost (2026-09-15)

Four findings, three of which correct something asserted earlier.

## Files.app visibility needs BOTH keys, and something to show

`UIFileSharingEnabled` alone is the older iTunes switch.
`LSSupportsOpeningDocumentsInPlace` grants the local file provider access to the
same folder, and both are needed before an app's `Documents` appears in
Files.app. `juce_add_plugin` has no option for the second — `PLIST_TO_MERGE`
(`11b791c`).

**And iOS hides the folder entirely while `Documents` is empty.** Verified: with
both keys shipped, Björn saw nothing. A single file pushed with
`devicectl device copy to … --destination Documents/README.txt` made
*On My iPhone → Synth.is* appear immediately.

That is worth keeping, because "folder absent" reads as "entitlement wrong" and
is not. C8 asserted Files.app visibility twice from the plist key alone; it was
never visible, because `Documents` has been empty since the library moved to the
App Group.

I got the second key wrong twice on the way — first as
`DOCUMENT_BROWSER_ENABLED` (which emits `UISupportsDocumentBrowser`, a different
feature), then reverted on the reading that it governs opening OTHER apps'
documents. That is its nominal description; its practical effect is the
visibility above. Both corrections are in the CMakeLists comment.

## devicectl cannot be trusted about the App Group container

```
library=appGroup
root=/private/var/mobile/Containers/Shared/AppGroup/6DB47DBD-…/Instruments
exists=yes entries=15
```

The library was exactly where `InstrumentLibrary.cpp:55` says, intact, while
`devicectl device info files --domain-type appGroupDataContainer
--domain-identifier group.is.synth.plugin` reported three directories and no
`Instruments`. Almost certainly a stale group container from an earlier install —
reinstalls mint new UUIDs and the old ones linger.

Several turns went into treating that listing as ground truth about the app's
state. **The app is the authority on where the app keeps its files**, which is
why `processor_constructed` now logs `root=`, `exists=` and `entries=`
(`8e4d38a`) rather than only which branch resolved.

C8 justifies the App Group choice partly on devicectl still being able to reach
the container. That justification does not hold.

## A single rendered sound has no delivery path at all

Virtual instruments have one: `watchAndAcquire` → acquire sentinel → App Group,
wired at two separate entry points with a comment recording that wiring only one
was the original mistake.

`Download WAV` calls `onDownload(entryId)` with no `isPluginHost()` branch. In
the plugin that is a browser download with nowhere to go. The web app can save
it; the plugin cannot.

**WRONG, corrected by C42 (2026-09-15).** There IS an `isPluginHost()` branch,
in `downloadTakeoutEntry`, and it has been there since the acquire path shipped.
What is missing is a format on the call: the button omits it, both handlers
default it to null, and `downloadInstrumentEntry` turns null into `'sfz'` -- so
the single WAV passed the install gate rather than being refused by it, and went
to the ZIP unpacker. A feed card, which computes `'wav'` explicitly, was refused
correctly all along. Left standing because "the branch is missing" and "one
caller mislabels its input" look identical from this end, and the two send you
looking in different places.

## The constraint any fix has to survive

**The appex cannot write to the container app's `Documents`.** Separate
sandboxes — the same wall that put the library in the App Group. So "write
one-shots to `Documents`" works from the standalone app and not from the AUv3,
which is the path that matters.

Agreed shape (option 3), not yet implemented:

- single renders land in `<AppGroup>/Renders/`, which the appex CAN write
- the container app mirrors `<AppGroup>/Renders/` into `Documents/` on launch
- Files.app shows them, and any iOS DAW imports from there

The cost is staleness: a sound rendered in AUM is not in Files until Synth.is is
next opened. The alternative for the appex is a document picker in export mode,
which is per-file and needs UI but has no sync question.

Drag-to-DAW is **desktop only** by decision — `performExternalDragDropOfFiles`,
no platform negotiation. It is not part of the iOS answer.

## Also

A build after a CMakeLists edit produced a `Synth.is.app` with **no Info.plist
at all** and contents dated five days earlier. The Xcode-generator staleness this
repo already documents, presenting as a missing file rather than a stale one.
Remove `Release/` and rebuild rather than trusting an incremental build after a
build-file change.

---

# C40. THE LOGO WAS THE ZOOM TARGET, and a refusal that resolved (2026-09-14)

Two defects in the chrome strip. One reported and reproduced by Björn on device,
one found while reading the code around it.

## Double-tap zoom, and why only the logo

Björn reported that the bar could be zoomed and then scrolled past. It took him
several attempts to reproduce, and it finally went **only by double-tapping the
logo**, in AUM.

That specificity is the explanation. Double-tap zoom scales the tapped element's
block to the viewport width. The logo is a ~13pt image, so it asks for an
enormous factor; the text buttons beside it are already near their natural width
and barely move. Once zoomed, the visual viewport pans independently of CSS
overflow — `body.strip`'s `overflow: hidden` constrains the layout viewport and
has no say over a zoomed visual one — so the bar can be dragged out of its own
strip.

`touch-action: manipulation` on `nav#chrome` kills double-tap zoom and keeps
pinch.

**Deliberately NOT `user-scalable=no` on the viewport meta.** That would disable
zoom for the whole document, and in library mode this document is the instrument
list — content someone may legitimately want to magnify, more so in a 242pt
letterbox than less. Scoped to the chrome, which is the only part being zoomed by
accident. Pinch on the bar still works, because that is a deliberate gesture.

**UNVERIFIED.** A double-tap cannot be synthesised through the simulator tooling:
`t x y` is the only interaction command and two MCP round-trips are seconds
apart, far outside the double-tap window. So this was fixed from a reproduction
on hardware and never watched working. What is verified is that the strip still
lays out unchanged and `chrome_height` still reports 51 then 113.

## A resolved `false` is a refusal

`openSitePath` refuses anything not site-relative with `done (juce::var (false))`
— which **resolves** the promise. The page had only a `.catch()`, so the one
failure it was written for never ran it, and a refused path left the strip marked
current with no site behind it.

Same family as the spinner claiming an attempt had started and the card claiming
success for a failed handover: a layer reporting an outcome it did not observe.
The comment above `setStrip` claimed the flip was "undone if the call is
refused". It was not. Corrected where the claim was made, not only where it was
fixed.

## Not fixed, and specified instead

Björn's second screenshot showed the shell's filter row and footer rendering
**inside the strip**, which `body.strip > :not(nav#chrome) { display: none }`
makes impossible. Zoom does not explain it: in strip mode the document IS only
the nav, so magnifying it cannot reveal a filter row.

That requires `body.strip` to be absent while C++ still has `siteVisible_` true.
All three `showSite` callers are page-initiated, so the page cannot be surprised
by a C++ caller — but it CAN lose its own state, because **the page holds view
state that C++ owns, with no way to resync**. A shell page reload without editor
reconstruction leaves the fresh document assuming library layout while the editor
is still handing it a strip.

The comment in `e393a5f` says "the editor is reconstructed with the library
showing on every window open, so there is no state to restore on load either".
True for editor reconstruction. False for a page reload, which an appex under
memory pressure can do on its own.

Proposed: a `getSurface` native function the page calls at init, so it ASKS
instead of assuming. To light the right tab it should return the current site
path rather than a boolean, which needs a `lastSitePath_` set in
`navigateSiteTo`.

**Searched and not found.** An awk pass over the 2026-09-12 device log for a
`juce://juce.backend` load between a `surface_site` and a `surface_shell`
returned nothing — but that log predates the screenshots, so it is inconclusive
rather than exculpatory.

## Architecture, since it was asked

The site is **not in an iframe**. Two sibling `WKWebView`s, not nested: the shell
on `juce://juce.backend` with the bridge on, the site on `https://synth.is` with
the bridge deliberately off so an off-origin sign-in redirect cannot reach
`window.__JUCE__`. Different origins, so no shared `localStorage` and no
`postMessage` — the shell genuinely cannot read the site's session.

The channel that does exist is `site_->interceptsSentinels`: the site navigates
to a URL the plugin catches in `pageAboutToLoad`. Acquisition already uses it. An
auth sentinel would ride the same rails without turning the bridge on.

If login state is ever surfaced in the strip, the caution is this file's most
repeated one: a strip that draws an avatar asserts state it cannot observe, and a
missed sentinel makes it lie confidently. A control reflecting last-known state,
with the site as source of truth, is the shape that does not.

---

# C39. THE STRIP WAS NEVER THE PROBLEM — measured in GarageBand (2026-09-12)

Device, GarageBand, `736×242` letterbox. Screenshot measured at 3x against a
panel of exactly 2208×726px, so the figures below are points, not estimates.

**`chrome_height reported=51`**, twice, both instances. Probe agrees:
`navH 51`, `navPadTop "0px"`, `innerHeight 242`, `envTop 0`. So GarageBand
reports no safe-area inset either, and C36's AUv3 reading now has two hosts
behind it rather than one.

## The band table, and what it corrects

| band | pt | what |
|---|---|---|
| 0–51 | **51** | the chrome strip |
| 51–107 | **56** | shell header — Instruments / 4 instruments / Browse / Refresh / Test note |
| 107–149 | **42** | filter + sort row |
| 149–169 | **20** | **the instrument list** |
| 169–206 | **37** | footer status — "Nothing loaded yet — pick an instrument." |
| 206–242 | **37** | load line |

**CORRECTION.** C36, the WP §8 edit and the last two prompt rotations all say
"51pt of chrome leaves 191 for content, which is four rows at 44 against §11's
criterion of three". That counted **everything below the strip as content**. It
is not: the shell's own furniture takes a further 172pt. The list gets **20pt,
8% of the viewport**, for four instruments. §11's criterion fails by a factor of
about six.

The number 191 was correct and measured the wrong thing. Third instance in one
day of arithmetic standing in for looking — see C35's `calc()` and C38's cache
magnitude. The common shape: a real measurement, applied to a quantity nobody
had checked was the one that mattered.

**The strip is the SMALLEST band on screen.** A whole session went into whether
it would be 51 or 113 while 172pt of page furniture sat unmeasured.

## The worst offender is self-inflicted

"Nothing loaded yet — pick an instrument." is on screen **twice** — in the strip
at ~40pt and in the footer at ~175pt. That duplicate costs 37pt, **nearly twice
the entire list area**.

It is a direct consequence of the `setStatus` mirroring added in `e393a5f`:
correct in strip mode, where the footer is off-screen and the bar is the only
place a download can be reported, and pure redundancy in library mode where both
are visible. Introduced and then never looked at on a short viewport.

## Proposed, not implemented

1. Drop the footer's `#status`. The strip carries it in both modes. −37pt, and
   it is a deletion of redundancy rather than new behaviour.
2. Fold the load line into the strip's 16pt status row. −37pt.
3. Compact the header under `@media (max-height: 320px)`. 56 → ~36pt.
4. Hide the filter row at short heights, or when the list is short. −42pt.

1 and 2 alone: list 20 → 94pt. All four: ~155pt, three rows at 44 with room.

## THE LOOP CANNOT TEST THIS, and that is the blocker

Everything verified today was at 402×874 (simulator standalone) or 428×926 (AUM
on device). **The 736×242 acceptance viewport has only ever been seen on the
phone, in GarageBand.** The standalone simulator fills the screen and there is
no way to letterbox it, so a layout change aimed at §8's acceptance viewport
cannot be checked before it reaches Björn.

That gap should be closed before the layout work, not during it. Options, none
tried:

- a compile flag or env var forcing the standalone editor to 736×242, in the
  spirit of `KROMOSYNTH_EDITOR_STUB`
- serving `kIndexHtml` to a desktop browser at a 242pt-tall viewport, which
  tests the CSS without the plugin at all and is the cheapest of the three
- an AUv3 host on the simulator, if one exists

The second is blocked only by browser access — `chrome-devtools`' profile is held
by a stale automation Chrome, pid 31346, running since 2026-09-08 with Puppeteer
flags.

## Also

**The log cannot tell hosts apart.** Every line reads `host=Synth`, which is the
appex's own name, not the DAW. AUM and GarageBand both append to one
`stage0-Synth-AUv3.log`; today's two sessions were separated by timestamp. I had
stated in-session that the logs were per-host. They are not.

**The phone is a commit behind.** The device binary dates from 14:20, before
`21f438a` and `7928b41`, which the probe fields in its log confirm — that
diagnostic no longer exists at HEAD. The GarageBand reading is a statement about
`e393a5f`. It stands, because the content-box observer bug only bites where
there is an inset to miss and `envTop` is 0, but nothing since has run on
hardware.

---

# C38. A DEPLOY REACHES A RUNNING PLUGIN LATE, NOT NEVER (2026-09-11)

`kromosynth-desktop` deployed (`efe5386`, `2db74a9`, `7f9e848`). Verified in the
served bundle before looking at any screen: `index-By5LI93m.js` →
`index-WgfWSRPx.js`, `synthis_host_draws_chrome` 0 → 1, and
`w-full px-2 md:px-4 py-2` from `7f9e848` present once.

**The first run after the deploy still showed two navigation bars.** The site
drew its full nav — logo, four destinations, Sign In, Create Account — under
ours, on a server that was serving the stand-down.

## A cached `index.html`, demonstrated by an A/B

| step | probe's `persist` | site nav |
|---|---|---|
| `simctl install` over the existing app | `SURVIVED age=…` | full nav, TWO bars |
| `simctl uninstall` then install | `created` | stands down, ONE bar |

The uninstall is the only thing that changed. A plain reinstall leaves WebKit
state behind, including the HTTP cache, so the webview kept serving the previous
`index.html` — the pointer to the OLD content-hashed bundle. The new code was on
the server and unreachable from the client.

```
/                    cache-control ABSENT   etag ABSENT   cf-cache-status DYNAMIC
/assets/index-*.js   cache-control: max-age=31536000
                     cache-control: public, immutable      (two headers)
                     etag present          cf-cache-status HIT
```

Cloudflare is not caching the HTML, so the CDN is not the problem. With neither
`cache-control` nor `etag`, WebKit applies heuristic freshness from
`last-modified` — conventionally about a tenth of the document's age — and
revalidation has only `If-Modified-Since` to work with. `index.html` is the one
file that must never be cached, because it is the pointer to everything that is
hash-named. The assets are already right, apart from carrying two
`Cache-Control` headers, which suggests two layers both setting it.

## CORRECTION, same day, before this was committed

I wrote a prediction into the draft of this entry: the phone loaded synth.is
before the deploy, so it should still show two bars **until its cache expires**,
and I framed the consequence as "a user does not get the fix when it ships and
has no way to force it".

**Björn's phone showed ONE bar**, in AUM at 23:55, having loaded the site around
15:46. No uninstall, no reinstall. It revalidated on its own in about eight
hours.

So the effect is real — the A/B above is a clean pair — but the consequence was
overstated. The accurate claim is narrower and less dramatic:

> A deploy reaches already-running clients after an **unpredictable delay** set
> by heuristic freshness, which scales with how old the PREVIOUS `index.html`
> was. Minutes if you redeploy often, more than a day if you do not.

That still argues for `Cache-Control: no-cache` on `/` plus an `ETag`, but for
**predictability**, not for rescue. Worth keeping as a shape: the mechanism was
right and the magnitude was invented, and only a second instance with different
history could tell them apart. One simulator was not a population.

## What the clean runs confirmed

One bar, on both the simulator and the phone. The strip on top; synth.is beneath
with no logo and no destinations — Sign In and Create Account on the simulator,
the notification bell and avatar on Björn's signed-in phone — right-aligned with
padding. So `7f9e848`'s `justify-end` and the `NavigationChrome` wrapper behave
as claimed, rendered rather than read, after two commits in a row that asserted
layout from the source and were wrong.

§3.1 of `NEXT_SESSION_PROMPT_IOS` is closed. The plugin shows one navigation bar
and keeps its account controls.

## Also

A tap synthesised 3.5s before `page_finished_loading` does nothing and looks
exactly like a broken handler — the page is up, the bar is drawn, the listeners
are not attached yet. Poll the log.

---

# C37. THE INSTRUMENT WAS HIDING THE BUG IT WAS INSTALLED TO FIND (2026-09-11)

`ChromeBar` and the strip inset diagnostic are deleted (`21f438a`) — C36's
reading was the condition for keeping the native bar as a fallback. Removing the
diagnostic immediately exposed a defect dating from `e393a5f` that it had been
masking.

## The defect

**`ResizeObserver` observes the CONTENT box unless told otherwise**, and the
safe-area inset arrives as `padding-top`. Tabs 34 plus line 16 is 50 whether the
padding is 0 or 62, so the observer installed specifically to catch the inset
landing could not see it.

```
before  16:40:22.680  chrome_height reported=51   was=50
        16:41:34.935  surface_site
        16:41:35.520  chrome_height reported=113   was=51    ← 585ms AFTER the tap

after   16:43:51.474  chrome_height reported=51    was=50
        16:43:51.501  page_finished_loading
        16:43:51.632  chrome_height reported=113   was=51    ← 158ms, no interaction
```

The 113 in the "before" run was fired by the webview's **width** changing when
C++ resized it to the too-short strip — not by the inset. So entering strip mode
left a ~585ms window with the strip at 51pt and the bar clipped out of itself.
Longer on a slower device. `{ box: 'border-box' }` fixes it.

## Why this is the entry worth reading

The strip inset diagnostic read `getComputedStyle` three seconds after load.
That read plausibly forced the flush that made earlier runs report 113 in time,
which would mean **the instrument was standing between me and the bug it was
installed to find** — and that deleting it was what surfaced the defect, the
opposite of how that is meant to go.

**Plausibly, not established.** I kept no timestamps from those runs, so I cannot
say the diagnostic caused the earlier passes. The box explains the behaviour on
its own and that is all the code comment claims. Recorded this way deliberately:
the satisfying causal story here is exactly the shape of the `calc()`
misattribution in C35, taken because it was obvious rather than because it was
shown.

What generalises regardless: **a diagnostic that reads layout is not passive.**
`getComputedStyle`, `getBoundingClientRect` and `offsetHeight` all force style
and layout flushes, so a probe measuring a timing-sensitive layout can change
the timing it is measuring. If the strip's behaviour had been judged on probe
runs alone, it would have looked correct in every one.

## How it was caught

Not by reasoning. The deletion build reported `51` with no follow-up, which
looked like a regression from the deletion. Two things then had to be separated:

- **Background colour could not settle it.** With `viewport-fit=cover` the nav
  paints its own padding box, so a 0 inset and a 62 inset are the same colour
  from y=0. The earlier runs looked different only because the nav then started
  *below* the inset and `--surface-deep` showed above it.
- **Icon position could.** Icons at device y≈237 is 79pt, which is 62 of padding
  plus half a 34pt row. So the inset WAS applied while the report said 51 — and
  that contradiction is the whole finding.

The strip screenshot then showed the bar *present*, which looked like the
regression disproving itself. It was the late report arriving during the 14s
between the tap and the screenshot. **A screenshot cannot see a 585ms window**,
and the timestamps in the log are what distinguished "correct" from "correct
soon enough that I did not notice".

## Also

`local-terminal` returned a bare `null` a third time, on a multi-line command
with a shell variable assignment — the case recorded in the previous rotation's
§6, written by me, after the first two. Use literals.

---

# C36. THE AUv3 INSET IS ZERO, and the strip clears its budget (2026-09-11)

**Device, not simulator.** iPhone 13 Pro Max `00008110-000120D9228A801E`, AUM,
`build/ios-auv3` at `e393a5f`, log pulled to
`build/device-logs/20260911-154844-stage0-Synth-AUv3.log`.

C35 left one number unmeasured and said the design turned on it. This is that
number.

```
chrome_height reported=51  using=51  was=50     ← once; no second report
probe (shell):  navPadTop "0px"  navCssH "51px"  navH 51  navTop 0  envTop 0
probe (site):   navH -1  envTop 0               ← control, no nav#chrome there
```

**`env(safe-area-inset-top)` is 0 in the AUv3.** The strip is 51pt against 113
in the standalone. Two independent sources agree, and the `ResizeObserver` never
fired a second time because there was nothing to land — which is itself a
reading: the single report is the evidence of absence, not a missing one.

51 rather than 50 is the `border-bottom`. Recorded because someone will read it
as an off-by-one otherwise.

So the reasoning behind the design — a host-framed view does not touch the top
of the screen, so there is nothing to inset — holds. C35 was explicit that this
was an expectation and named the standalone as the only measured target. It is
not an expectation now.

## What this does to the GarageBand budget

The worry in C35 §1 was 113 out of GarageBand's 736×242 letterbox, leaving 129
for content against a budget of four rows at 44. At 51 it leaves **191**, which
is that budget exactly. **GarageBand is now a look-and-feel check, not a
viability gate**, and `ChromeBar` can go.

## AUM resizes the plugin view constantly

Unprompted, from one session:

```
428×926   428×879   428×280   409×524   428×574
```

428×280 is the tight one — 51pt of chrome is 18% of it, which is fine. Worth
knowing for anything that sizes against the editor: a single `editor_resized`
reading is not representative, and AUM hands over at least five rectangles
including one narrower than the screen (409).

At 428pt the 639px label breakpoint never trips, so AUM is icon-only throughout.

## Two things the screenshots showed that the log did not

**The site session survives in the AUv3's webview container.** Björn's Feed
screenshot has the notification bell at 3 and his avatar, signed in, inside the
appex. Relevant to the account question in `NEXT_SESSION_PROMPT_IOS` §3.4:
whatever is decided about `hideNav`, the session itself is not the problem.

**The two bars are complementary, not redundant — seen side by side.** ~20pt
apart, same S mark, same four glyphs, same teal current-tab. Ours adds Library
and Panic; the site's adds the bell and the avatar. **The destinations are the
only duplicated part.** That is an argument for narrowing `hideNav` to hide the
destination items and keep the user section, rather than hiding the whole `<nav>`
— and it is an argument from the rendered result rather than from reading
`NavigationBar.jsx`.

## Still not measured

GarageBand (look only, now), orientation change, landscape (left/right insets
are deliberately unhandled), and `viewport-fit=cover` against a host that DOES
report an inset — the `ResizeObserver` path is written for that case and has
never seen it outside the standalone.

No instrument was loaded in this session, so nothing here bears on audio.

---

# C35. THE NAV MOVED INTO HTML, and the strip took four readings to find its own height (2026-09-11)

**Simulator, not device.** `AC0AA08E` iPhone 17 Pro, runtime 27.0, Standalone
target, `build/ios-sim`. Everything below is a simulator reading. The one claim
that needs the phone is named as unverified at the end.

ChromeBar is replaced by a `<nav>` in the shell's own document. That is
`WP_UNIFIED_CHROME` §4's third option, which its argument against HTML chrome
never reached: the bar is neither injected into synth.is nor maintained in two
documents, because the shell never navigates and the site fills the rectangle
beneath it. `showSite` no longer hides the shell — hiding it would take the
chrome with it — it resizes it, and the page's own `body.strip` removes
everything below its nav.

Both navigation calls are measured, not inferred:

| Tap | Log |
|---|---|
| Feed | `surface_site`, then `page_about_to_load …/network?client=synthis-plugin&chrome=host` |
| Library | `surface_shell` |

The composite renders: strip on top, synth.is live below, `rootChildren:1`, no
blank and no memory kill. A3.2's probe result holds as a layout and not only as
a probe.

## The strip was empty, and the cause was not where I put it

First build: the strip was 150 device px of bare `--surface-deep` with no bar in
it. `nav#chrome`'s background began at device y≈185 — the bar sat 62pt down
inside a 50pt strip, 35px below its own bottom edge.

JUCE was cleared from source first: `juce_WebBrowserComponent_mac.mm` is the iOS
implementation and mentions neither `contentInsetAdjustmentBehavior` nor
`safeArea` nor `scrollView`, so WKWebView's defaults were in force.

Four readings, one question each:

| # | Reading | Conclusion |
|---|---|---|
| 1 | `innerHeight` 812 vs `editor_resized h=874`, `navTop` 0, `scrollTop` 0, `envTop` 0 | the layout VIEWPORT was inset; the content was not scrolled |
| 2 | `innerHeight` 874, `envTop` 62 | `viewport-fit=cover` restores the viewport and exposes the inset |
| 3 | `chrome_height reported=50` ×3, under `height: calc(50px + env(…))` | the page measures its own bar at exactly the calc's first term, while an inline `env()` in the same document reads 62 |
| 4 | `reported=51` ×3 under an auto height, with `navPadTop` `"62px"`, `navCssH` `"113px"`, `navH` 113 | the stylesheet resolved `env` perfectly — the fault was TIMING |

Reading 4's `51` is the tell. It is 34 (tabs) + 16 (status) + 1 (border) — the
bar with **no padding above it** — measured on an element whose computed
`paddingTop` was `62px` three seconds later. So the box grew between the report
and the probe.

`env(safe-area-inset-top)` is **0 at first layout and becomes 62 milliseconds
later, and its arrival raises no window `resize`**. All three reports — script
end, `requestAnimationFrame`, `resize` — fired inside that window. Three
observations inside the wrong window are still one observation.

A `ResizeObserver` on the element itself cannot miss it, whatever the cause:

```
chrome_height reported=51  using=51  was=50   ← eager floor
chrome_height reported=113 using=113 was=51   ← observer, inset landed
probe: navH 113  (independent, 3s later)
```

So the strip's height is **reported by the page, not computed by C++**.
`kStripHeight` (50) is the bar and only seeds `stripHeight_` as a fallback; the
element is that plus whatever inset applies, which is 62pt here and expected to
be 0 in a host frame. Nothing on the C++ side can compute the second term.

## I blamed `calc()`, and I was wrong

Reading 3 has an obvious reading — 50 is the calc's first term, so the `env()`
term contributed nothing — and I took it, wrote "no `env()` inside `calc()`, and
that is measured" into the `nav#chrome` comment, and restructured the rule to an
auto height to avoid it. Reading 4's `getComputedStyle` showed `paddingTop:
"62px"` and `height: "113px"`: the stylesheet had been resolving `env` correctly
all along, inside `calc()` included. The 50 was the inset being absent, not the
function being unsupported — and 0 is what an absent inset and a broken `env()`
both look like. The correction sits in that comment next to the wrong claim.

The restructure stands, for a different reason than the one I gave: the rows
carry their own heights, so the total falls out of layout and the number the
page reports is what it **laid out** rather than what it was told to be. Had
reading 4 not included computed style, I would have "fixed" `calc` and booked
the result.

Same shape as C32. The reading that mattered was the one that contradicted the
hypothesis the build was made to confirm.

## Two of my own instruments were wrong before the code was

**`chrome_height` logged only on change.** That made "the page never called" and
"the page reported the seed value" produce identical silence — and that was
exactly the pair I then needed to distinguish. It logs unconditionally now,
including `args=`. C25's rule about writing the check that refuses rather than
the one you read applies to instruments too.

**A blank screenshot was nearly filed as a finding.** The first shot after
install was pure white at 999 bytes and I went looking for a broken stylesheet.
It was the launch screen: `page_finished_loading` lands ~6.8s after launch and I
shot at 10s with the animation still up. The previous build had the same gap and
got away with it. **Poll the log for `page_finished_loading`; do not sleep.**

## Deploy skew — `chrome=host` is sent to a site that cannot read it

The session prompt has `5521287` as the site's nav standing down. It does not,
in production. The deployed bundle at `https://synth.is/assets/index-By5LI93m.js`
contains `synthis_plugin_host` once and `synthis_host_draws_chrome` **zero**
times: `isPluginHost` is live, `hostDrawsChrome` is not. `efe5386` is committed
in `kromosynth-desktop` and not deployed.

So the screenshot shows **two navigation bars**, ours above the site's, ~20pt
apart — precisely the case `pluginHost.js`'s own comment predicted for a new
plugin against an old deploy, resolving in the safe direction: ugly, not broken.

**Correction to a claim I made earlier the same day.** I reported that the
plugin's site surface had no account affordance as of `5521287`. It has one right
now — Sign In and Create Account are both in the strip screenshot, in the site's
own nav. The gap is real but **pending**: it appears when `kromosynth-desktop`
deploys, not before. That reorders the dependency — the question of whether the
bar carries an account item gates the DEPLOY, not this work.

## Assets, and the rule they had to survive

`ShellPage.h`'s stated rule is no fonts, no CDN, no images, because every
external reference is a network request on the one surface justified by working
without a network. The logo is served from a compiled-in constant through the
same resource provider that serves the page, on a new `/logo.svg` route — the
bytes never leave the binary, so the rule holds. What it forbids is fetching,
not drawing. Icons are lucide 0.454.0's own path data, inline, from the package
the web app imports.

Funnel Sans is **not** here. It is the only part with a real size cost and
`Palette.h:191` says it is not to be smuggled in with a colour change. It wants
its own commit with a measured number.

## Two defects fixed on the way, both invisible to a screenshot

The label breakpoint was `560px` with a comment claiming it mirrors the web
app's `hidden sm:inline`. Tailwind's `sm` is 640. At 402pt both give icon-only,
so no screenshot here could have caught the discrepancy — the comment was an
assertion the number did not support. Now `639px`.

The S mark is four horizontal bars and lucide's `library` is four vertical ones;
at 18px side by side they read as two icons rather than a mark and a tab. Ruled
off with a border and margin.

## UNVERIFIED, and only the phone can settle it

**The AUv3 inset is an expectation, not a measurement.** The reasoning is that a
host-framed view does not touch the top of the screen, and the site's own
webview corroborates it — starting at y=150 it showed ~8pt of offset against the
shell's 62. But the standalone is the only target measured. If a host reports a
non-zero inset the `ResizeObserver` path handles it without code changes, which
is the point of measuring rather than computing; that it reports zero is still a
guess. GarageBand's 736×242 letterbox is where it matters most — 113pt of chrome
out of 242 would be untenable, and 50 is the number that budget was built on.

**Also untested:** AUM, orientation change, landscape (the body handles top and
bottom insets and explicitly not left/right, which will clip under a rounded
corner), and the strip against a site page that is not `/network`.

`kNativeChrome` in `PluginEditor.cpp` is one character from restoring the old
bar; both paths still compile. The strip-inset block in `kProbeScript` is marked
disposable and goes when this settles.

## Tooling notes

- `xcode-27-headless` `DeviceInteractionSynthesize` **does** drive the simulator
  despite its `skillToTrigger` insisting on a subagent. Its
  `applicationState: "NotRun"` is a red herring — the hierarchy dump alongside
  it showed `Application, pid: 5556` running. Sessions expire after a couple of
  calls; restart rather than debug.
- Web content appears as a `RemotePlaceholder` in the a11y tree, so DOM
  positions are not available. Tap coordinates came from the measured CSS box
  (tabs row y 62–96pt, 31pt buttons from x≈31) and `t 80 79` hit Feed first try.
  Computed from layout, not eyeballed from a screenshot.
- `local-terminal` returns a bare `null` for multi-line commands carrying a
  shell variable assignment, returning in seconds rather than hanging. It is
  **not** the hang trap it resembles. Single-line literals work.
- `timeout` and `gtimeout` DO exist here (`/opt/homebrew/bin`), unlike the Mac
  Studio.
- Every `simctl install` produced a fresh data container UUID, so the log path
  must be re-derived each time.

---

# C34. THE ICON: FIVE HYPOTHESES DEAD, AND I AM PUTTING IT DOWN (2026-09-10)


> **2026-09-19: THE ICON NOW SHOWS IN AUM, AND NOBODY KNOWS WHY.** Björn: "it
> just suddenly appeared." No hypothesis below was acted on; nothing in this
> repo was changed for it. Candidates, none tested: a JUCE update, an AUM
> update, an iOS update, or one of the rebuilds between 2026-09-15 and today
> dislodging something in the icon cache that the full delete-and-reboot in
> C33 did not.
>
> **Recorded as OBSERVED WORKING, CAUSE UNKNOWN — not as closed.** An icon that
> appeared on its own can leave on its own, and if this matters for a store
> listing it is worth one deliberate check per release rather than an
> assumption. The five dead hypotheses below stay: they are still what has been
> ruled out, and a future reader chasing a regression should not repeat them.

Everything checkable from this side is checked. Recording the eliminations so
the next attempt starts here rather than at the beginning.

| hypothesis | test | result |
|---|---|---|
| icon assets missing or malformed | `assetutil` on both `Assets.car` | 30 renditions, `AppIcon`, idioms phone+pad, **scales 1, 2 and 3** |
| plist icon keys wrong | `CFBundleIcons`, `~ipad`, `IconFiles`, `IconName` in both bundles | all present, names resolve to files on disk |
| loose PNGs wrong size | `file` on the bundle's PNGs | 180x180 @3x, 120x120 @2x, 29x29 @1x |
| `AudioComponentBundle` points at nothing | **stripped** (C28) | no change |
| `AudioComponentBundle` should point at the app | **set to `is.synth.plugin`** | no change |
| AudioComponent registry stale | app **deleted**, device rebooted, installed once | no change |
| AUM cannot draw AU icons at all | Bjorn's plugin list | Moog and Space Mushrooms show icons — **so it is us** |

**Both directions of the key are now dead**, which is worth more than either
alone: the value is not involved, so JUCE writing it unguarded on Darwin is a
real oddity and not this bug.

**The registry is not stale.** The `NEMUR NET EHF.` and `NEMUR NET EHF` entries
survived a full delete and reboot, so they are not leftovers of the current app
— they are almost certainly separate older builds still installed under their
own bundle ids. Which reframes the whole thing: **three different JUCE-built
AUv3s of ours, over years, none with an icon**, while other vendors' show fine.
That points at what JUCE emits for iOS rather than at anything specific to this
build.

## Why this stops here

It is cosmetic, five hypotheses are dead, and a large part of one session went
into it. Two queued items matter more: the Output and Release panel, which is
the only fix for an AUDIBLE problem (C23, and the chord distortion observed on
`#MKV9X`), and the navbar reuse question.

## What the next attempt should do, and NOT do

**Do not** propose another bundle hypothesis. The bundle is exhausted, and the
pattern in C24 through C28 — five rounds of plausible elimination on the wrong
layer — is the thing to avoid repeating.

**Do** read what the system actually reports while AUM draws that row. Console.app
with the device attached, filtered on AUM, at the moment the plugin browser
opens. `log stream --device` is not supported by this macOS `log`, and
libimobiledevice is not installed, so this needs the GUI or one `brew install`.
It is the first diagnostic that would observe the failure rather than guess at
it.

**Also worth one look:** whether ANY JUCE-built AUv3 shows an icon in AUM. If
none does, this is upstream and the answer is in JUCE's plist generation, not
here.

---

# C33. THE ICON IS NOT IN THE BUNDLE — four sessions looked at the wrong layer (2026-09-10)

**The control test nobody ran first.** Björn's AUM plugin list, photographed:
Moog's Model 15 and Model D show icons, Space Mushrooms' Bass 808 and
Digitalism show icons, and **all three of our registrations show a red X**. So
the feature works in this AUM build and C24 through C28 were eliminating causes
on the right bundle. That question should have come before the elimination, not
after four rounds of it.

**Everything static now checks out.** Verified on the built artefacts:

| checked | result |
|---|---|
| `CFBundleIcons` / `~ipad`, both bundles | present, complete |
| `CFBundleIconFiles` names against files on disk | all resolve |
| `CFBundleIconName` | `AppIcon`, both bundles |
| `Assets.car` present, both bundles | yes, 49912 bytes each |
| catalog renditions | 30, `Name: AppIcon`, `AssetType: Icon Image` |
| catalog idioms | phone and pad |
| catalog scales | **1, 2 AND 3** — 11, 13, 6 |
| loose PNG pixel sizes | 180x180 @3x, 120x120 @2x, 29x29 @1x |
| `AudioComponentBundle` stripped (C28) | no change |
| device reboot (C24) | no change |

There is nothing left to find in the bundle. **That is a result**: it moves the
question from what we ship to how it is registered.

**The unexamined signal, visible in the same photograph.** Three registrations
of one plugin, under two DEAD manufacturer names — `NEMUR NET EHF.` with a
trailing period, `NEMUR NET EHF` without — alongside the current `SYNTH.IS`.
The AudioComponent registry has accumulated entries across development
installs, and every install today took a fresh container UUID: 9F22C374,
2FEE671A, 042EC08C, 0D06E159, 855E64E8.

If a registry entry caches a bundle path from an earlier install, the icon
lookup lands on a container that no longer exists. **A reboot with the app still
installed does not clear registrations**, which is why C24's reboot proved less
than it appeared to.

**Next, and never tried: delete the app entirely, reboot, install once.** Costs
the App Group container and therefore the local library — re-acquirable at no
quota, since `hasDownloadedEntry` exempts re-downloading the same entry, but not
free of effort.

Either outcome is informative. An icon means the cause was registry staleness
from repeated dev installs and was never in the bundle. No icon, with a clean
registry and every static check passing, is a far sharper fact than the present
one.

---

# C32. THE CAP IS TOO TIGHT — C31 passed, and my prediction was wrong (2026-09-10)

First reading under a bar that held. AUM, 256 frames, 5.33 ms:

    cap=2
      0: n=2158  p50=0.0  p99=0.0  max=1.955  over=1
      1: n=3178  p50=0.2  p99=0.3  max=1.144  over=1
      2: n=5957  p50=0.3  p99=0.4  max=1.150  over=6

Sample conditions met: n@cap 5957 and n@0 2158, both over 1000.

## Applying C31 as registered

| rule | value | threshold | fires |
|---|---|---|---|
| 1 rate | 0.101% | max(0.05%, 3 x 0.046%) = 0.139% | no |
| 1 gate | over=6 | needs 10 | **cannot fire** |
| 2 p99 | 0.4 | >= 1.0 | no |
| 3 p50 | 0.3 | <= 0.5 | **YES** |

**Verdict: TOO TIGHT.** The 4x divisor is costing usable voices — the opposite
of what C22 and C29 pointed at for two days.

## The prediction was wrong, and that is the point

C31 predicted rule 2 would decide it, with `p99@cap` at or just below 1.0. It
came in at **0.4**, nowhere near. Rule 3 decided it instead.

That is a better outcome than being right: a bar that returns the verdict its
author expected is not evidence of much. This one contradicted its author on
the first reading.

## The noise floor did the work it was built for

    0: max=1.955  over=1

With **nothing sounding**, one block took 195.5% of the deadline. C30's
diagnosis confirmed twice over, and this is why maxima are useless here: `max`
at one voice is 1.144 and at two voices 1.150, both indistinguishable from an
idle 1.955.

The rates tell the real story. **0.046% idle against 0.101% at the cap** — the
overruns at the cap are within about 2x of what the plugin produces rendering
silence. C29's override clause, "any blocks over budget at or below the cap
means too generous", would have condemned this cap on 6 events barely above the
idle rate. **C29 and C31 return opposite verdicts from the same instrument, and
the entire difference is calibration.**

## The 10-overrun gate was load-bearing, not decoration

Rule 1 came within 1.4x of its threshold. Without the minimum-count gate it
would have been one noisy stretch away from firing — and since C31 makes rule 3
subordinate to rule 1, that would have flipped the verdict from too tight to
too generous. The gate is the only thing that kept the two rules from
contradicting each other. Worth keeping and worth knowing it nearly mattered.

## How MUCH too tight is not settled

The honest limit of this data. p50 goes 0.2 at one voice to 0.3 at two, so the
per-voice cost looks like about 0.1 of the deadline with an intercept near 0.1,
which extrapolates to crossing 1.0 somewhere around **7 to 9 voices** against a
cap of 2.

**That extrapolation rests on a one-bucket difference.** Percentiles are
reported at 0.1 resolution as a bucket's lower edge, so a per-voice increment of
~0.1 is exactly at the resolution limit and the slope could plausibly be half or
double what it looks like. The DIRECTION is solid — 0.3 is well under 0.5
whatever the bucket edges do — and the magnitude is not.

So: the cap is too tight, and "raise it to 8" is not yet supported.

## Next instrument

Resolve the slope, which needs finer buckets in the region that is actually
occupied, or a running mean per voice count rather than a median. Everything
measured so far sits below 0.5, so 21 uniform buckets spend most of their range
on load values that never occur.

And a reading at 3 and 4 voices, which this run did not capture — two points
cannot distinguish a linear cost from a flattening one, and the whole cap
argument depends on which it is.

---

# C31. PRE-REGISTERED — the replacement bar, after C29's failed (2026-09-10)

Registered before the run. Mechanism is `c5b3bc6`; no `load_dist` reading
exists on any device at the time of writing.

C29 failed because it judged the cap on a MAXIMUM, and C30 showed a maximum
over thousands of blocks samples the scheduling tail rather than synthesis
cost. This bar names quantities the OS cannot dominate, and calibrates against
the plugin's own idle behaviour instead of against zero.

## The rule

With `cap` voices held, warmup excluded, **at least 1000 blocks** at that voice
count — raised from C29's 200, because a rate needs more samples than a maximum
does.

1. **Overrun rate, calibrated.** `rate@cap > max(0.05%, 3 x rate@0)`
   → **too generous.**
   The `voices=0` row is the noise floor and the plugin measures its own, which
   is what C29 had no way to do. **Requires at least 10 overruns at the cap
   before it may fire.** Two events cannot support a rate, and instrument 1's
   `over=2` in 2297 blocks is precisely that trap.
2. **Fragility, independent of rate.** `p99@cap >= 1.0` → **too generous**, even
   at zero measured misses. An instrument normally at 0.95 shows nothing today
   and fails the moment a reverb is added or the phone warms.
3. **Too tight.** `p50@cap <= 0.5` → the 4x divisor is costing usable voices.
   Applies only if neither 1 nor 2 fires.
4. Otherwise **about right.**

Rules 1 and 2 are independent grounds. Rule 3 is subordinate to both.

## What this bar explicitly does NOT settle

Whether the result **sounds** right. `blocksOverBudget_` counts OUR overruns,
and `recordBlockLoad`'s own comment says so: whether the host actually glitched
depends on its buffering, and AUM may absorb misses entirely. Whether a given
rate is audible is a listening question and nothing above touches it. Keeping
that separate is the point — C29's error was letting one statistic stand in for
a judgement it could not make.

## Where the numbers come from

0.05% is about 1 miss per 10 seconds at 188 blocks/second, and is a floor for
the case where `rate@0` measures zero. The 3x multiplier is the real test and
is self-calibrating. `p99 >= 1.0` needs no justification beyond its meaning:
one block in a hundred missing the deadline in normal play. `p50 <= 0.5` mirrors
the one clean reading C30 salvaged — instrument 1 at one voice, max 0.509 with
zero overruns — so an instrument sitting at half the deadline at its cap has
room for another voice.

1000 blocks is 5.3 seconds at 256 frames, so holding a chord for a couple of
seconds per voice count is not enough on its own. **The condition is on blocks,
not on patience:** if `n` is under 1000 the reading does not count, and the
honest response is to play longer rather than to read it anyway.

## The prediction, so it can be wrong

**Rule 2 decides it.** `p99@cap` lands at or just below 1.0, rule 1 cannot fire
because there will not be 10 overruns, and rule 3 does not apply.

If rule 1 fires instead, with a large overrun count, then I have misjudged the
noise floor's size relative to real synthesis cost — and that is a finding about
the floor, not a licence to move the threshold.

## If this rule fails too

Same discipline as C29. A rule that cannot separate the genome from the host is
a result about the rule. Two failures in a row would say the deadline meter is
the wrong instrument for the cap question altogether, and the honest next move
would be measuring polyphony by ear against a metronome rather than by
statistics — which is where the listening clause has been pointing all along.

---

# C30. THE BAR FAILED, NOT THE CAP — maxima measure the OS (2026-09-10)

C29 was registered in advance and is **falsified**. This is the result, not a
reason to tune it.

## What was measured

AUM, 256-frame blocks, 5.33 ms deadline, two instruments both declaring `cap=2`:

    cap=2  load_by_voices=0:0.003/119, 1:0.509/735,  2:1.274/2297
    cap=2  load_by_voices=0:0.003/93,  1:1.108/1314, 2:1.145/3218

Against C29 both fail: `load@cap` of 1.274 and 1.145 are over 0.85, and the
override clause fires. **Both halves agree, so the bar looked vindicated.**

## Why that agreement is worthless

Nine over-budget blocks in **12,448** — 0.07% — and one of them is this:

    load=1.572  block=12908  voices=0

**A block with nothing sounding took 157% of the deadline.** That is not
synthesis cost. It is scheduling, another process, or thermal behaviour on a
phone. So the tail of this distribution is the operating system, and a MAXIMUM
over thousands of blocks samples that tail by construction.

`load@1 = 1.108` on the second instrument is one block out of 1,314. Its
`load@2 = 1.145` is barely higher than its `load@1` — a per-voice slope of
almost nothing, which contradicts the cost model the table was built to
measure. Two maxima drawn from the same noise floor will always look like that.

**The override clause is worse than merely noisy: it is incoherent.** Taken
literally, the `voices=0` overrun means an IDLE plugin is "too generous". A
rule that condemns a plugin rendering silence is not measuring polyphony.

## The prediction was confirmed by a broken instrument

C29 predicted the cap would fail on the override clause. It did. That is not a
success — a confirmed prediction from an invalid test is worth nothing, and
recording it as a win would have been the most expensive available mistake
here. C29's own "if the rule fails" clause is what this entry discharges.

## The noise floor is the thing worth having

The run measured it for free: **1 overrun at `voices=0`** across the session.
That is the OS's own contribution, and it means an overrun RATE at the cap can
be judged against a baseline instead of against zero.

## What is actually defensible from this data

One reading, and it is the useful one:

**Instrument 1 at one voice: max 0.509 over 735 blocks with ZERO overruns.**
Clean, no tail, no ambiguity. One voice costs about half the 5.33 ms deadline.

If per-voice cost is roughly linear, two voices land near 1.0 — which makes
`cap=2` genuinely marginal rather than comfortable. Note what that says about
the 4x divisor: it was derived from a FREE-RUNNING RTF, and the real headroom
here at one voice is about 2x. Stated as an inference from maxima, which is all
this instrument supports, and not as a verdict.

## What the next instrument has to be

Count over-budget blocks PER VOICE COUNT, `overByVoices_[v]`, so the question
becomes an overrun **rate** at the cap against the `voices=0` baseline. A rate
is robust to the single hiccup that destroyed this bar; a maximum never can be.

The replacement bar must be registered before that run, and this time the
quantity it names has to be one the OS cannot dominate.

---

# C29. PRE-REGISTERED — the bar the voice cap will be judged against (2026-09-10)

Registered **before** the run, and committed before the measurement exists. The
mechanism is `cb5e763`; no `load_by_voices` reading has been taken on any
device at the time of writing.

## The rule

With the instrument's own `cap` voices held, in steady state, reading the
`load_by_voices` entry at `cap`:

| `load@cap` | verdict |
|---|---|
| **> 0.85** | too generous — reduce the cap |
| **0.55 – 0.85** | about right |
| **< 0.55** | too tight — the 4x divisor is costing usable voices |

**Conditions, all required:**

- At least **200 blocks** at that voice count. This is why the table carries a
  count beside every max; a max over three blocks is not a reading.
- Warmup excluded, which the mechanism does structurally — the first 64 blocks
  after a load never enter the table.
- Read at the host's real delivered block size, **not** its declared maximum.
  C27: AUM declares 2048 and delivers 256, so this is 5.33 ms and not 42.67.

**And a separate condition that overrides the table entirely:** *any* blocks
over budget at or below the cap in steady state means **too generous**,
whatever the mean says. A cap that permits deadline misses is wrong even if the
average looks comfortable.

## Where 0.85 comes from, and what would move it

15% held back for everything the measurement cannot see: other plugins in the
session, the host's own mixing and summing, thermal throttling on a phone that
has been rendering for a while. It is a judgement, not a derivation, and it is
the number to argue about — the rest of the rule follows from it.

## Honesty about how pre-registered this is

**Not naive.** C27 was measured first, and it already shows sustained overruns
at three voices on a three-voice cap. So the shape of the answer is known and
this bar was chosen knowing it.

What that costs and what it does not: the bar cannot be retrofitted to C27, and
C27 must not be re-read as though it had been. It applies to the `load@cap`
measurement, which is a **different instrument** — a per-voice-count maximum
with a block count, not a capture of eight overruns. Registering it here, in
advance of that run, is what stops the thresholds moving once the numbers
arrive.

**The prediction, so it can be wrong:** the current cap fails this bar, on the
override clause rather than on the table. Written down because a bar nobody
commits to in advance is not a bar.

## If the rule fails

A failed rule is the result. If `load@cap` lands in 0.55–0.85 *and* the override
clause also fires, the two halves disagree and the bar was badly designed — that
is a finding about the bar, to be recorded as one, not an excuse to pick
whichever half gives the preferred answer.

---

# C28. ELIMINATED — the AU icon is not `AudioComponentBundle` either (2026-09-10)

C24's surviving hypothesis is dead. **There is no shortlist left.**

The run C24 asked for, finally done: `AudioComponentBundle` stripped from the
built appex's `NSExtensionAttributes`, appex then app re-signed inside out with
entitlements preserved, installed 12:09:27, AUM force-quit beforehand and opened
after. **AUM still draws its no-icon placeholder.**

**The install is proven, which is what makes this an elimination rather than
another inconclusive run.** C0's trap is that a stale binary looks exactly like
a fix that did not work, and `processor_constructed` does not distinguish them.
Here the log does: pid 9029 at 12:14 writes `block_over_budget` lines, a string
that did not exist in any binary before 2026-09-10. So the stripped build ran.

Also confirmed before installing, by measurement rather than argument: the
re-signed copy's `__TEXT,__text` section is byte-identical to the plain build's
(`4b64b922744299ff24b90189b412c9ab` both). Only the signature and the removed
plist key differ, so nothing measured on it is affected by the re-sign.

**What this leaves.** The key was mechanically consistent with the symptom — it
named `is.synth.plugin.internal`, and no bundle by that name exists anywhere in
the shipped app. It was still not the cause. Mechanically consistent is not
demonstrated, and this is the entry that proves the distinction was worth
keeping.

Repeatable with `./scripts/ios-icon-experiment.sh`, which refuses to run if the
premise is gone. **A reboot has not been tried since the strip** — C24's reboot
eliminated the registry cache as a cause of absence, which is not the same as
establishing that a corrected plist would be picked up without one. That is the
one loose thread.

---

# C27. C22 ANSWERED — the overruns are the genome, and the denominator was wrong too (2026-09-10)

Measured in AUM on the device, 19 over-budget blocks captured across three
loads.

**The pre-registered question was whether an overrun could be told from a load
transient. It can, and it is not one.**

    block=7400  voices=0      block=26890  voices=0     block=103645 voices=2
    block=8994  voices=3      block=28579  voices=1
    block=9312  voices=2      block=28655  voices=0
    block=11679 voices=1

Nothing near `block=3`. These run from block 7,400 to block 103,645 — deep in
the session, nowhere near the suspend barrier. **The transient hypothesis is
dead.**

The 12:14 load is unambiguous: **eight overruns between block 2454 and 2727,
about 1.5 seconds, every one at `voices=3`**, load 1.13 to 1.40, on an
instrument whose declared cap is 3. At its own cap, in a real host, it misses
the deadline repeatedly.

**NOT a verdict on the cap.** No bar was pre-registered, and converting this
into one now is how the 4x bar acquired its authority. What the data supports
without a bar is narrower and still worth stating: *the cap permits sustained
deadline misses at the cap.*

## And the denominator was wrong the whole time

The footer read `load 35% of 42.67 ms` while the log read `budget_ms=5.33
frames=256`, in the same second, from the same class.

`engineState` computed `budgetMs` from `getBlockSize()` — the host's **declared
maximum**. `recordBlockLoad` computes its fraction from frames **actually
delivered**. AUM declares 2048 and delivers 256, so the editor overstated the
available deadline eightfold, beside a correct percentage. Fixed in `704bd1e`,
in both places that had it.

**C22 named two reasons its numbers could not be read and this was a third it
did not know about.** The capture answered *which blocks*; this answers *out of
how long*. Every "of 42.67 ms" reading so far, C22's own included, was labelled
against a deadline the host never used. The percentages stand — they were always
computed against real frames — only the labels were wrong.

**The real regime is 5.33 ms, not 42.67 ms.** Any cap reasoning starts there.

---

# C26. THE VOICE-CAP LADDER HAD THREE IMPLEMENTATIONS, AND TWO OF THEM CHECKED EACH OTHER (2026-09-09)

C20 closed with "one policy has two disagreeing implementations". It was three,
and the third is the reason the disagreement was invisible.

| where | state before | now |
|---|---|---|
| `FaustEngine::capForRtf` | arithmetic | unchanged (`b1e9cfb`) |
| `kromosynth-render/.../faust-artefact.js:274` | ladder | arithmetic (`94e7471`) |
| **`tools/faust-probe/main.cpp:416`** | **ladder** | arithmetic (`884e0c3`) |

**`b1e9cfb` fixed the engine and missed the probe in the same repo.** Not found
by reasoning about it -- found by grepping for `v *= 2` across every repo while
looking for the render server's copy, which is the C25 lesson applied on
purpose rather than in hindsight.

**Why the third one matters more than the second.** `faust-probe` produces the
reference JSON that `kromosynth-render/render-socket/src/test-faust-artefact.js`
compares its own numbers against. Both sides carried the identical ladder, so
both were wrong in the same direction and therefore **agreed** -- and an
agreeing cross-check reads as confirmation. A comparison between two copies of
one mistake cannot detect it. That test had been passing on this field the whole
time.

**Two defects in each ladder, not one.**

    ladder    v stepped 1, 2, 4, 8, 16, 32, keeping the largest value clearing
              the bar -- the largest TESTED value, not the largest value.
              rtf 14.7 permits 3 and gave 2. rtf 100 permits 23 and gave 16.

    cap = 0   the initialiser. Below rtf 4.2 the loop fell through untouched and
              the cap was reported as ZERO voices -- an unplayable instrument.
              The measurement did not say that. It said the genome is too
              expensive for 4x headroom, and one voice is still the answer.

The second is the one that would have been hardest to attribute later, because
`applyFaustTrim` does `jlimit(1, 32, faustDeclared_.capAt4x)` and silently
clamps a declared 0 up to 1. The wrong number was corrected downstream and
therefore never surfaced as a disagreement between the two sides.

**Verified rather than asserted.** `rtf / (v * 1.05) >= headroom` is exactly
`v <= rtf / (1.05 * headroom)`. At headroom 4.0 that reduces to
`floor(rtf / 4.2)` clamped to 1..32, which is `capForRtf`'s body character for
character -- checked against it at 500001 points over rtf 0..5000, identical at
every one. That check is what to rerun if either side moves again.

**Written as `1.05 * headroom`, not folded to 4.2**, in both copies. `capAt2x`
comes from the same lambda, and a folded constant would put two policies back
inside one file.

**STILL TWO IMPLEMENTATIONS, and deliberately.** `capForRtf`'s own
documentation claims "one formula, one place" and that is not true yet: it
hardcodes 4.2 and answers headroom 4.0 only, while the probe also reports 2x.
Collapsing them means giving `capForRtf` a headroom parameter, which touches
`FaustEngine.cpp` and sits behind the 120/120 null test. **The numbers agree
across all three copies now; the structure is one commit behind.** Left for a
session that can run the null test rather than done half-way.

**Not deployed.** `94e7471` is committed and not pushed or restarted. Artefacts
already on disk keep whatever cap they were minted with, and the plugin
re-derives from stored `rtf` rather than reading `capAt4x` back (`b1e9cfb`), so
installed instruments are unaffected either way.

---

# C25. MY OWN RECURRING ERROR — inferring a missing mechanism from a missing log line

Five times across two days I claimed a capability did not exist because I could
not see its output, and five times the codebase had already built it:

| claim | reality |
|---|---|
| acquisition is not instrumented | `acquisition_sentinel` logs on every path (C11) |
| the `show-login-modal` listener is unmounted in the plugin | `App.jsx:1450` keeps `NavigationBar` mounted precisely so it survives |
| the indicator cannot render a code-only `AUTH_REQUIRED` | it handles exactly that case |
| there is no live Faust render peak | `FaustEngine::peakSinceReset()` since §4.2, called from nowhere |
| the three surfaces offer different VI formats | one shared `InstrumentDownloadButton` with a dynamic format list |

Plus two tooling errors of the same shape: `find -maxdepth` after `-name`, which
BSD find ignores, and trusting devicectl's frozen container view over the
running system (C16).

The fix is procedural and cheap: **grep the source before asserting a thing does
not exist.** Absence from a log means the layer does not report; absence from
`grep` on one spelling means nothing at all. This entry exists because catching
it in oneself is the only version that generalises, and five times is a pattern
rather than a slip.

---

# C24. FINDING — branding ships, and the AU icon still does not appear

`3e67906` added `ICON_BIG`. Both bundles carry a compiled `AppIcon`, 24 entries
in each `Assets.car`, `CFBundleIconName` set in both plists.

**The app icon works: it is on the home screen.** **The AU icon does not: AUM
still draws its no-icon placeholder**, alongside two older Nemur builds that
never had icons. A device reboot changed nothing, which eliminates the
AudioComponent registry cache.

So the app-icon pipeline is fine and the AU's icon RESOLUTION is not. The
surviving hypothesis is the one flagged in `58629e9` and wrongly retired in C7:
`AudioComponentBundle = is.synth.plugin.internal` in the appex's
`NSExtensionAttributes`, naming a bundle JUCE only creates on Darwin
(`juce_PlistOptions.cpp:377` unguarded, `JUCEUtils.cmake:1459-1471`). C7 retired
it because the AU instantiated; instantiation and icon lookup are different code
paths, and the crash in C15 was in `requestIcon`. **Over-generalising that
retirement was a mistake.**

Not a `juce_add_plugin` keyword, so fixing it means a post-build plist edit plus
re-signing, or patching vendored JUCE. **Untested.** The experiment is one
throwaway run: strip the key from the built appex plist, re-sign, install, look.
Do that before building either fix.

Submission item, unrelated: `juceaide` puts an alpha channel back into every
generated slot even from an RGB source. Fully opaque (extrema 255..255), but
Apple has historically rejected any alpha on an app icon. Whether `actool`
strips it is untested.

---

# C23. FINDING — RETRACTED. The mechanism named here did not exist when it was written

**Retracted 2026-09-14.** The measurement below is real. The explanation is not,
and the original text is kept rather than deleted so the error stays visible.

Reported as "a bit distorted in the upper half" on `#DDJ9Y`. Its measurement:

    c3=1.1160  a4=0.6847  a5=0.2743  spread_db=12.2  trim=1.0000

What this finding claimed: `trim=1.0` means no attenuation was taken at all,
while the per-note gain equalises every key to the a4 reference, so notes near
a5 are boosted by 0.6847/0.2743 = **2.5x** with no headroom in hand, and any
moment where the true signal exceeds the short measured peak clips.

**There is no boost.** `FaustEngine.cpp` sets `compTarget` to
`jmin(1.0f, referencePeak / here)` — attenuate-only, and the comment above it
calls that the load-bearing rule. It landed in `249cb4a` on 2026-09-03. This
finding is newer than C22, which is dated 2026-09-09, so the boost it blames had
been gone for most of a week before the sentence was written. Under the real
code a5 takes gain 1.0 and delivers 0.2743, c3 takes 0.6135 and delivers 0.6847,
and nothing reaches full scale.

How it happened: the numbers were read off a log line, a ratio between two of
them was computed, and the code path was never opened. A ratio present in a
measurement is not evidence that anything applies it.

**The report is still unexplained.** One candidate, explicitly not a mechanism:
`estimatePeakAt` interpolates between table entries, so a pitch whose true peak
exceeds a low estimate takes a gain near 1.0, and `#DDJ9Y` has a raw c3 peak of
1.116. That would clip without any boost. Unverified. It stays a candidate until
someone opens the table and checks — which is the step this finding skipped.

The −9 dB Output default did NOT incidentally fix this. There was no headroom
problem of this kind to fix.

---

# C22. THE DEADLINE METER — built, and its first numbers are NOT yet interpretable

`dc5cb17` measures wall time in `processBlock` as a fraction of the block
period, which is the quantity `capForRtf` has always been a proxy for. Prompted
by Björn asking, of a reasoned claim that a 2-voice cap was right, "could we
know?"

First readings, AUM, 2026-09-09:

    #FJF8V  3-voice cap (RTF 15x)  peak 142%  22 OVER BUDGET of 4065 blocks
    #RPGW9  2-voice cap (RTF 12x)  peak 115%  12 OVER BUDGET of 4804 blocks
    both:   load 0%  of 42.67 ms

**AUM gives 2048-frame blocks.** With GarageBand's 256 and the standalone app's
64, block sizes across our surfaces span **64 to 2048, a 32x range**. A cap
derived once at 512 free-running was never going to fit that.

**But the question is not answered, and the design is why:**

- `load 0%` in both, because both were read with **0 voices** sounding. The
  rolling meter clears on read, so a poll between notes reads zero. The reading
  needed is with notes held.
- 142% of 42.67 ms is a 60 ms block. At RTF 15 a block should cost ~2.8 ms per
  voice, about 7%. So 60 ms is ~20x steady state and smells like a TRANSIENT --
  the first block after `suspendProcessing(false)`, a page fault, a thermal
  event. 22 in 4065 is 0.54%, consistent with a few one-offs.

**So the counters cannot yet separate "this genome is too heavy" from "loading
costs one late block", which is the whole question.** Next: log the first few
over-budget blocks with timestamps so they can be correlated against
`instrument_load` in the same file. Until then these numbers should not be used
to argue either way about the cap.

---

# C21. FINDING — "you have to tap a new instrument to activate it" is a 9-second audio suspension

Not autoload, which works. `#RPG29`, 2026-09-09:

    11:12:47.406  acquisition_queued 2ac089a6
    11:12:47.894  acquisition_autoload            <- 488 ms
    11:12:47.916  instrument_load ok=1 detail="loaded"   <- 22 ms later
    11:12:56.841  faust_measure rtf=11.7 measure_ms=8918.5   <- NINE SECONDS

`PluginProcessor.cpp:712` suspends processing and, in its own words,
"suspendProcessing(false) MOVES WITH THE MEASUREMENT" -- resuming earlier would
open the barrier while the measuring render still owns the engine. Sound
reasoning. But the comment was written against "the one to three seconds a
105-second measurement takes", and on an A15 under wasm3 it is **8.9 seconds**.

So: acquire, load in half a second, silence for nine seconds, user taps, the
measurement is now cached (`measure_ms=0.0`) and it works instantly. The tap
looks causal and is not.

**And the shell already knows.** `measuring_` is an atomic "read by the editor"
and the load outcome carries `measuring=true`. Nothing displays it. Cheapest
honest fix is to say so while it happens; not suspending at all is
architectural and risks the race the barrier exists to prevent.

---

# C20. THE BADGES SAID THINGS THEY DID NOT MEAN

Two, same failure: an internal value printed as a user-facing fact. `28f0c12`.

**`silent` is the BEST-behaved articulation, not a dead sound.** From
`faust-artefact.js:282` the classification is tail level after note-off:
`silent` < 0.001 stops cleanly, `quiet` < 0.032 decays quickly, else `drone`
sustains. The badge expanded only `drone` and printed the other two raw. Three
working instruments were reported as "not responding to notes" on 2026-09-09 for
this reason; their measurements show healthy output at all three probe pitches.

**"8 voices" was another machine's number.** `capAt4x` in the sidecar derives
from an rtf measured on the render server's V8, and `faustArtefact.js` is
explicit that "the V8/wasmtime ratio ranges 0.48-0.99 depending on the genome,
so there is no factor to convert it". Its own text summary says "Indicative
polyphony" and "Your machine will differ". The badge said `8 voices` for a genome
the phone capped at 2. Now `est. 8 voices`.

**Related, still open:** `faust-artefact.js:274-279` computes `capAt4x` with the
same power-of-two ladder this repo removed in `b1e9cfb`, so new artefacts still
carry ladder estimates and one policy has two disagreeing implementations.
Different repo, deployed service.

---

# C19. THE VOICE CAP — a ladder, and a persisted conclusion

Two commits, and the second is the interesting one.

**`b1e9cfb`: the ladder.** `capAt4x` stepped `v *= 2`, testing only 1, 2, 4, 8,
16, 32, so the cap was the largest TESTED value clearing the 4x bar rather than
the largest value clearing it. rtf 14.7 and 15.8 both permit 3 and got 2; 24.5
permits 5 and got 4. `rtf / (v * 1.05) >= 4.0` is exactly `v <= rtf / 4.2`, so
no loop was needed. `NEXT_SESSION_PROMPT.2026-09-01.md` had already specified
"a CONTINUOUS answer" and "derive the per-platform cap arithmetically" -- the
ladder contradicted the recorded design.

**`6e13c93`: and on the device the cap stayed at 2.** Because `capAt4x` was
PERSISTED. The measurement cache validates "this runtime, this rate, these exact
module bytes -- nothing the measurement depends on has changed", and a change to
the FORMULA depends on none of those. Every check passed and the cache kept
serving the previous build's conclusions.

**The generalisable mistake is storing a conclusion beside the observation that
produced it.** rtf is measured and worth trusting from disk; the cap is what
this build concludes from rtf, and storing a conclusion freezes it. One formula
now lives in `FaustEngine::capForRtf`, called by `measure()` and by every read
path.

Verified on the same cached measurement -- identical rtf, identical peaks,
`measure_ms=0.0` both times:

    10:30:52  faust_measure rtf=15.0 voices=2   (old build)
    10:47:27  faust_measure rtf=15.0 voices=3   (new build)

**The 4x bar is UNCHANGED and that is deliberate.** The rtf is free-running, and
`d3321de` measured a real block cadence costing 2.3-2.9x more, so 4x
free-running is nearer 1.4-1.7x in a host. Whether the bar is right is what C22
exists to answer.

---

# C18. STAGE C, AND A HARNESS THAT COULD NOT FAIL

**`0e8b93e`: concurrent log appends LOSE lines, not merely interleave them.**
C12 understated it. `File::appendText` builds a `FileOutputStream`, and
`FileOutputStream::openHandle` on POSIX opens `O_CREAT | O_RDWR` -- **no
O_APPEND** (`juce_SharedCode_posix.h:581`) -- then seeks to the end and writes at
the offset it just remembered. Two processes each seek to EOF and write THERE.

Fixed: one `write(2)` on an `O_APPEND` descriptor, `stage0-<host>-<wrapper>.log`
so Standalone and AUv3 no longer share a name, and `pid=` per line because the
filename separates hosts and formats but not instances. Also fixes a latent
desktop collision nobody had hit: our AU and VST3 in one host were one process
writing one filename. Does NOT fix `STAGE0_FINDINGS.md:444` -- GarageBand and
Logic both host AU in `AUHostingServiceXPC_arrow` -- but their lines now survive
and are attributable.

**`a7bd1bc`: and the null test reported IDENTICAL for code it had never
compiled.** The probe build FAILED (`build/wasm3` cannot compile faust_probe --
`main.cpp` wants `wasmtime.h` and that configuration lacks it) and the script ran
the months-old binary beside it, reporting PASS 120 FAIL 0. `find | head -1`
picked whichever of three probes find reached first, and nothing compared the
binary's age to the source.

Now: newest by mtime, overridable, **refuses** when `FaustEngine.cpp` is newer,
and prints which probe ran. Negative control run before trusting it. The
staleness guard is the same one `scripts/build.sh` already applies to a running
host -- applied to the harness rather than the product.

**A harness that cannot fail is worse than no harness, because it is believed.**

---

# C17. STAGE B VERIFIED — an instrument acquired in the AUv3 appears in the standalone app

Acquired inside AUM, then Refresh in the standalone app revealed it there.

The test is unambiguous because of what the standalone app is NOT doing: it has
acquired nothing today, and after Stage B it no longer reads its own private
`Documents`. Its own four instruments are invisible to it. So anything it lists
can only have come out of the shared container, written by a DIFFERENT PROCESS.

    19:12:03  wrapper=AUv3        library=appGroup   <- wrote it
    19:12:40  wrapper=Standalone  library=appGroup   <- reads it

This is the requirement §3a is actually about. Refresh was needed because
`scan()` runs on demand rather than watching the directory — a list rendered
before the download landed is stale, not empty.

**Not migrating was the right call.** Because both surfaces started at 0, a
single acquisition appearing in both proves SHARING. Had the four been migrated
first, the same observation would only have proved that a copy happened.

---

# C16. RETRACTION of C8 — devicectl's App Group container view is frozen at install time, and lies plausibly

C8 claimed `devicectl --domain-type appGroupDataContainer` can read the shared
container. **It cannot be trusted.** Across the entire session it reported the
same three entries, all dated 15:20 — the install moment — and never changed:
not when both processes began resolving `library=appGroup`, not after six
acquisitions, not while the instrument was visibly listed in the app.
`--username mobile` returns the identical frozen answer.

**A plausible-looking wrong answer is worse than an error**, and this is the
second-order version of the pattern already catalogued in C6 and C11: not a
layer that fails to report, but an OBSERVER that reports confidently about
something it is not seeing. It cost real damage:

- I asserted that instrument `70f23e40` existed in none of the three
  containers, and treated that as a contradiction with `acquisition_autoload`
  and `instrument_load ok=1`.
- I proposed instrumenting `fetchAndInstall` as the way to resolve it. The
  gap is real (C6) but the urgency was manufactured to explain a phantom.
- I trusted the tool over the running system, having spent the session
  insisting on the opposite.

**Ground truth for the shared container is the plugin itself**: the count and
the path the shell displays via the `getLibraryRoot` native function, plus
`library=` in the log. Those agreed with each other and with reality throughout.

**REOPENED as a result:** the Stage B trade-off C8 claimed to close. Moving the
library into the group container does lose Finder and Files.app visibility
(`FILE_SHARING_ENABLED` covers only the app's own `Documents`), and there is now
NO established way to read it from a Mac. `InstrumentLibrary.cpp`'s comment
currently cites the devicectl command as the answer; that citation is wrong.

---

# C15. FINDING — AUM died in AudioComponentCopyIcon after a reinstall under a live host

    exception   EXC_BAD_ACCESS / SIGSEGV
    subtype     KERN_INVALID_ADDRESS at 0x3eebc86a03a8eeec
                (possible pointer authentication failure)
    thread 0    com.apple.main-thread
      0  AudioToolboxCore  AudioComponentMgr_NSExtension::requestIcon(...)
      1  AudioToolboxCore  AudioComponentCopyIcon
      2  AUM               ...
      6  CoreFoundation    __CFNOTIFICATIONCENTER_IS_CALLING_OUT_TO_AN_OBSERVER__
     10  Foundation        -[NSNotificationCenter postNotificationName:...]

Report `AUM-2026-09-08-191128.ips`, two seconds after `acquisition_autoload`.
Not our code, and nothing to do with the library path — but caused by our
procedure: a new build was installed at 19:00 while AUM had been running since
18:37, so it held `AudioComponent` references into a bundle that was replaced
underneath it, and asked the system for the icon of a stale component on the
next registration-changed notification.

The appex log stopping at `measuring=true` was a CONSEQUENCE, not a cause: the
extension was torn down with its host before the measurement could log.

**Rule:** do not install under a live AU host on iOS. Quit the host first.
`scripts/build.sh` already guards the desktop equivalent by warning when a
running host predates the installed binary; iOS has no such check, and adding
one is cheap.

Also worth keeping: the PAC-failure signature reads like a use-after-free in
our own code, and the first instinct was to suspect the change made minutes
earlier. The backtrace cost one command and pointed somewhere else entirely.

---

# C14. RESOLVED — C11's cause was auth, and the whole acquisition chain works inside an app extension

Signed in inside the AUv3's site view, the same button worked. Full trace from
the appex log:

    18:40:28.393  acquisition_sentinel synthis://acquire artifact=a3abb066-...
                    name="Virtual Instrument #MBDZW" ticket=present
    18:40:28.394  acquisition_queued a3abb066-...
    18:40:28.561  acquisition_autoload id=a3abb066-...
    18:40:28.566  instrument_load kind=faust ok=1 dsp="faustgenome_mtt0lnag_4"
                    voices=1 gate=1 detail="loaded"
    18:41:02.081  instrument_load kind=faust ok=1 dsp="faustgenome_mtt0lnag_4"
                    voices=4 gate=1 detail="loaded"

168 ms from sentinel to autoload. The container holds the full bundle:
`instrument.json`, `instrument.wasm`, `measured.local.json`, `metadata.json`,
`synthis.json`.

**What this establishes, all of it inside an extension process:** ticket
minting, sentinel navigation, the intercept, `fetchAndInstall`, directory
creation in an appex container, the sidecar merge, wasm3 compilation, voice
allocation, and autoload. None of it needed changing for AUv3.

**The ticket redaction works live.** `ticket=present`, not the value. That is
the property the intercept's ordering was built for — logged before the origin
check and with the ticket stripped — and it is now confirmed on a device rather
than argued from the code.

**Noted, not chased:** the reload at 18:41:02 reports `voices=4` where the
first load reported `voices=1`. Unexplained.

## What remains, and it is much smaller than C11 implied

Acquisition is not broken. The **signed-out path produces no visible
feedback**. `acquireIntoPlugin` toasts on the 401 from the download-recording
POST, and no toast was seen in AUM's tall view, so clipping is unlikely.

One hypothesis excluded, and worth recording because the code had already
guarded it: I expected the `show-login-modal` listener to be unmounted in the
plugin, since the plugin suppresses the web app's chrome. It is not.
`App.jsx:1450` is `if (hostDrawsChrome()) return <NavigationBar hideNav />;`
with a comment stating that NavigationBar still mounts without its `<nav>`
SO THAT those listeners survive. Someone anticipated this exact failure.
`RegisterPromptModal`, `useRequireRegistered` and the event wiring all exist.

So the fault is inside `SoundCard`'s handler, or in toast visibility. The
desktop signed-out test (C11) remains the discriminator and still costs nothing.
Priority drops: this is legibility on an error path, not a broken feature.

## Stage B's acceptance improves because of this

The AUv3 is now signed in, and its session persists in the appex's own
WKWebView store. So after Stage B the test can be the **product requirement
itself** rather than a proxy: acquire one instrument in the AUv3, and it must
appear in the standalone app with no second sign-in and no acquisition of its
own. Planting a directory with `devicectl device copy to` stays as the fallback
if acquisition regresses.

Revised container counts for that test: the app holds 4 in its private
`Documents`, the extension now holds 1 in its own. Both are private, so both
surfaces should read 0 immediately after Stage B, then 1 after a single
acquisition. Nothing is deleted either way.

---

# C13. FINDING — the view size is host-specific, and the shell already shows the library root

AUM gives the AUv3 a **tall, near-full-width view**. C4's `736x242` is a
GarageBand constraint, not an AUv3 one. Both matter, but they are different
problems: GarageBand needs a letterbox layout, AUM does not.

Two things visible in AUM's shell surface that are worth more than the null
result they came with:

    /var/mobile/Containers/Data/PluginKitPlugin/24971780-.../Documents/Instruments
    Note sent (a4, 0.8 s) — 3697 audio blocks rendered so far.

**`PluginKitPlugin`, not `Containers/Data/Application`.** That is the extension
container, so this is direct confirmation of the MECHANISM behind C1 rather
than its symptom: `InstrumentLibrary::root()` is resolving to the appex's own
private `Documents`.

**The shell already displays that path** through the `getLibraryRoot` native
function (`PluginEditor.cpp:406`). So Stage B has a free on-device acceptance
check: the displayed path must change to the shared container. No devicectl
needed, and the check is visible to a user rather than only to a debugger.

And the audio path is live in an extension — 3697 blocks rendered, silence
because nothing is loaded, but `prepareToPlay` and `processBlock` are running.

---

# C12. C3 ESCALATED — the appex log is shared across HOSTS, so the collision is live now

C3 said the filename collision was harmless today because the containers
differ, and would bite once the log moved to the shared container. That
understated it. The GarageBand session (16:56–17:03) and the AUM session
(18:37–18:39) are in the **same** `stage0-Synth.log`, because the container is
per-extension, not per-host.

Each AUv3 instance runs in its own process. Two instances in two hosts, or two
in one host, are already two processes appending to one file with no locking.
The interleaving risk is **present now**, not after Stage B.

---

# C11. CORRECTION to C6 — the attempt IS instrumented, and the fault is not in the plugin

C6 says "the log cannot say why". That over-corrected, and the sequence is worth
recording as it happened:

1. I inferred from the absence of an acquisition log line that nothing was
   handed over. Right in substance, but I had not established the premise.
2. I retracted it on finding zero `logEvent` calls in `ArtifactAcquisition.cpp`
   and `AcquisitionRunner.cpp`, and on §3a saying outcomes are not logged.
3. Reading `PluginEditor.cpp:923-952` settled it. The retraction was wrong in
   SCOPE. The *outcome* is uninstrumented; the *attempt* is not.

The sentinel branch logs on every path — `acquisition_sentinel` with the ticket
redacted, then `acquisition_queued`, `acquisition_already_running`, or
`acquisition_sentinel_malformed`. §3a's "acquisition outcomes are still not
logged" was precise; I collapsed "outcome" into "attempt".

**So the silence is positive evidence.** Across two hosts and three site
sessions there is no `acquisition_sentinel`, no `page_about_to_load synthis://`
and no `navigation_blocked`. Nothing was ever offered to the intercept.

The plugin side is correct and can be excluded:

- `site_->interceptsSentinels = true` (`PluginEditor.cpp:582`) — the site
  surface does intercept.
- The intercept runs BEFORE the `page_about_to_load` log line and before the
  origin check, both deliberately: an unconditional log would have written the
  ticket to disk in plaintext, and the origin check would have handed the
  sentinel to `launchInDefaultBrowser`.
- `InstrumentLibrary::root()` is passed at the intercept site, so Stage B flows
  through with no change here.

The fault is web-app side. `SoundCard.jsx` — the component on screen — does use
`useObtainInstrument` → `watchAndAcquire` → `acquireIntoPlugin`, and that
function toasts on the signed-out 401 from the download-recording POST. So
"nothing at all" means either the purple piano is not the obtain affordance
(`TakeoutView.jsx:1005` is another call site, and the package icon in the top
nav looks like Takeout), or the handler dies before reaching any toast.

**NOT established which.** Pre-registered next test, which needs no phone: press
the same button in the DESKTOP plugin while signed out.

- Desktop also silent → the signed-out path is broken everywhere and this is
  not an AUv3 issue at all. That is the bigger finding, because it would mean
  the amber state the `FeedRenderJobIndicator` work was meant to produce is
  unreachable.
- Desktop toasts → AUv3-specific, and the fixed-position toast or the host
  viewport becomes the suspect.

---

# C1. FINDING — the sandbox boundary is real, and now has a before-state

The AUv3 instantiates in GarageBand, renders our UI, and receives MIDI
(`midi_in events=74 on=22 off=22`, notes 66–80). It reports:

    Instruments   0 instruments
    No instruments in the local library yet.

against four instruments in the container app — three Faust
(`5253ba00…`, `6d5f3028…`, `e140008d…`) and one sfz (`cabd7ca5…`), each with
its `measured.local.json` sidecar.

This is the Stage B before-state and it was captured deliberately, before
touching `InstrumentLibrary::root()`. After the change, an empty list would
otherwise be ambiguous between "the sandbox" and "the group ID does not match".

---

# C2. FINDING — the WKWebView session does not cross the process boundary

`persist` markers from the PROBE lines:

| | shell origin | site origin |
|---|---|---|
| standalone app | `SURVIVED age=98075s` | — |
| AUv3 extension | `created` | `created` |

The extension's web storage is fresh for both origins while the app's has
survived 27 hours. `sessionStorage` likewise `fresh`.

**CORRECTION to `NEXT_SESSION_PROMPT_IOS.md` §3a.** It says of sign-in: *"The
App Group container is where that lands."* That is wrong. `WKWebsiteDataStore`
appears **nowhere** in JUCE (`grep -rn WKWebsiteDataStore modules/` returns
nothing), so the webview uses `defaultDataStore`, which is container-scoped,
and Apple ships no API to point a `WKWebsiteDataStore` at an App Group. The
library files can move to the shared container. The cookies cannot.

Consequence: **Stage D is a ticket-passing problem, not a container problem.**
What crosses the boundary is the HMAC ticket as data, not the session.

---

# C3. CORRECTION — §3a's log claim is false on iOS, and the filename is worse

§3a says: *"The per-host log naming already keys on `hostApplicationPath`, so
this should work."*

`hostApplicationPath` on Apple platforms is `_NSGetExecutablePath()`
(`juce_Files_mac.mm:238-245`) — the **current process's** executable. On desktop
the plugin lives in the host's process, so that resolves to GarageBand or
Reaper and the naming works. An AUv3 runs in its **own** process, so it
resolves to the appex's own executable.

Measured, and sharper than predicted:

    stage0-Synth.log                                        (both surfaces)
    processor_constructed host=Synth wrapper=Standalone     (app)
    processor_constructed host=Synth wrapper=AUv3           (extension)

`getFileNameWithoutExtension()` strips `.is` from `Synth.is` as if it were a
file extension, so the name collapses to `Synth`. The naming therefore fails
twice: it identifies no host, and it mangles the product.

Both surfaces produce the **same filename**. Harmless today because the
containers differ. A two-writer, no-locking collision the moment the log moves
into the shared container — which is a natural thing to want, since the group
container is the only place both processes can write.

**The fix is small, and smaller than first proposed.** `wrapper=` is already in
the log *content*; only the filename collides. Put the wrapper type in the
filename and drop the claim of host discrimination on iOS. No new plumbing.

---

# C4. FINDING — GarageBand gives the AUv3 a 736x242 view

Resize sequence: `320x400` → `428x926` → `926x428` → `242x736` → settles at
**`736x242`**.

A 3:1 letterbox. The synth.is web UI is being asked to lay itself out in it.
This is a hard constraint on the unified-chrome work (§3), not a detail, and it
is a live candidate cause for C6.

---

# C5. FINDING — GarageBand runs at 44100 / 256 frames. The voice caps were measured at 512.

    prepareToPlay sampleRate=44100 blockSize=512
    prepareToPlay sampleRate=44100 blockSize=256

Every Gate 0 voice cap — 1–2 voices for the slowest ranks, up to 8 for the
fastest, at a >=4x RTF margin — was established at **512 frames**. GarageBand
settles at 256. Smaller blocks mean more per-block overhead per unit of useful
work, so the caps are likely optimistic in the host that actually matters.

**NOT established:** the size of the effect. Do not assume it halves, and do not
tune anything until it is measured at 256 under a pre-registered rule. This is
a re-measurement task, not a known regression.

Found only by loading the plugin in a real host. `faust-probe` was never going
to surface it.

---

# C6. NOT A FINDING — acquisition produced no visible result, and the log cannot say why

Observed: on the site surface inside the AUv3, pressing the acquire affordance
did nothing visible. No instrument appeared, no error was noticed.

**CORRECTION, mine, made and retracted inside the same session.** I read the
absence of any acquisition line in the appex log as evidence that nothing was
handed over. That inference is void. Verified from disk:

    logEvent calls in ArtifactAcquisition.cpp     0
    logEvent calls in AcquisitionRunner.cpp       0
    logEvent calls in PluginEditor.cpp           21
    logEvent calls in PluginProcessor.cpp        32

The acquisition path is entirely uninstrumented — §3a already said so
("acquisition outcomes are still not logged"). Absence of a log line from a
layer that emits none is not evidence of anything. This is the same failure the
`--clicks` reporting and the acquisition card were caught doing: asserting an
outcome the layer does not observe. It is recorded here because catching it in
oneself is the only version that generalises.

Three candidate causes, **none eliminated**:

1. Signed out (proven by C2) — the web app may correctly refuse and show the
   amber "Sign in to add this to the plugin", which could go unnoticed in a
   736x242 strip.
2. The `synthis://acquire` interception may not be wired on the site surface in
   an appex. The namespace is referenced from `PluginEditor.cpp`, so the path
   exists; where it is triggered was not located this session.
3. C4's viewport putting the affordance out of reach.

**Prerequisite before diagnosis: instrument the acquisition path.** Diagnosing
it now would mean three live hypotheses, a known-broken precondition (auth) and
a destination about to change in Stage B.

---

# C7. RESOLVED — AudioComponentBundle names a nonexistent bundle on iOS, and it does not matter

Flagged unverified in `58629e9`: `juce_PlistOptions.cpp:377` emits
`AudioComponentBundle = is.synth.plugin.internal` unguarded, but JUCE creates
the `_Framework` target only on Darwin (`JUCEUtils.cmake:1459-1471`), so on iOS
that key names a bundle that does not exist. It was named as the first suspect
for a listed-but-uninstantiable AU.

Retired. The appex instantiated, rendered and received MIDI in GarageBand. The
key is inert on iOS.

---

# C8. FINDING — the Mac can read the App Group container

    xcrun devicectl device info files --device <udid> \
      --domain-type appGroupDataContainer --domain-identifier group.is.synth.plugin

`appGroupDataContainer` is a first-class domain type alongside
`appDataContainer`. This **resolves a Stage B trade-off that was open**:
`UIFileSharingEnabled` exposes only the app's own `Documents`, so moving the
library into the shared container loses Finder and Files.app visibility — which
is exactly what `InstrumentLibrary.cpp`'s comment chose `Documents` for. It does
**not** lose inspectability, because devicectl reaches the group container
directly. Stage B costs less than estimated.

Mechanical note: `devicectl device copy from --destination` needs a full **file**
path. Given a directory it fails, and with stderr suppressed it fails silently.

---

# C9. FINDING — the iOS toolchain had no Apple ID; every prior device build rode a cached wildcard profile

    defaults read com.apple.dt.Xcode IDEProvisioningTeamByIdentifier      {}
    defaults read com.apple.dt.Xcode DVTDeveloperAccountManagerAppleIDLists  {} {}

Five profiles cached on disk, all team ATA8MQ4L7K, for other apps plus one
wildcard `ATA8MQ4L7K.*` expiring **2027-09-06**. None for `is.synth.plugin`.

The first App Group build failed with eight errors. Seven described the
symptom — the wildcard profile lacking `application-groups`. One, first and
quiet, named the cause:

    error: No Accounts: Add a new account in Accounts settings.

`-allowProvisioningUpdates` had nothing to authenticate with, so it fell back to
the cached wildcard. **The condition was latent for every prior iOS build**,
because a wildcard profile is sufficient right up to the first entitlement. The
flag in `probe/gate0-ios/run-on-device.sh` had never had to do anything.

Fixed by signing in to Xcode-beta. Durable fix outstanding: an App Store Connect
API key already exists at `~/Downloads/dnl/AuthKey_95F348PRU8.p8` (key ID
`95F348PRU8`); it needs the Issuer ID from the Keys page, and either an explicit
`-authenticationKeyPath` or a move to `~/.appstoreconnect/private_keys/`. Worth
doing before 2027-09-06, when the wildcard expires and the failure recurs
looking like an entitlement problem.

---

# C10. LATENT TRAP — Signing.cmake would destroy iOS provisioning if a sign identity were ever set

`_kromosynth_sign` does `if(NOT APPLE) return()` and then returns early only
because `KROMOSYNTH_SIGN_IDENTITY` is empty. There is no `if(IOS) return()`.

So iOS is safe **by accident**. Set a sign identity on an iOS build and the
post-build step would re-sign the `.app` with `--options runtime` and a
Developer ID identity, replacing the automatic signature and its provisioning.
Not fixed; recorded so the next person does not find it the hard way.

---

# What Stage B now has

- a measured before-state (C1): 0 in the extension, 4 in the app
- a confirmed-live shared container (C0)
- a way to read that container from the Mac (C8)
- `File::getContainerForSecurityApplicationGroupIdentifier` in plain
  `juce_core` — no `.mm` translation unit needed, contrary to what §3a implies
- the failure mode to design against: that function returns a **non-existent**
  `File`, `scan()` returns early on `! base.isDirectory()`, and the result is an
  empty list indistinguishable from a user who has downloaded nothing. Hence
  `rootOrigin()` reported once at construction.

# Open, in order

1. **The `load@cap` measurement** (C27 answered the transient question; this is
   the cap question). `loadByVoices_[0..32]` with a block count beside each
   entry, so one note, two, three gives the intercept and the per-voice slope
   directly. **Needs a bar registered before the run** — and note we are no
   longer naive, since C27 shows the shape. The bar is for the next
   measurement, not retrofitted to this one.
2. **Re-read the footer after `704bd1e`.** It should now say roughly 5.33 ms in
   AUM, not 42.67. One glance, no run needed. If it still says 42.67 the fix
   did not reach the device.
3. **The icon is now unexplained** (C28). The `AudioComponentBundle` strip was
   run on device and changed nothing, so C24's last hypothesis is eliminated and
   there is no shortlist left. The one loose thread is that **no reboot has been
   tried since the strip** — C24's reboot ruled out the registry cache as a
   cause of absence, which is not the same as showing a corrected plist gets
   picked up without one. Cheapest next step, and it needs eyes on AUM.
4. **The unified chrome and the merged library.** Designed and committed as
   `daw-plugin/WP_UNIFIED_CHROME.md`. §7.2 is the important part and outranks the
   chrome it sits in: instruments bought on one machine are invisible on
   another. **NOT blocked, correcting an earlier overstatement**: §7.2 already
   re-download exemption keys on the takeout entry, so the merged list must
   re-download the same entry and never re-render. **OQ7 is still open, unchanged
   since 2026-09-07, and it is one decision: entry or sound.** Highest leverage
   item on this list, and not code. §11's acceptance list already includes the
   merged-library criterion, so building the chrome first means shipping against
   a list known to be incomplete.
5. **The envelope panel** (`WP_UNIFIED_CHROME.md` §9). Parameters already exist
   as host parameters; the gap is presentational. Release is the polyphony
   lever, so it belongs beside the voice count.
6. **The per-note gain clipping decision** (C23). Clamp the boost, or take trim
   headroom on a wide spread. A real choice, not a number to pick.
7. **Finish consolidating `capForRtf`** (C26). All three copies now agree
   numerically; there are still two of them. Giving `capForRtf` a headroom
   parameter touches `FaustEngine.cpp` and so sits behind the 120/120 null test.
8. **Deploy `94e7471`** (C26), whenever the hub is next restarted. Committed in
   `kromosynth-render`, not pushed. Nothing breaks until it ships; new artefacts
   keep being minted with ladder caps until it does.
9. **The desktop signed-out test** (C11, C14). Still costs nothing.
10. **Stage D** — the ticket across the process boundary (C2).
11. **A reliable way to read the group container from a Mac** (C16).
12. **Migration**, if wanted: the app's four and the extension's several are
    still in their private containers, untouched and unlisted.

# Closed 2026-09-09, after the prompt was rotated

- **C22's counters are now interpretable** (`844223d`). Item 1 above is the run,
  not the build.
- **"Say measuring while measuring"** (`2f40a20`, C21). Both surfaces — and both
  turned out to be making a positive false claim rather than staying silent:
  `SiteBar` said "Installed - ready in Library" and the shell said "Loaded",
  throughout the eight to nine seconds the audio path is suspended.
- **The render server's ladder** (`94e7471`, C26), and the probe's, which was not
  on this list because nobody knew it existed (`884e0c3`).
