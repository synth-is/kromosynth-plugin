# Stage 0 findings

---

# TASK 1 VERDICT — the webview assumption holds. Six things change the WP.

**Environment:** macOS 26.3.1, Apple clang 21.0.0, SDK 26.5, JUCE 9.0.1,
AU + VST3, ad-hoc signed Debug build.

**Hosts tested:** GarageBand (AU, out-of-process via `AUHostingServiceXPC`),
Reaper 7.79 (AU **and** VST3, in-process), Live 12 Beta (AU **and** VST3),
plus Safari as a rendering control.

**Not tested:** Logic, Bitwig.

## The WP §7 gate

| gate question | verdict | |
|---|---|---|
| Does it load? | **YES** everywhere | ~1.2 s; React mounts (`rootChildren: 1`) |
| Does it stay loaded? | **YES** | after fixing a use-after-free of our own — §A4 |
| Does it resize? | **VST3 yes / AU no** | occlusion, mitigable — §A3.1c |
| Does the host steal keyboard focus? | **NO** | space types a space; transport stays put |
| Does routing work outside a browser tab? | **YES** | `BrowserRouter` against the remote origin |

Plus, beyond the written gate: survives close→reopen and host quit→relaunch,
runs multiple instances with independent pages, and passes `auval` cleanly at
every sample rate from 11 025 to 192 000 Hz.

**The riskiest assumption in the WP survives.** A WKWebView pointed at the
deployed origin loads and renders the real application inside a sandboxed,
hardened Apple host that runs plugins out-of-process.

---

## Six things that change the WP

**1. Signing is load-blocking on Apple hosts, not a Stage 1 formality (§A2).**
GarageBand refused the ad-hoc signed plugin until the user granted a "Lower
Security Settings" exception. §7 schedules signing for Stage 1; for Apple hosts
it belongs earlier, and no real user can ever be shown that dialog. Non-Apple
hosts are unaffected, so a clean Reaper result does not generalise here.

**2. Audition audio does not go through the plugin (§C).**
Probe-confirmed: the page has its own `AudioContext`, following the *system
default output device* rather than the host. §5's "auditioning inside the plugin
is interaction and free" assumes audition behaves like a plugin. It does not —
it exits the system output, unrouted and unrecordable. **This is a design
decision the WP has not made**, and it is the most consequential item here.

**3. Storage is per-host-application (§A3.1d).**
Reaper's VST3 wrote a key and Reaper's AU read it; GarageBand never saw it. So
the container is scoped to the **host application**, not the plugin or format.
Sign-in and entitlement are **per-DAW** — three DAWs, three logins, and
revoking in one leaves the others live. Argues for the token living in the
plugin's own storage, which is also the RFC 8252 shape §5 already mandates.

**4. The app cold-starts on every window open (§A3.3).**
New page, fresh `sessionStorage`, ~1.2 s reload, all in-memory UI state lost.
`withKeepPageLoadedWhenBrowserIsHidden()` does not help because hosts *destroy*
rather than hide. §4 assumes tab-like persistence; the DAW-native pattern is
open → search → close → work → reopen, and the search is gone each time.
Mitigable by persisting UI state to `localStorage` — but that is plugin-specific
work the web app does not do today.

**5. AU editor resize is broken in two distinct ways (§A3.1c).**
Every VST3 resizes and reflows. Every AU fails. Two separate defects:

| path | JUCE component resizes | WKWebView follows |
|---|---|---|
| JUCE's own grip | ✅ | ✅ reflows |
| Host-driven (Reaper AU) | ✅ | ❌ crops |

The dead corners in Live and GarageBand are **occlusion** — a native `NSView`
composites above JUCE's `ResizableCornerComponent` and buries the only resize
affordance an AU host offers. Proven by A/B: a plain `juce::Component` in the
same editor resizes fine, and insetting the browser 24 px restores it. The
host-driven crop is a *different* bug that insetting does not fix. AU is the
only format Logic and GarageBand accept, so §4's "one UI codebase" currently
means **"a UI that lays out correctly in VST3 hosts."**

**6. The embedded-webview User-Agent is detectable (§B3).**
No `Safari`/`Version` token. OAuth providers block embedded webviews on exactly
this signal. §5 already mandates RFC 8252 — this is empirical confirmation that
the design is **forced, not merely preferred**.

---

## Four things that turned out fine

- **`localStorage` works and persists** across editor close/reopen and host
  restart (§B2). The `kromosynth-live` precedent suggested it would fail. It
  did not.
- **`crossOriginIsolated: false` is a non-finding** (§B1). Real WKWebView
  behaviour, but three call sites depend on it and two actively prefer it
  false. No action.
- **Multiple instances work** (§A3.2), with independent pages.
- **Out-of-process AU hosting is transparent** (§A1). The plugin does not run
  in the DAW's address space at all, and nothing broke because of it.

## Three development costs discovered

- **No Web Inspector** (§B6). JUCE 9 never sets `isInspectable`, so no devtools
  in the plugin webview. Print-debugging via `evaluateJavascript` works, but
  Stage 1's bridge will feel this.
- **Async lifetime hazards are easy to hit** (§A4). Four lines of
  instrumentation produced a host-level "may cause the system to become
  unstable" modal. `SafePointer`-or-equivalent on every async callback should
  be a stated rule in the bridge design.
- **Non-ASCII in C++ string literals is mangled.** Any C++-side user-facing
  text needs `juce::CharPointer_UTF8` or must stay ASCII.

---

## Recommendation on Task 2

**The gate passes; Task 2 is unblocked. But do two things first, and decide a
third.**

**Do first — generate a suitable VI.** Every one of the 17 artefacts on disk is
XY-morph or single-velocity-layer with zero round-robins (§D). Task 2's stated
question — *"whether round-robins and velocity layers behaved"* — **cannot be
answered with existing material.** A fresh render with `velocityLayers > 1` and
`roundRobins > 1` is a prerequisite, not part of the task.

**Do first — fix the audio device.** The test machine is running at **24 kHz**
(§A3.1e). Judging sfizz playback, round-robins and velocity layers through a
12 kHz Nyquist would waste the session.

**Decide, do not test — §C's audition routing.** This changes what Task 2 is
*for*. If audition stays in the webview, the sampler is a playback engine for
downloaded artefacts. If audition moves to the plugin (§C option 3 — "the
webview browses, the plugin plays"), the sampler becomes the primary audition
path, load-bearing far earlier than Stage 1 assumes, with different latency and
voice-count requirements. **"Does sfizz work" has two different answers
depending on which.** Worth settling before embedding a library.

**Also worth doing, cheaply:** Logic. It is the one untested host that matters,
it is AU-only (so it inherits finding 5 at its worst), and it shares
`AUHostingServiceXPC` with GarageBand — so it would settle whether the storage
boundary in finding 3 is the DAW or the hosting process.

---


Status: **Task 1 complete.** GarageBand, Reaper 7.79 and Live 12 Beta tested
across AU and VST3, with Safari as control. Logic and Bitwig outstanding.
Task 2 not started.

Corresponds to `WP_SYNTHIS_PLUGIN.md` §7, Stage 0. The gate there reads:

> the webview loads, stays loaded, resizes, and does not steal keyboard focus
> in Logic, Live, Reaper and Bitwig.

**That gate is insufficient, and this document proposes widening it.** A
webview can pass all four of those and still make the one-UI-codebase premise
in §4 false, because the web app depends on browser capabilities that a
WKWebView inside a plugin may not grant. Sections B and C below are the added
questions.

Report failures as findings. A webview that crashes Live is the most useful
result this stage can produce.

---

## 0. RESOLVED — the JUCE licence (2026-08-25)

WP §0 assumes JUCE's open-source option is AGPLv3 and asks that it be confirmed
"before the first public build rather than after". Confirmed, from the module
header on disk rather than from juce.com:

```
-- JUCE 9.0.1 — module declares license: AGPLv3/Commercial
-- JUCE licence assertion passed: module header mentions AGPL
```

`cmake/JuceLicenceCheck.cmake` reads `modules/juce_core/juce_core.h` — the text
that ships inside the translation units, which is what governs — not
`LICENSE.md` at the repo root and not the website. It is a configure-time
`FATAL_ERROR`, so it re-runs on every fresh configure and will catch a licence
change at a future version bump.

**Two corrections to the WP that follow from this:**

1. **JUCE 8 is no longer current.** 9.0.0 shipped 21 July 2026, 9.0.1 shortly
   after; 8.0.15 appears to close the 8.x line. §0's framing should say 9.x.
2. **9.x is the right choice here, not 8.x.** JUCE's CHANGE_LIST entries for
   "Added macOS/iOS 26 support" and "Fixed some LLVM 21 compiler warnings" are
   on the 9 line. This machine is macOS 26.3.1 / SDK 26.5 / Apple clang 21.0.0
   — exactly that combination. And since Task 1 *is* a webview test, running it
   on an EOL branch would risk reporting a WKWebView failure that 9 already
   fixed, then changing the WP on a false negative.

This also keeps §9's deferred libfaust question in its current state rather
than reopening it: AGPLv3 + GPL remains the pairing to reason about if tier 2
ever ships.

---

## 0b. RESOLVED — build and AU validation (2026-08-25)

Built clean on JUCE 9.0.1, Apple clang 21.0.0, macOS SDK 26.5. **No compile
errors.** Two predictions from the previous session were wrong and are recorded
as such:

- `juce_audio_processors_headless` did **not** break the include. JUCE 9 split
  the module but `<juce_audio_processors/juce_audio_processors.h>` resolves
  transitively, as hoped.
- `WebBrowserComponent::evaluateJavascript` and `EvaluationResult` compiled
  unchanged. The JUCE 8 API shape survives into 9. This had been flagged as
  unverifiable because no JUCE headers existed on the machine at the time.

`auval -v aumu Krm0 Sthi` — **AU VALIDATION SUCCEEDED**, full pass: required,
recommended, optional and special properties; custom Cocoa UI present
(`JUCE_AUCocoaViewClass_...`); render tests at 11025 / 22050 / 44100 / 48000 /
96000 / 192000 Hz; MIDI test passed. Logging confirmed working
(`host=auval wrapper=AU`).

### Three things from the auval output to carry forward

**1. Cold open time is 1532 ms; warm is 3 ms.**
This is a Debug build with no LTO, so dyld is loading a large unoptimised
binary, and the first-open cost includes that. It is very likely fine. But
hosts apply timeouts during plugin scanning, and 1.5 s is not nothing —
GarageBand and Logic in particular will drop a plugin that is slow to
instantiate during a scan. **Re-measure with `--preset release`** before
concluding anything, and note the number in the matrix if any host fails to
scan.

**2. `WARNING: Source AU supports multi-channel output but does not provide a
channel layout`.**
Reported channel capabilities came back `[0, -1]` — no inputs, *any* number of
outputs — so auval probed 0-1 through 0-8 and all passed. The cause is that
`PluginProcessor` declares a stereo output bus but never implements
`isBusesLayoutSupported`, so JUCE advertises an open-ended capability.

Not fatal, and not worth fixing in the disposable processor. But it is exactly
the kind of thing **Apple hosts are fussy about**, so if GarageBand or Logic
refuses the plugin or instantiates it with a strange channel count, this
warning is the first suspect. Recorded now so it is not re-diagnosed from
scratch later.

**3. Stale 2023 plugins are still installed, and one of them is a webview
plugin.**

```
~/Library/Audio/Plug-Ins/Components/  KRME.component   (Dec  7 2023)
                                      SRVB.component   (Dec  4 2023)
~/Library/Audio/Plug-Ins/VST3/        KRME.vst3        (Dec 21 2023)
                                      SRVB.vst3        (Dec 20 2023)
```

These are the JUCE 7 / Elementary builds. **KRME is itself a choc-WKWebView
plugin** (see "Prior art" below). Two consequences:

- *Confounder.* A 2.5-year-old plugin that crashes during a host scan can abort
  or corrupt the whole scan, which would look like a Synth.is failure. Move
  both aside before running the matrix:
  `mkdir -p ~/Desktop/plugin-quarantine && mv ~/Library/Audio/Plug-Ins/{Components,VST3}/{KRME,SRVB}.* ~/Desktop/plugin-quarantine/`
- *Opportunity.* Once the Synth.is results are in, putting KRME back and seeing
  whether **it** still loads is a free comparison between choc's WKWebView
  wrapping and JUCE 9's, on the same machine and host. Worth ten minutes, but
  only after the clean run.

`scripts/check-deps.sh` only checked for stale `Synth.is.*` and
`KromosynthPlugin.*` builds and missed these entirely — a gap in the script,
not in the reasoning.

---

## Prior art the WP does not account for

`/Users/bjornpjo/Developer/apps/kromosynth-evoruns-plugin` (**KRME**) is an
existing VST3/AU built on SRVB. `native/WebViewEditor.cpp` creates a
`choc::ui::WebView`, attaches it via `viewContainer.setView(webView->getViewHandle())`
on macOS, binds `__postNativeMessage__`, and in dev mode navigates to
`http://localhost:5173`. There is a built artefact at
`native/build/scripted/KRME_artefacts/Release/VST3/KRME.vst3` dated
**2023-12-21**.

So "can a WKWebView live inside an AU/VST3 at all" is **not** the open
question — it shipped, in this project, two and a half years ago. What is
open:

- choc wraps WKWebView differently from JUCE 8's `WebBrowserComponent`; the
  Dec-2023 result does not transfer automatically.
- There is no evidence KRME was ever tested across the four-host matrix.
- KRME pointed at a **localhost dev server** or bundled assets. Stage 0 points
  at a **remote production origin**, which is a different security context.

WP §0 names `agentic-synth` as the shell reference and does not mention KRME.
Your own prior art is closer to the target than the MIT reference is.

---

## A. The gate as written

Per host. Record the outcome, not an impression.

| | GarageBand | Live 12 Beta | Reaper | Bitwig | Logic |
|---|---|---|---|---|---|
| AU validates (`auval`) | ✅ **PASS** (host-independent) | n/a | n/a | n/a | ✅ same binary |
| Plugin scan succeeds | ✅ † | | | | |
| Editor opens, page renders | ✅ ~1.2 s | | | | |
| Survives close → reopen (page state intact?) | ⚠️ opens, but **full reload** — §A3.3 | | | | |
| Survives host project save → reload | ✅ 2 instances restored | | | ⚠️ demo can't save | |
| Resize: page reflows, no clipping | ❌ **cannot resize** — §A3.1 | | | | |
| Typing into a web input reaches the page | ✅ | | | | |
| Host steals keyboard focus (spacebar → transport?) | ✅ **no** | | | | |
| Two instances in one project | ✅ — §A3.2 | | | | |
| Host still alive after removing the plugin | — not tested | | | | |

† only after granting the "Lower Security Settings" exception (§A2). A clean
machine would be blocked. Reaper, Live and Bitwig are not hardened this way and
will not reproduce that condition — so a pass there says nothing about §A2.

### On GarageBand as a Logic proxy

GarageBand is a **good proxy for the single most valuable thing Logic would
tell us, and a poor proxy for the rest.** Worth being precise about which.

**Where it substitutes well — the sandbox question.** GarageBand is a Mac App
Store app: App Sandbox plus hardened runtime, AU-only, same Core Audio AU
hosting path as Logic, same out-of-process `AUHostingService` mechanism. The
§B `localStorage` question is fundamentally *"can a WKWebView inside a
sandboxed, hardened Apple host read and write its data store, and where does
that store live?"* GarageBand exercises exactly that. If localStorage is
silently dropped or the container is per-host, GarageBand will show it.

Given §C's precedent — Live's modal webview having an ephemeral data store,
already worked around in `kromosynth-live` — this is the highest-prior failure
in the whole gate, and GarageBand tests it for free, today.

**Where it does not substitute.** Plugin *window* behaviour: Logic's plugin
window management, resize handling, and keyboard-focus routing are
substantially different from GarageBand's much simpler window. So the resize,
keyboard-focus and close/reopen rows are GarageBand answers, not Logic
answers. Do not let a GarageBand pass on those close out the Logic column.

**Record it as GarageBand.** A pass here is real evidence about sandboxing and
weak evidence about Logic's UI. Label accordingly rather than merging the
columns.

## A2. FINDING — Apple hosts refuse an ad-hoc signed plugin without lowering security (2026-08-25)

Selecting Synth.is in GarageBand's instrument slot produced a system dialog:

> **Use of the requested Audio Unit(s) requires lowering the security settings
> for "GarageBand". Are you sure you want to proceed?**
>   [ Lower Security Settings ] [ Cancel ]

After accepting, the plugin loaded — the instrument slot shows the green
`Synth.is` chip.

### What caused it

The build is **ad-hoc signed**, not Developer ID signed and not notarized. From
the build log:

```
code has no resources but signature indicates they must be present
-- Replacing invalid signature with ad-hoc signature
```

GarageBand ships with the hardened runtime. macOS will not let a hardened host
load third-party code that is unsigned or ad-hoc signed without an explicit
per-app exception, which is what "Lower Security Settings" grants.

### Why this is a WP finding, not just a dev annoyance

WP §7 treats signing and notarisation as a **Stage 1** concern — something to
do before distribution. This shows that for Apple hosts, signing is
**load-blocking, not cosmetic**. An unsigned build does not merely warn; the
host refuses it until the user weakens their own security posture.

Consequences:

1. **Signing moves onto the critical path earlier than Stage 1 implies.** Any
   Apple-host testing before then requires this exception, and any external
   tester would have to be walked through the same dialog.
2. **A real user must never see this.** A commercial plugin that opens with
   "lower your security settings" is dead on arrival. Properly Developer
   ID-signed and notarized plugins do not trigger it — which is why commercial
   AUs install cleanly. *Re-verify this at Stage 1 against an actually signed
   build rather than trusting the inference.*
3. **It confounds this machine's matrix.** GarageBand here now has the
   exception granted permanently. A clean machine would hit the dialog. Record
   GarageBand and Logic results as "with security exception granted", because
   they are not what a fresh install would do.
4. **Non-Apple hosts are unaffected.** Reaper, Live and Bitwig are not hardened
   in the same way and load ad-hoc signed plugins without complaint. So a clean
   Reaper result does *not* generalise to Apple hosts on this dimension.

### Corroborated registration

```
$ auval -a | grep -i synth
aumu Krm0 Sthi  -  Synth.is: Synth.is
```

Registered as `aumu` — a music device (instrument). This is also why the plugin
does not appear in effect menus: an earlier attempt browsed the **Master** bus
effect slot (`AUBandpass`, `AUDelay`, … all `aufx`), where an `aumu` component
cannot appear by design.

---

## A1. FINDING — Apple hosts load the plugin out-of-process (2026-08-25)

First GarageBand launch, before the editor was ever opened:

```
==> ~/Library/Logs/Synth.is/stage0-AUHostingServiceXPC_arrow.log <==
2026-08-25T20:57:47.320Z  processor_constructed host=AUHostingServiceXPC_arrow wrapper=AU
```

The host is **not GarageBand**. It is `AUHostingServiceXPC_arrow`, Apple's
out-of-process AU hosting XPC service. GarageBand does not load the plugin into
its own address space at all.

**Why this matters more than it looks.**

The §B `localStorage` question was framed as *"WKWebView's persistent store
lives in the hosting application's container, so login is per-host."* That
framing is now wrong in an interesting way. The container is not GarageBand's
and not Logic's — it is **`AUHostingServiceXPC`'s**. Consequences, none yet
tested:

- Sign-in state may be **shared across every AU plugin** Apple's service hosts,
  rather than per-DAW. Better than expected for UX, worse for isolation.
- Or the XPC service's container may be **more restricted than the DAW's**, in
  which case storage fails harder here than in an in-process host like Reaper.
- Logic and GarageBand may share one container, making them indistinguishable
  for storage purposes — which would mean GarageBand substitutes for Logic on
  the §B question even better than argued above.

The probe answers this. It has not run yet — see below.

**A defect in this instrumentation, caused by the same fact.**

`PluginProcessor.cpp` names the log file from
`File::getSpecialLocation(File::hostApplicationPath)`. Under out-of-process
hosting that resolves to the XPC service, not the DAW. So **GarageBand and
Logic will both write to `stage0-AUHostingServiceXPC_*.log`** and their results
will silently merge — in exactly the two hosts where per-host separation
matters most.

Workaround, no rebuild needed: move the log aside between hosts.

```sh
mv ~/Library/Logs/Synth.is/stage0-AUHostingServiceXPC_arrow.log \
   ~/Library/Logs/Synth.is/garageband-$(date +%H%M).log
```

Optional improvement (disposable code, one line): add
`juce::PluginHostType().getHostDescription()` to the `processor_constructed`
line. JUCE attempts real host identification there. Whether it sees through
XPC hosting is untested — if it also reports the service, the manual rename
above stays the answer.

**Only `processor_constructed` fired — no `editor_constructed`, no
`navigating`, no `page_finished_loading`.** That is a scan-time instantiation,
not a UI open. The webview has not been exercised at all yet. The plugin must
be placed on a Software Instrument track and its window opened.

---

### Before running any host

Quarantine the 2023 builds first (see §0b.3) — a crashing stale plugin can
abort a host scan and look like a Synth.is failure:

```sh
mkdir -p ~/Desktop/plugin-quarantine
mv ~/Library/Audio/Plug-Ins/Components/{KRME,SRVB}.component ~/Desktop/plugin-quarantine/
mv ~/Library/Audio/Plug-Ins/VST3/{KRME,SRVB}.vst3           ~/Desktop/plugin-quarantine/
```

If an Apple host does not see the plugin, reset the AU cache and relaunch:

```sh
killall -9 AudioComponentRegistrar
```

Notes on method:

- **Keyboard focus** is the one most likely to differ by host. The specific
  failure to watch for: typing in a search field and having the host swallow
  keystrokes as transport shortcuts. `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` is set
  in `src/CMakeLists.txt`; whether hosts honour it is the question.
- **Close/reopen** matters because `withKeepPageLoadedWhenBrowserIsHidden()` is
  enabled. Without it JUCE tears the page down on hide, and any "it lost my
  state" result would be about JUCE's default rather than the host.
- Logs land at `~/Library/Logs/Synth.is/stage0-<HostName>.log`, one file per
  host, appended. `editor_constructed` / `editor_destroyed` pairs tell you what
  the host actually did to the editor.

---

## A4. RESOLVED — the instability was our bug, and the fix is verified

Editor-lifetime test, built with `-DKROMOSYNTH_PROBE_DELAY_MS=30000` so the
trigger window is hit reliably rather than depending on a 3-second hand-timed
close. Three separate use-after-free opportunities, all caught, no crash, no
dialog:

| page_finished_loading | editor_destroyed | guard fired | delta |
|---|---|---|---|
| 21:42:29.379 | 21:42:35.518 | 21:42:59.384 | +30.005 s |
| 21:42:40.247 | 21:42:44.540 | 21:43:10.237 | +29.990 s |
| 21:42:49.280 | 21:43:11.646 | 21:43:19.273 | +29.993 s |

Each guard line pairs to its own page load within 5 ms of the configured delay.
Each editor was destroyed while its timer was still pending. Every one hit
`probe_skipped_editor_gone (SafePointer guard held)` instead of dereferencing
freed memory.

**One detail worth noting, because it rules out a false pass.** The first guard
fired at 21:42:59.384 — at which point a *different* editor was alive
(constructed 21:42:49.047, destroyed 21:43:11.646). The guard still correctly
reported its own editor gone. So `SafePointer` is tracking **instance
identity**, not merely "is any editor alive". A weaker guard would have passed
this test by accident.

**Verdict: H1 confirmed and closed. H2, H3 and H4 are not reached.** The
instability was a use-after-free in this repository's probe code, not a JUCE or
WKWebView defect. **This is not a WP finding.**

### What the WP should take from it anyway

The bug was four lines of instrumentation and it presented to the user as a
host-level *"an Audio Unit plug-in reported a problem which might cause the
system to become unstable"* dialog. Not a silent failure, not a caught
exception — a modal that tells the user to quit their DAW.

Stage 1's bridge will have many more C++/JS async callbacks than one probe
timer, and every one of them can outlive the editor. **`SafePointer`-or-
equivalent on every async callback should be a standing rule in the bridge
design**, stated in the WP rather than rediscovered. The cost of getting it
wrong is a dialog that makes the product look like it breaks the host.

---

## A4. INSTABILITY — GarageBand reported an AU plug-in problem (2026-08-25)

> **An Audio Unit plug-in reported a problem which might cause the system to
> become unstable. Please quit and restart GarageBand.**
>   [ Recover ] [ Quit ]

Occurred while exercising close/reopen and multi-instance behaviour.

**Cause not yet established.** What follows is a ranked set of hypotheses, not
a conclusion. Diagnostics needed before any of them is written into the WP.

### H1 (superseded — see confirmation above) — a use-after-free in THIS repository's probe code

Found by inspection immediately after the crash, in `PluginEditor.cpp`:

```cpp
juce::Timer::callAfterDelay (3000, [this]          // <-- raw `this`
{
    evaluateJavascript (kProbeScript,
        [this] (auto result) { proc_.logEvent (...); });   // <-- again
});
```

The probe fires 3 seconds after page load. **If the editor is destroyed inside
that window, the timer fires into freed memory.** Closing the plugin window,
switching tracks, or removing an instance all destroy the editor — and those
are precisely the operations being tested when this appeared. The inner
callback is worse: it captures `this` and dereferences `proc_` after an
asynchronous round-trip into WebKit, which can outlive the editor by an
arbitrary amount.

The timing fits: the dialog appeared during exactly the operations that trigger
it, and not during the earlier steady-state test where the window was simply
left open.

**Fixed** — `juce::Component::SafePointer` in both lambdas, with null checks.
Rebuild and re-test before drawing any conclusion.

**This is the misattribution trap, live.** Two turns ago the argument for JUCE
9 over 8.0.15 was that testing on an EOL branch risks reporting a WKWebView
failure that was really something else, and changing the WP on a false
negative. The same discipline applies with the sign reversed: *our own
instrumentation* is now the most likely cause, and reporting "webview
destabilises Apple hosts" on this evidence would be wrong in the identical way.

### H1 — CONFIRMED by log timing. Fix applied but NOT YET PROVEN.

The pre-fix log shows the mechanism exactly:

```
21:26:11.370  page_finished_loading      <- probe timer scheduled for ~21:26:14.37
21:26:11.488  PROBE {...}                <- belongs to an EARLIER load (~21:26:08.5)
21:26:12.098  editor_destroyed           <- destroyed 2.3 s BEFORE the pending timer
21:26:13.126  editor_constructed
21:26:13.304  page_finished_loading      <- timer scheduled for ~21:26:16.3
                                         <- log ends here. Crash.
```

Two editors were destroyed with a probe timer still pending against them. The
log terminates immediately after a load whose timer never got to fire. That is
the use-after-free, and the timing is unambiguous.

**However — the fix is not yet demonstrated.** In every post-fix cycle the
window stayed open well past the 3-second mark:

| PROBE | editor_destroyed | gap |
|---|---|---|
| 21:32:25.375 | 21:32:34.088 | 8.7 s |
| 21:32:41.083 | 21:32:44.238 | 3.2 s |
| 21:32:54.639 | 21:32:59.814 | 5.2 s |
| 21:33:03.232 | 21:33:05.651 | 2.4 s |
| 21:33:12.036 | 21:33:33.284 | 21.2 s |

The probe had already fired every time, so the `SafePointer` null-check was
never exercised. **Absence of the crash is not evidence the fix works when the
triggering condition was never met.**

Required test: open the plugin window and **close it within 3 seconds**, before
any `PROBE` line appears. Repeat several times. Expect `editor_destroyed` with
no following `PROBE`, and no dialog.

### H2–H4 — not reached

H1 is confirmed as *a* real defect present during the crash window. Whether it
was the *sole* cause is not established, but it is sufficient to explain the
observation and no other evidence points elsewhere. **This is not a WP
finding** unless the deliberate test above still fails.

### What is worth keeping for the WP regardless

Even as our own bug, this is a Stage 0 result: the async-lifetime hazard is
**easy to hit** in a webview plugin, and it presents to the user as "an Audio
Unit plug-in reported a problem which might cause the system to become
unstable" — a host-level dialog, not a quiet failure. Any real bridge, which
Stage 1 requires, has strictly more callbacks crossing the C++/JS boundary and
therefore strictly more of these. `SafePointer`-or-equivalent should be a
standing rule for every async callback in the bridge, not a lesson relearned.

### H2 — XPC watchdog on a blocked or slow main thread

`AUHostingServiceXPC` watchdogs hosted plugins. A WKWebView doing something
synchronous on the main thread, or the 1.5 s cold-open measured under `auval`,
could trip it. Distinguishable from H1: H2 should reproduce even with the probe
compiled out (`-DKROMOSYNTH_WEBVIEW_PROBE=OFF`).

### H3 — resource pressure from multiple webviews

Two instances means two WKWebViews, each loading a full React app plus
three.js, inside one sandboxed XPC service. Plausible but would not usually
present as this specific dialog.

### H4 — a genuine JUCE 9 / WKWebView defect in a plugin context

The finding the WP cares about. **Only creditable once H1 is eliminated.**

### Diagnostics to capture

```sh
# Crash reports, newest first. Look for AUHostingServiceXPC*, GarageBand*,
# or com.apple.WebKit.WebContent*
ls -lt ~/Library/Logs/DiagnosticReports/ | head -20

# Last lines before the failure
tail -40 ~/Library/Logs/Synth.is/stage0-AUHostingServiceXPC_arrow.log
```

In the `.ips`, what matters is **which process crashed and where the crashing
thread was**:

| crashing thread in… | reading |
|---|---|
| `KromosynthPlugin` / `Synth.is` frames, near `Timer` or `evaluateJavascript` | **H1 confirmed** — our bug |
| `com.apple.WebKit.WebContent` | H4 territory — a real WebKit finding |
| `AUHostingServiceXPC` with no plugin frames | H2 — watchdog or timeout |
| no crash report at all | H2 — a hang, not a crash |

WP §7 anticipates a WebKit crash and says it would *not* reopen the framework
choice, since iPlug2 would fail identically. That reasoning stands — but it
applies only to H4. It does not cover a bug in our own code.

### Retest protocol after rebuilding

1. Reproduce the original sequence with the fix in place. If it no longer
   occurs → H1 confirmed, and this is **not** a WP finding.
2. If it still occurs, rebuild with `-DKROMOSYNTH_WEBVIEW_PROBE=OFF` and repeat.
   Still failing with the probe gone → H2/H3/H4, and now genuinely interesting.
3. Only then test in Reaper, which loads in-process: a failure there too would
   separate "webview in a plugin" from "webview in Apple's XPC sandbox".

---

## A3. Host-behaviour results — GarageBand (2026-08-25)

| row | GarageBand | note |
|---|---|---|
| Plugin scan succeeds | ✅ | after granting the §A2 security exception |
| Editor opens, page renders | ✅ | ~1.2 s, React mounted |
| Typing into a web input reaches the page | ✅ | |
| Host steals keyboard focus (spacebar) | ✅ **no** | space types a space, transport does not start |
| **Resize: page reflows** | ❌ **cannot resize at all** | see A3.1 |
| **Two instances in one project** | ⚠️ **unclear** | see A3.2 |

### A3.1 GarageBand will not resize the plugin window

**UPDATE 2026-08-26 — Reaper resizes it. Cause 2 is unlikely; cause 1 leading.**

In Reaper 7.79 the plugin window resizes normally. So the editor *is*
advertising resizability and the mechanism works somewhere.

**The comparison is not yet clean, though.** GarageBand loaded the **AU**;
Reaper loaded the **VST3**. Format and host both changed at once, so this does
not yet distinguish:

| | still possible? |
|---|---|
| GarageBand does not offer resizable plugin windows | **yes** — leading |
| Our **AU** wrapper does not advertise resize, while VST3 does | **yes** — not excluded |

**The isolating test is one click:** Reaper loads AudioUnits too. Add
`AU: Synth.is` to a track in Reaper and try to resize it.

- **AU resizes in Reaper** → the wrapper is fine in both formats; GarageBand is
  the limitation. Record as a host quirk, no work needed, and expect Logic
  (whose plugin windows are far more capable than GarageBand's) to behave
  differently.
- **AU does not resize in Reaper** → it is our AU wrapper, it is ours to fix,
  and it would have been wrongly blamed on GarageBand.

Until that runs, do not write "GarageBand limitation" into the WP.

### A3.1c RESOLVED — the AU editor does not resize. Confirmed across three hosts.

| host | format | window resizes | page reflows |
|---|---|---|---|
| Safari (control) | — | — | ✅ |
| Reaper 7.79 | **VST3** | ✅ | ✅ |
| Live 12 Beta | **VST3** | ✅ | ✅ |
| Reaper 7.79 | **AU** | ✅ | ❌ **crops** |
| Live 12 Beta | **AU** | ❌ **corners dead** | n/a |
| GarageBand | **AU** | ❌ **corners dead** | n/a |

**Every VST3 works. Every AU fails.** Host, page, CSS and rendering engine are
held constant across the Reaper and Live pairs, so the web app is excluded
(Safari and both VST3s reflow) and the host is excluded (each host succeeds in
one format and fails in the other).

#### One cause explains both failure modes

The two AU symptoms look different but fit a single explanation: **the AU
editor is not advertising itself as resizable.**

- **Live and GarageBand honour that** and disable the resize affordance
  entirely — dead corners.
- **Reaper is permissive** and resizes the window regardless. The JUCE
  component follows (`editor_resized w=993 h=554` in the log), but because the
  AU view is not participating in a resize protocol, the embedded `WKWebView`'s
  native `NSView` frame never updates — so the page keeps laying out at its
  original width and is clipped.

That accounts for dead corners in strict hosts and cropping in a permissive one
without needing two separate bugs.

#### What is NOT established

Whether this is:

1. **`WebBrowserComponent`-specific** — the native `NSView` embedding does not
   cooperate with JUCE's AU editor resize path; or
2. **our configuration** — something about `setResizable`/`setResizeLimits`/
   `setSize` ordering, or the AU wrapper's preferred-size reporting.

This matters for ownership: (1) is an upstream JUCE report, (2) is ours to fix.
JUCE AU plugins with resizable editors are common and work in Logic, so a
blanket "JUCE AU cannot resize" is too strong a reading of this evidence.

**Decisive discriminator — RUN 2026-08-26. Cause 1 confirmed.**

`KROMOSYNTH_EDITOR_STUB` replaces the `WebBrowserComponent` with a plain
`juce::Component`, leaving `setResizable`, `setResizeLimits`, `setSize` and the
`resized()` logging byte-identical.

**Result: the stub AU resizes normally in Live 12.** All four border edges stay
visible and the printed dimensions track the drag. It reported `640 x 461` —
clamped at exactly the 640 px minimum from `setResizeLimits (640, 400, …)`, so
the limits are being honoured as well.

| | AU in Live 12 |
|---|---|
| Editor containing a plain `juce::Component` | ✅ resizes, limits honoured |
| Editor containing a `WebBrowserComponent` | ❌ corners dead |

**Conclusion: JUCE's AU editor resize machinery works. The
`WebBrowserComponent` is what breaks it.** Our `setResizable` /
`setResizeLimits` / `setSize` configuration is exonerated — identical code
resizes fine with a non-native child. This is upstream territory, not ours.

**Cheap test — RUN 2026-08-26. Occlusion hypothesis CONFIRMED.**

`KROMOSYNTH_BROWSER_INSET=24` pulls the browser 24 px back from the bottom and
right, exposing JUCE's `ResizableCornerComponent`. AU in Live 12:

- **The corners drag.** The same AU that had dead corners is now resizable.
- **And the page reflows.** At the resized width the nav collapsed to
  icons-only, the hero fitted, and the cards stacked — no cropping.

So the AU resize failure was **occlusion**: a native `WKWebView` `NSView`
composites above JUCE's software-rendered content regardless of z-order, and
JUCE's `toFront()` on the corner cannot beat that. AU hosts do not supply their
own resize affordance, so burying JUCE's grip removes the only one.

#### A nuance worth keeping: there are two resize paths, and only one is broken

| path | JUCE component resizes | WKWebView follows |
|---|---|---|
| **JUCE's own grip** (inset build, Live AU) | ✅ | ✅ **reflows** |
| **Host-driven** (Reaper AU, host window resize) | ✅ (`editor_resized w=993`) | ❌ **crops** |

When the resize originates inside JUCE, the native child follows correctly and
the page relayouts. When the *host* drives the AU view's size, the JUCE
component updates but the native child's frame does not.

That is a sharper bug report than "AU can't resize", and it means the earlier
Reaper cropping is a **separate defect** from the dead corners — not one cause
with two faces, as §A3.1c originally proposed. That proposal was wrong.

#### Stage 1 implication

There is a viable mitigation: reserve a strip so JUCE's grip is not covered.
The 24 px orange band used here is diagnostic and ugly, but a thin styled
handle in a corner would work and costs nothing architecturally.

Still worth an upstream report, because the host-driven path (row 2) is not
fixed by insetting and will affect any host that resizes AU windows itself.

**Not adopted.** Per the session brief this is recorded as a finding with a
known mitigation, not merged into the shipping editor. `KROMOSYNTH_BROWSER_INSET`
defaults to 0.

#### A bad check, recorded so it is not repeated

```sh
strings <binary> | grep -c "EDITOR STUB"     # returned 1 for BOTH builds
```

`StubContent::paint` was compiled unconditionally — only its *construction* was
`#if`-ed — so the string literal was present in every binary. The check could
not distinguish the thing it existed to distinguish, and briefly suggested the
inset build had not installed when in fact **Live was caching the previously
loaded binary**; a restart was all that was needed.

Fixed: `StubContent::paint` is now inside `#if KROMOSYNTH_EDITOR_STUB`. The
reliable discriminators are the log line (`EDITOR_STUB build …` vs
`navigating to https://synth.is`) and, visually, the solid orange strip.

#### Candidate mechanism — the resize corner is buried under the native view

A hypothesis that fits every observation, stated as one:

AU host windows generally size themselves to the plugin's view rather than
providing their own resize affordance, so JUCE supplies its own
`ResizableCornerComponent` (added by `setResizable (true, true)`). The stub is
software-rendered by JUCE, so that corner draws and hit-tests on top of it —
visible as the grip in the bottom-right of the screenshot.

A `WebBrowserComponent` on macOS is a real Cocoa `NSView` (`WKWebView`).
Native views composite **above** JUCE's software-rendered content regardless of
JUCE's z-order, so the resize corner is covered: invisible and unclickable.

That explains both failure modes without needing two causes:

- **Live / GarageBand AU** — JUCE's corner is the only affordance and it is
  buried → dead corners.
- **Reaper AU** — Reaper supplies host-level window resizing, so the window and
  the JUCE component do resize (`editor_resized w=993` in the log), but the
  `WKWebView`'s `NSView` frame does not follow → crop.
- **VST3 everywhere** — VST3 hosts provide their own resize affordance and
  JUCE's VST3 wrapper propagates size into the native child, so both work.

**Cheap test, ~5 min, not yet run.** Inset the browser inside the editor so
JUCE's corner is exposed — in `resized()`, use
`browser_->setBounds (getLocalBounds().withTrimmedBottom (24).withTrimmedRight (24))`
instead of the full bounds. Load the AU in Live.

- **Corners now work** → mechanism confirmed. It is an occlusion problem, and
  Stage 1 has a cheap mitigation (reserve a strip for the grip) as well as a
  precise upstream bug report.
- **Corners still dead** → the hypothesis is wrong and the cause is deeper in
  JUCE's AU view handling. Report upstream with the stub A/B as evidence.

Note this is diagnosis, not a workaround being adopted — the finding stands
either way, and the trimmed-bounds change is not being kept.

#### Unrelated minor: non-ASCII in C++ string literals is mangled

The stub rendered `EDITOR STUB â€ no WebBrowserComponent` — the em-dash in the
source literal came through as mojibake, i.e. UTF-8 bytes being interpreted as
single-byte characters somewhere between the compiler and `juce::String`.

Cosmetic here, in code that is being deleted. Worth recording only because it
means **any non-ASCII text originating on the C++ side will be corrupted** —
error strings, labels, anything user-facing from the plugin rather than the
webview. Use `juce::CharPointer_UTF8 ("…")` explicitly, or stay ASCII, once
Stage 1 has C++-side text.

#### Why this matters more than a cosmetic bug

**AU is the only format Logic and GarageBand accept.** On Apple hosts the
plugin UI is a fixed-size window; in Reaper's AU it is a cropped one. Combined
with §A2 (Apple hosts also refuse unsigned builds), the Apple-host path is the
worst-supported one — and Logic is a large share of the target market.

§4's "one UI codebase" currently means **"a UI that lays out correctly in VST3
hosts."** Recorded as a finding, not worked around, per the session brief — but
it needs an owner before Stage 1.

### A3.1c (superseded) — it is the AU wrapper. VST3 reflows, AU crops, same host.

Controlled comparison, Reaper 7.79, same page, same WebKit:

| | window resizes | page reflows |
|---|---|---|
| Safari (control) | — | ✅ |
| Reaper, **VST3** | ✅ | ✅ |
| Reaper, **AU** | ✅ | ❌ **crops** |
| GarageBand, AU | ❌ | n/a |

Only the plugin format varies between rows 2 and 3. Host, page, CSS and
rendering engine are all held constant, so **the web app's CSS is excluded**
(Safari and the VST3 both reflow) and **the host is excluded** (same Reaper).

The log confirms the JUCE side is working in the AU too:

```
17:28:19.498  editor_resized w=993 h=554
17:28:19.565  editor_resized w=993 h=553
17:28:19.702  editor_resized w=993 h=552
```

So `resized()` runs and `browser_->setBounds (getLocalBounds())` executes — JUCE
believes the component is 993 px wide — while the WKWebView keeps laying out at
its original width and is clipped.

**Likely mechanism, stated as a hypothesis.** `WebBrowserComponent` on macOS
wraps a native `NSView` (the `WKWebView`). The AU wrapper hosts the editor
inside `JUCE_AUCocoaViewClass`, and the JUCE-side bounds update appears not to
reach the embedded native view's frame in that path, whereas the VST3 wrapper's
view hierarchy propagates it. Not verified — confirming it means stepping
through JUCE's AU view resize path.

#### Why this matters more than a cosmetic bug

**AU is the only format Logic and GarageBand accept.** So on Apple hosts — the
ones §A2 already showed are strictest, and the ones a large share of the target
market uses — the plugin UI cannot reflow. Combined with GarageBand refusing to
resize at all, the Apple-host experience is a fixed-size window showing a
cropped responsive web app.

This is a **finding, not a task**: per the session brief it is recorded rather
than worked around. It does need an owner before Stage 1, because "one UI
codebase" (§4) currently means "a UI that only lays out correctly in VST3
hosts".

Open: whether this is AU-generic or specific to Reaper's AU hosting. Live 12
loads both formats and would settle it with the same A/B.

### A3.1c (superseded) UPDATE — the editor IS being resized. The PAGE is not reflowing.

Reaper, AU. The log settles what the screenshots could not:

```
17:28:19.498  editor_resized w=993 h=554
17:28:19.565  editor_resized w=993 h=553
17:28:19.702  editor_resized w=993 h=552
```

`resized()` is being called with live drag values, so
`browser_->setBounds (getLocalBounds())` is running and the JUCE component is
tracking the window. **Yet the content crops instead of reflowing.**

That rules out the framing used up to now. This is not "the host refuses to
resize" — in Reaper the host resizes fine and we receive the events. The
question is narrower: **does the WKWebView re-lay-out the document when its
bounds change?**

Revised candidate causes:

| | status |
|---|---|
| GarageBand does not offer resizable plugin windows | still true **for GarageBand**, separate issue |
| Our AU wrapper does not advertise resize | **superseded** — see A3.1c: it advertises and receives resize, but the page does not reflow |
| WKWebView's viewport is not following the component bounds | **open** |
| synth.is has a CSS min-width and would crop in any browser | **open** |

#### The decisive test takes thirty seconds

Open `https://synth.is` in **Safari** and narrow the window to roughly 990 px.

- **Safari reflows** (nav collapses, text rewraps) **but the plugin crops** →
  the viewport is not propagating. A real plugin-side finding, and ours.
- **Safari crops the same way** → the web app has a min-width and this is a CSS
  matter, not a plugin one. Nothing to fix in the plugin; the UI simply needs a
  usable layout at plugin-window widths.

Safari is the right comparison rather than Chrome: same WebKit engine, so a
difference isolates the embedding rather than the renderer.

### A3.1d FINDING — storage is per-host-application, not shared

`persist: SURVIVED age=224s` in Reaper.

**224 seconds, not a day.** The GarageBand value was written 2026-08-25 at
~21:32; had Reaper seen the same store, the age would read roughly 72,000 s.
Instead the key was absent and freshly created — ~17:24 today, during the
earlier Reaper VST3 session — and then read back by the Reaper **AU** instance
at 17:28.

Two things follow:

1. **Different hosts have different stores.** GarageBand (via
   `AUHostingServiceXPC`) and Reaper do not share `localStorage`.
2. **Plugin format does not matter.** Reaper's VST3 wrote it and Reaper's AU
   read it, so the container is scoped to the **host application**, not the
   plugin binary or format.

**§5 consequence.** Sign-in and the entitlement record are **per-DAW**. A user
running Logic, Live and Reaper signs in three times, and revoking access in one
leaves the others authenticated until their own tokens expire. That is a
product decision the WP has not made, and it argues for the token living in the
plugin's own storage (a file the plugin owns, shared across hosts) rather than
in the webview's `localStorage` — which is also the RFC 8252 shape §5 already
mandates, so the two pressures agree again.

Still untested: whether **Logic and GarageBand** share a store, since both go
through `AUHostingServiceXPC`. If they do, the boundary is the *hosting
process*, not the DAW.

### A3.1e CAUTION — the test machine's audio device is at 24 kHz

```
prepareToPlay sampleRate=24000 blockSize=480
audio: state=suspended sr=24000
```

Reaper is running at **24 kHz**. Unusual, and worth fixing before any audio
work: Task 2's sfizz playback, round-robin and velocity-layer testing would all
be evaluated through a 12 kHz Nyquist. Bluetooth headsets in headset
(bidirectional) mode commonly force rates like this.

Set Reaper's device to 44.1 or 48 kHz before Task 2.

**It also refines §C.** In GarageBand both host and page reported 44100, and
that was recorded as coincidence. Here both report 24000. So the page's
`AudioContext` does appear to follow the same system output device the host is
using — they agree *while* the host uses the system default device. They would
diverge the moment a host takes a different device, or an exclusive-mode
interface at another rate. §C's point stands, but the mechanism is now
understood rather than assumed.

### A3.1b Open question — does it reflow, or only clip?

The matrix row is *"resize: page reflows, no clipping"*, and the Reaper
screenshot shows the window narrower than 1280 px with content running off the
right edge: the nav's rightmost control is cut in half and the hero subtitle
ends mid-word ("…shaped by human creativit").

Two readings, not yet distinguished:

1. **Normal responsive overflow.** The page has a min-width and the browser is
   scrolling/clipping horizontally, exactly as it would in a narrow browser
   window. Benign, and a web-app CSS matter rather than a plugin one.
2. **The viewport is not being updated.** The `WebBrowserComponent` bounds
   follow `getLocalBounds()` in `resized()`, but if the page's viewport does
   not track that, the document keeps laying out at the old width and simply
   gets cropped. That would be a real plugin-side finding.

**How to tell:** narrow the window well below any tablet breakpoint. If the nav
collapses, columns stack, or text rewraps, the viewport is live and this is
case 1. If the layout stays pixel-identical and merely loses its right-hand
edge, it is case 2.

---

### A3.1 (original) GarageBand will not resize the plugin window

Dragging the window corners **moves** the window; it does not resize it. The
editor requested and received 1280×800 and is stuck there.

This is not obviously our bug. `setResizable(true, true)` and
`setResizeLimits(640, 400, 3840, 2400)` are set, and the two `editor_resized`
lines in the log are this code's own calls, not the host's. Two candidate
causes, not yet distinguished:

1. **GarageBand does not offer resizable AU plugin windows.** Plausible — its
   plugin UI presentation is deliberately simplified compared to Logic's.
2. **The AU wrapper is not advertising resizability to the host.**

**Do not generalise this to Logic.** §A flagged window behaviour as precisely
the dimension where GarageBand is a *poor* Logic proxy, and this is that
dimension. Reaper and Live will disambiguate: if either resizes fine, cause 1
is established and this is a GarageBand quirk. If none of them resize, cause 2
is ours and worth fixing.

**Why it matters if it holds.** The web app is a responsive browser app. A
fixed 1280×800 window is larger than some laptop screens can comfortably show
alongside a DAW, and the user has no recourse. If hosts broadly refuse resize,
the UI has to target a fixed size, which weakens the "same UI everywhere"
premise in §4 — not fatally, but it becomes a design constraint rather than a
free win.

### A3.2 "Only one instance at a time" — needs disambiguating

After adding a second track with the same instrument, only one appeared usable.
**Three different things could produce that observation**, with very different
implications:

| cause | how it looks | severity |
|---|---|---|
| GarageBand shows only one plugin **window** at a time | both instances exist and both render audio; only one UI visible | **none** — a UI convention, not a limit |
| The second **instance** fails to instantiate | second insert silently does nothing | serious |
| Both instantiate but share one **webview/page** | two editors, one page | serious, and a JUCE/WKWebView issue |

GarageBand does present plugin UI in a single Smart Controls panel, so cause 1
is the most likely and the most benign. **The log settles it**, and the probe
has been extended to help:

- Two `processor_constructed` lines → two real instances exist.
- `editor_constructed` / `editor_destroyed` alternating → the host is swapping
  a single window between them (cause 1).
- Two `PROBE` lines with **different `pageId`** → two independent webviews.
  Same `pageId` → the page is being shared (cause 3).

Re-test after rebuilding. Do not record a verdict until the log says which.

---

### A3.2 "Only one instance at a time" — RESOLVED: benign

**Cause 1 confirmed.** After the GarageBand restart restored the two-track
project:

```
21:33:58.861  processor_constructed host=AUHostingServiceXPC_arrow wrapper=AU
21:33:58.869  prepareToPlay sampleRate=44100 blockSize=1024
21:33:58.875  processor_constructed host=AUHostingServiceXPC_arrow wrapper=AU
21:33:58.879  prepareToPlay sampleRate=44100 blockSize=1024
21:34:06.556  editor_constructed          <- only ONE editor
```

Two processors, two `prepareToPlay` calls, one editor. Both instances exist and
are prepared to render; GarageBand simply presents one plugin **window** at a
time in its Smart Controls panel. That is a host UI convention, not a limit on
the plugin.

Every `pageId` across the session was distinct — `5z7ktt`, `gtev5q`, `99s0ca`,
`izr8cr`, `2j0jke`, `w5jg9l` — so no webview or page was ever shared between
editors. Cause 3 is excluded.

**Row verdict: PASS.**

### A3.3 FINDING — the web app fully reloads every time the window reopens

Not in the original gate, and it should be.

Every `editor_constructed` is followed by a fresh navigation, a new `pageId`,
and `sessionStorage: "fresh"`. The page is **torn down and rebuilt from
scratch** on every plugin-window open — roughly 1.2 s of load, React remounts,
all in-memory state discarded.

`withKeepPageLoadedWhenBrowserIsHidden()` is set and does not help here,
because GarageBand does not *hide* the editor — it **destroys** it
(`editor_destroyed` on every close). The option only covers the hide case.

What survives: `localStorage` (see B2), so sign-in and entitlement persist.
What does not: scroll position, search results, current selection, playback
position, any un-persisted UI state.

**Why this matters to §4.** The "one UI codebase" premise assumes the plugin
webview behaves like a browser tab. It does not: a tab persists across being
backgrounded, whereas the plugin window is a cold start every time. For a
browsing-and-discovery UI, where a user opens the window, searches, closes it
to work in the DAW, and reopens — losing the search each time is a real
friction, and it is the DAW-native interaction pattern, not an edge case.

Not fatal, and cheap to mitigate: persist UI state (last query, selection,
scroll) to `localStorage`, which is proven to survive. But it is **work the web
app does not currently do**, and it is plugin-specific. Worth putting in the WP
as a scoped task rather than discovering it in Stage 1.

Open question: do other hosts destroy or hide? If Reaper or Live merely hides
the editor, `withKeepPageLoadedWhenBrowserIsHidden()` would preserve the page
there and behaviour would differ per host — which is its own problem.

---

## B. Capability probe — RESULT (GarageBand / AUHostingServiceXPC, 2026-08-25)

The webview **loads, renders, and the app mounts.** Full sequence, ~1.2 s from
editor construction to page load:

```
21:17:51.788  editor_constructed
21:17:51.934  editor_resized w=640 h=400
21:17:51.934  editor_resized w=1280 h=800
21:17:51.934  navigating to https://synth.is
21:17:52.391  page_about_to_load https://synth.is/
21:17:52.403  page_about_to_load https://synth.is/
21:17:53.008  page_finished_loading https://synth.is/
21:17:56.091  PROBE {...}
```

| field | result | verdict |
|---|---|---|
| `rootChildren` | `1` | React mounted. The app really rendered, not just the document. |
| `href` | `https://synth.is/` | BrowserRouter origin correct. |
| `secureContext` | `true` | fine |
| **`crossOriginIsolated`** | **`false`** | **FAILURE — and a genuine WKWebView finding.** See below. |
| **`sharedArrayBuffer`** | **`undefined`** | consequence of the above |
| **`localStorage`** | **`writable`** | **prediction was wrong.** Good news, with a caveat. |
| `opfs` | `present` | `navigator.storage.getDirectory` available |
| `audio` | `state=suspended sr=44100` | confirms §C — the page has its own audio graph |
| `webgl` | `webgl2` | three.js will work |
| `serviceWorker` | `present` | API available |
| **`ua`** | no `Safari`/`Version` token | **new finding.** See below. |

### B1. crossOriginIsolated is false — RESOLVED: a real WKWebView property, but a NON-FINDING for this product

The deployed origin sends both required headers, verified before the test
(`COEP: credentialless`, `COOP: same-origin`). In a compliant browser that
yields `crossOriginIsolated === true`. In the plugin it is false and
`SharedArrayBuffer` is absent. Because the deployment was checked first, this
is genuinely a WKWebView property, not a server gap — most likely WebKit not
honouring COEP `credentialless`, which is a Chromium-first value.

**But the grep shows almost nothing depends on it.** Three call sites total:

| site | uses | impact when `crossOriginIsolated === false` |
|---|---|---|
| `components/RenderComparison.jsx` | `@sqlite.org/sqlite-wasm` | see below — likely already unusable in a webview for an unrelated reason |
| `components/TakeoutView.jsx:12` | `self.crossOriginIsolated && …` | **benefits** — see below |
| `components/MediaLinks.jsx:22` | `self.crossOriginIsolated && …` | **benefits** |

**The two `needsVideoFallback` sites take the better branch.** Both read
`const needsVideoFallback = self.crossOriginIsolated && …`. With
`crossOriginIsolated` false, `needsVideoFallback` is false and **no fallback is
needed**. Losing isolation actively helps here.

**`RenderComparison.jsx` is a local-analysis tool, not a user flow.** It walks
a directory tree looking for `genomes.sqlite` files and deserialises them into
an in-memory `sqlite3.oo1.DB()`. Two observations:

1. In-memory `sqlite3_deserialize` does **not** require SharedArrayBuffer.
   Only the OPFS SyncAccessHandle VFS does, and this code does not use it. So
   sqlite-wasm here is probably fine without isolation.
2. It reaches the files via the **File System Access API**
   (`entry.kind === 'file'` — a directory-picker walk). WebKit does not support
   `showDirectoryPicker`, so this component is almost certainly unusable inside
   the plugin webview **regardless of isolation**. Worth confirming, but it is
   a dev/analysis surface, not part of the plugin's job.

**Verdict: downgrade B1 from FAILURE to a recorded property.** No action
required for Stage 0 or Stage 1. Do not switch the deployment to
`require-corp` — that would force CORP headers on every cross-origin
subresource to fix something nothing needs.

**One forward constraint, though.** WP §5 defers a local library to Session 3.
If that is ever built on sqlite-wasm with the **OPFS VFS**, it will need
SharedArrayBuffer, and this result says it will not get it inside the plugin.
OPFS itself is `present`, so the non-SAB OPFS paths remain open. Design the
local library around that constraint rather than discovering it later.

### B6. No Web Inspector — JUCE 9 does not expose `isInspectable`

```sh
$ grep -rni "inspectable" third_party/JUCE/modules/juce_gui_extra/
$   # no output
```

Since macOS 13.3, `WKWebView.isInspectable` defaults to false and the host app
must opt in programmatically; the old `defaults write … WebKitDeveloperExtras`
route no longer applies. JUCE 9.0.1's `WebBrowserComponent` never sets it, so
**Safari Web Inspector cannot attach to the plugin's webview** without patching
JUCE or reaching the native handle.

Consequence for Stage 1: debugging the JS side of a real bridge will have no
devtools. The `evaluateJavascript` probe used here is the workaround and it
works well — but it is print-debugging, not a debugger. Worth deciding early
whether to carry a small JUCE patch enabling `isInspectable` in Debug builds.
Record it in the WP as a known development-ergonomics cost of the webview
approach; it is the sort of thing that silently doubles Stage 1 effort.

### B1. crossOriginIsolated is false — this one is real

The deployed origin sends **both** required headers, verified independently
before the test:

```
cross-origin-embedder-policy: credentialless
cross-origin-opener-policy: same-origin
```

In a standards-compliant browser that yields `crossOriginIsolated === true`.
Here it is false and `SharedArrayBuffer` is absent. Because the deployment was
verified first, **this is a property of WKWebView, not of the server.** That
pre-check is what makes the result interpretable, and it is the reason it was
added.

**Most likely cause: WebKit does not honour COEP `credentialless`.** WebKit
supports `require-corp`; `credentialless` is a Chromium-first value with much
later and patchier WebKit support. Stated as a hypothesis, not a conclusion —
verify by serving `require-corp` and re-probing.

**But first establish whether it matters.** `vite.config.js` justifies the
header as *"for SharedArrayBuffer support"* (sqlite-wasm), and
`@sqlite.org/sqlite-wasm` is in `package.json`. Yet no sqlite usage was found
under `src/` — no matching filenames, nothing in `src/lib/`, `src/audio/`, or
the top level. **If nothing in production actually uses SharedArrayBuffer,
this is a non-finding** and the header is vestigial.

Settle it before spending effort:

```sh
cd kromosynth-desktop/packages/web
grep -rn "SharedArrayBuffer\|crossOriginIsolated\|sqlite" src/ | head -30
```

- **No hits** → vestigial config. Note it and move on. Consider dropping COEP,
  which would also stop constraining cross-origin subresources.
- **Hits** → that code path is broken inside the plugin. Switching COEP to
  `require-corp` is the fix to test, at the cost of requiring CORP headers on
  every cross-origin subresource — which is presumably why `credentialless`
  was chosen.

### B2. localStorage is writable — the prediction was wrong

§C argued from the `kromosynth-live` precedent (Live's modal webview having an
ephemeral data store) that storage would likely fail in a sandboxed host. **It
did not.** localStorage is writable inside `AUHostingServiceXPC`. On this
evidence the §5 entitlement-record and auth-token design is viable.

**The caveat matters, though.** The probe writes and reads back within one
page session. It does **not** test persistence across:

1. editor close → reopen
2. host quit → relaunch
3. GarageBand → Logic (both use `AUHostingServiceXPC`, so they may share one
   container — untested and interesting either way)

Those are the questions auth actually depends on. Follow-up test in §B4.

### B3. The User-Agent identifies this as an embedded webview

```
Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)
```

Note what is **missing**: no `Version/x.y` and no `Safari/605.1.15` suffix. A
real Safari UA carries both. A bare `AppleWebKit ... (KHTML, like Gecko)` tail
is the standard fingerprint of an embedded WKWebView, and it is trivially
detectable server-side.

This matters for three things:

- **Auth.** Google and other OAuth providers actively block embedded webviews
  and detect them this way. WP §5 already mandates RFC 8252 — system browser
  plus PKCE, token held natively. This result is **empirical confirmation that
  §5's design is forced, not merely preferred**: the in-webview login shortcut
  would be refused by the provider, not just inadvisable.
- **Payments.** Card providers commonly refuse embedded webviews. Relevant to
  §5's purchase flow.
- **Analytics and WAF rules.** Plugin sessions will be misclassified as bots or
  as an unknown browser unless the UA is explicitly handled.

### B4. Follow-up tests this result implies

Not yet run. All cheap.

1. **Storage persistence.** In the webview, run
   `localStorage.setItem('persist_probe', Date.now())`. Close the plugin
   window, reopen it, read it back. Then quit GarageBand, relaunch, read again.
   This is the question §5 depends on — not the intra-session write.
2. **Container sharing.** Repeat in Logic. Same value visible → shared
   `AUHostingServiceXPC` container. Different → per-host.
3. **`require-corp` re-probe**, only if the grep above finds real
   SharedArrayBuffer usage.
4. **In-process comparison.** Reaper loads plugins in-process, so its webview
   sits in Reaper's own container rather than Apple's XPC service. If storage
   behaves differently there, that isolates the cause to the XPC sandbox.

### B5. Minor observations

- `page_about_to_load` fired **twice**, 12 ms apart, both for `https://synth.is/`.
  Probably a redirect from the bare origin to `/`. Harmless, noted so it is not
  mistaken for a reload loop later.
- The two `editor_resized` lines are this code's own `setResizeLimits` then
  `setSize` firing, not the host. **GarageBand did not resize the editor
  afterwards** — it accepted the requested 1280×800. The resize *test* (dragging
  the window) is still outstanding.
- `audio: sr=44100` matches `prepareToPlay sampleRate=44100`, but that is
  coincidence: the page's AudioContext follows the **system output device**,
  not the host. Under a host running at 48 kHz they would diverge — which is
  precisely §C's point.

---

## B. Capability probe — the added questions

The probe in `PluginEditor.cpp` fires 3 s after page load and writes one
`PROBE {...}` line per load. Record the JSON per host.

| field | why it matters | source on disk |
|---|---|---|
| `crossOriginIsolated`, `sharedArrayBuffer` | `packages/web/vite.config.js` sets `COEP: credentialless` explicitly "for SharedArrayBuffer support" (sqlite-wasm). `credentialless` is Chromium-first; WebKit support is late and version-dependent. If false here, isolation fails **silently** inside a page that looks perfect. | `vite.config.js` `coopCoepPlugin` |
| `localStorage` | `AuthService.js` stores `kromosynth_token`, `kromosynth_user`, `kromosynth_token_expiry` there. WKWebView's persistent store lives in the **hosting application's** container → login is per-host, and sandboxed/hardened hosts (Logic) may deny it. Directly affects WP §5's "locally-stored entitlement record". | `src/services/AuthService.js` |
| `audio` | Confirms the page has its own `AudioContext`. See section C. | `@elemaudio/web-renderer`, `virtual-audio-graph` in `package.json` |
| `webgl` | `three` ^0.185.1 is a dependency. | `package.json` |
| `serviceWorker` | `main.jsx` calls `registerServiceWorker()`. Push API is unavailable in WKWebView; the call is `.catch()`-wrapped so it should degrade rather than throw. Confirm it degrades. | `src/main.jsx` |
| `rootChildren` | Non-zero means React actually mounted, not just that the document loaded. | — |

**Expected-to-be-fine, worth confirming:** routing. `App.jsx` uses
`BrowserRouter` (history API), which needs a server with SPA fallback. Pointing
at deployed `https://synth.is` satisfies that. It is worth stating explicitly
because the named reference implementation does the opposite: `agentic-synth`'s
`WebUiComponent` uses `withResourceProvider` to serve bundled assets off JUCE's
`juce://` virtual origin, which **would** break `BrowserRouter`. Stage 0
deliberately does not do that.

---

## C. The finding that does not need a test

Recorded here because it is visible from the source and will otherwise be
rediscovered expensively.

**Audio from the webview does not go through the plugin.**

The web app plays through `@elemaudio/web-renderer` and `virtual-audio-graph` —
Web Audio API, inside the page. In a plugin, WKWebView's audio goes to the
**system default output device**. Not the plugin's bus. Not the host's sample
rate. Not through the host's transport, and not recordable by it.

`PluginProcessor::processBlock` clears the buffer, so at Stage 0 the plugin
outputs silence while the webview is audible — through a different path, at a
different sample rate, ignoring the host's monitoring and routing.

WP §5 says "auditioning inside the plugin is interaction and free". That is a
monetisation statement, and it is fine. But it quietly assumes audition works
like a plugin. It does not. A user auditioning in Logic hears sound that Logic
cannot route, record, or process.

This is a design problem for the WP, not a bug to fix in Stage 0. Options, none
chosen here:

1. Accept it. Audition is preview-quality and out-of-band; only downloaded
   artefacts play through the plugin bus. Simple, and slightly dishonest to the
   user unless the UI says so.
2. Route webview audio into the plugin. Requires capturing the page's audio —
   there is no supported WKWebView API for this. Effectively a rewrite of
   audition as a native path.
3. Move audition to the plugin's own engine. The webview browses; the plugin
   plays. Fits "the platform compiles; the plugin plays" better than the
   current split does, and makes the sampler engine load-bearing earlier than
   Stage 1 assumes.

Option 3 is the coherent one, and it moves work **forward**, not back. Flagging
it before Stage 1 scope is set.

### The kromosynth-live precedent — and why it only half-transfers

`kromosynth-live` (the Ableton Live extension) already runs this split, and it
works:

- Live's modal **WebView** shows `/ableton` from the remote origin. Preview
  there is Web Audio, out the system output — exactly the situation above.
- On commit, `src/synthis/renderSource.ts` `renderItemToWav()` runs in the
  **extension-host Node process**, not the webview: it fetches the genome and
  renders over the preview WebSocket (`ws` backend) or submits a job to the VI
  service (`vi` backend), then `inject.ts` drops a real WAV into the set as an
  audio clip.

So "preview in the webview, hand off to the compiled core" is not speculative.
It is shipped, and it validates **one** of the plugin's two jobs:

| job | does the kromosynth-live pattern cover it? |
|---|---|
| **Browse → commit a file to the session** (drag-to-lane, clip injection) | **Yes.** Preview is a shopping action; the committed artefact is a file. Webview preview is adequate because nothing about it needs to be sample-accurate or host-routed. |
| **Be an instrument** (MIDI in → audio out of `processBlock`) | **No.** Nothing in the preview path helps. The sampler engine has to actually play. |

Two consequences the WP does not currently state:

1. **The plugin has two audio paths from day one, and the user must be able to
   tell which one they are hearing.** The trap: audition a sound in the webview
   (system output, page's sample rate, no track FX, no latency compensation),
   like it, load it into the sampler — and it sounds different. Different SRC,
   different render parameters, different signal chain. That is an A/B trap of
   exactly the kind that burned the render-comparison work before, and it will
   be blamed on the sampler.

2. **Preview and "how it will sound in my session" diverge.** Webview audio
   bypasses track routing, sends, and monitoring. In Live that means a preview
   is never what the clip will sound like once it lands.

### A precedent that raises the prior on the localStorage prediction

From `kromosynth-live/src/extension.ts`, verbatim from the source comment:

> Live gives each modal a fresh WebView with an ephemeral data store, so the
> app can't remember its own sign-in between opens — it hands the token (and
> the open composition id) back on close and we pass them in again next time.
> In memory only: the JWT never touches disk.

That is the section-B `localStorage` concern, already encountered, already
worked around, in a real host. It does not prove JUCE's `WebBrowserComponent`
in an AU/VST3 behaves the same way — different host, different webview
lifetime, and Live's *modal* is explicitly ephemeral where a plugin editor need
not be. But it moves the prediction from "plausible" to "already happened once
nearby", and it hands us the fallback design if the probe confirms it: keep the
token in the C++ process and re-inject it per page load, exactly as the
extension does with `composeResume`.

Worth noting that this fallback is *also* the RFC 8252 shape WP §5 already
mandates — system browser + PKCE, token held natively, never in the embedded
webview. So the failure mode and the required design point the same way.

---

## D. Task 2 material — checked, and it is short

Not started (gated on Task 1). But the artefacts were inventoried, and one of
Task 2's stated questions **cannot be answered with what is on disk**.

17 rendered VIs exist under `kromosynth-vi/data/vi-artifacts/`. Every one of
them is either:

- **XY-morph** (`generateXYMorphSFZ`) — which per `sampleMatrix.js`'s
  `xy-pad-layers` branch forces `velLayers[0]` only and sets
  `roundRobin: 0, seqLength: 0`; or
- **plain** with `Velocity Layers: 1` and no `seq_*` opcodes at all.

So: **no artefact on disk has more than one velocity layer, and none has
round-robins.** "Whether round-robins and velocity layers behaved" needs a
fresh render with `velocityLayers > 1` and `roundRobins > 1` before it can be
asked. Worth queuing now so it is ready when Task 1 clears.

Predictions to test, from reading the generator (falsifiable, written before
running anything):

1. **Round-robin indexing is correct.** `sampleMatrix.js` uses
   `for (let rr = 1; rr <= effectiveRRCount; rr++)`, so `seq_position` is
   1-based, matching SFZ. The emit guard in `sfzGenerator.js`
   (`seqLength > 1 && roundRobin > 0`) is consistent. I went looking for an
   off-by-one and there isn't one.

2. **XY morph will misbehave under sfizz.** `generateXYMorphSFZ` relies on two
   `amplitude_oncc` bindings on the same `<group>` **multiplying**, and the
   source comment asserts this as fact — "giving the center layer tent(X) ×
   tent(Y) natively, no workarounds needed". But the neighbouring comments
   reference ConvertWithMoss and Decent Sampler group mapping, so it was
   verified against sforzando/DS, **not sfizz**. Multiply-vs-sum for stacked
   `amplitude_oncc` is exactly the ARIA-divergence class WP §3 warns about.
   Also unverified against sfizz: `<curve>` at `curve_index=10..12` with sparse
   `vNNN=` points, `amplitude_curvecc`, `set_ccN` defaults, and
   `ampeg_sustain_oncc72=-80` (negative depth).

3. **WP §3's `cutoff=0` caution does not apply to our own output.** The
   generator never emits `cutoff` at all. Absent ≠ zero. That caution is
   inherited from the ConvertWithMoss report about *converted* files; carried
   into this WP it reads as a constraint on the VI service that the VI service
   already satisfies by omission. Minor, but the WP claims to be grounded.

4. **No `loop_mode`, no sustain loops.** Holding a key past the rendered sample
   duration produces silence. A limitation of the artefact, not of sfizz, and
   the first thing anyone will notice playing a pad.

Concrete test material:
`kromosynth-vi/data/vi-artifacts/ymn5rj11wx4kuhk/3f2477c2-b260-46ff-a08f-28cde501a84f/`
— `instrument.sfz` + 20 WAVs in `samples/`, 4 pitches × 5 XY directions,
`default_path=samples/`.

---

# E. TASK 2 PREPARATION — session of 2026-08-27

Both §D prerequisites are cleared and sfizz builds and links. No engine code
exists yet: nothing in `src/` references sfizz, so the linker currently drops
the archive. Task 2's actual question is still unanswered.

The headline is not about our code. **The sfizz library repository is
archived.** WP §3 and §9 both need revising, and the fallback they name is in
better health than the engine they chose.

---

## E1. The submodule pin did not take, and the version string did not warn

`git submodule add` records the gitlink at the default-branch tip. A manual
`git -C third_party/sfizz checkout 1.2.3` afterwards moves only the working
tree; the next `git submodule update --init --recursive` reverts it silently.

The result was a tree at `1.2.3-39-gf5c6e29f`, 39 commits past the tag, while
`project(libsfizz VERSION 1.2.3)` still read 1.2.3. Nothing in the source
indicated the drift. `git describe --tags` was the only thing that did.

The missing step is `git add third_party/sfizz` between the checkout and the
update. `cmake/Sfizz.cmake` now asserts the pin at configure time and fails
with the correct sequence in the error text.

**This mattered more than a tidiness issue.** Every conclusion recorded about
sfizz's CMake in the first half of the session was read off `f5c6e29f`, not the
tag, and one of them was materially wrong (see E2). The generic lesson is the
one already in this document's standing rules: verify the thing you are
measuring before trusting the measurement.

## E2. sfizz 1.2.3 does not build on Apple silicon

`cmake/SfizzConfig.cmake` at the tag:

```cmake
elseif(PROJECT_SYSTEM_PROCESSOR MATCHES "(arm.*)")
    add_compile_options(-mfpu=neon)
    if(NOT ANDROID)
        add_compile_options(-mfloat-abi=hard)
```

`arm64` matches `arm.*`, so 32-bit ARM flags are applied to an arm64 target.
Apple clang rejects both options. Because these are directory-scope
`add_compile_options`, the failure lands in the vendored subprojects first:
abseil-cpp and WavPack fail before sfizz itself is reached.

Upstream added `AND NOT (PROJECT_SYSTEM_PROCESSOR MATCHES "(arm64)")` after the
release. That fix exists on the development branch and **is in no tag**. The
last tagged library release does not build on the only platform Stage 0
targets.

Worked around by pre-setting `PROJECT_SYSTEM_PROCESSOR` to the equivalent
`aarch64` spelling before `add_subdirectory`. Safe because the variable is read
in exactly three places at this tag: these flags, a status line, and
`SfizzSIMDSourceFiles.cmake`, which branches on x86 names only. An arm64 host
takes the same path under either spelling.

**Universal builds are not covered.** For `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`
sfizz derives `PROJECT_SYSTEM_PROCESSOR` as the literal list `arm64;x86_64`,
which also matches the broken branch, and no single value gives correct flags
for both slices. `cmake/Sfizz.cmake` now hard-errors on
`KROMOSYNTH_UNIVERSAL_BINARY` with the sampler enabled rather than failing
obscurely. This blocks the `release` preset for any sampler build and needs
resolving before Stage 1 ships anything.

## E3. clang 21 rejects the vendored atomic_queue, at every commit

`external/atomic_queue/include/atomic_queue/atomic_queue.h` has four sites of

```cpp
Base::template do_pop_any(states_[index], elements_[index]);
```

with no template argument list after the `template` keyword. That is
ill-formed. Clang places
`-Wmissing-template-arg-list-after-template-kw` in its DefaultError group, so
it is an error irrespective of `-Wall` or `-Werror`, and sfizz's own warning
configuration is irrelevant to it. Every translation unit that includes
`Synth.h` is affected.

**Checked before working around it:** the same four lines are present verbatim
at the development tip `f5c6e29f` as at `1.2.3`, even though that file was
otherwise substantially rewritten between them. Moving the pin would fix E2 and
not this. That is the argument for staying on the tag: a newer commit buys one
of two fixes and costs a tagged, auditable pin.

Downgraded to a warning with `-Wno-error=`, probed with
`check_cxx_compiler_flag` because the diagnostic name is clang-version-specific,
and applied per-target so our own code keeps the diagnostic at full strength.

## E4. The sfizz library repository is archived

WP §3 records that `sfizz-ui` was archived in June 2026 and calls the engine
"stable", with §9 listing liquidsfz as a fallback to revisit at Stage 3. The
situation is worse than that and the revisit should not wait for Stage 3.

| | `sfztools/sfizz` | `swesterfeld/liquidsfz` |
|---|---|---|
| archived | **yes** | no |
| last push | 2025-03-17 | 2026-04-24 |
| last release | 1.2.3, 2024-01-14 | 0.4.1, 2026-04-07 |
| previous release | 1.2.2, 2023-08-26 | 0.4.0, 2026-03-13 |
| open issues | 73 | 19 |
| open PRs | 14, unmergeable | — |
| licence | BSD-2-Clause | MPL-2.0 |

`sfztools/sfizz-ui` is also archived, last pushed 2025-03-19.

Among the 14 open pull requests that can never be merged, by title: a
`Voice::startVoice()` crash fix (2025-10-09), CMake Xcode-generator build fixes
(2025-10-07), **"Fixes build with Clang 17" (2025-04-10)**, a
`Voice::Impl::fillInterpolated()` crash fix (2025-04-09), and CMake presets plus
architecture testing (2025-03-17).

**What follows from this.**

1. There is no upstream to contribute to. Archived GitHub repositories do not
   accept pull requests. E2 and E3 are permanent local maintenance, not patches
   awaiting a release.
2. Two toolchain-vintage build failures appeared on the first build attempt,
   and a third ("Fixes build with Clang 17") is sitting unmerged. Compilers only
   move forward. This is the shape of the cost, and it is ours.
3. The fork option in WP §3 is no longer a contingency. Using sfizz *is*
   maintaining a fork, whether or not one is formally created.
4. The other side, stated fairly: archived also means it will never break us
   with a bad release, and BSD-2-Clause means the source is permanently ours.
   The risk is decay against a moving toolchain, not churn.
5. **Availability is a separate risk from maintenance.** The build depends on a
   third-party archived repository remaining reachable. A `git clone --mirror`
   onto own storage addresses that and costs about 22 MB. Independent of the
   engine decision.
6. liquidsfz is MPL-2.0, not BSD-2-Clause. MPL-2.0 is file-level copyleft and
   by default carries the secondary-licence clause permitting distribution
   under GPL/LGPL/AGPL, which would suit an AGPL plugin. **Verify properly
   before relying on it.** Not legal advice, and not verified here.

**Recommendation: do not switch engines yet, and do make the choice
measurable.** Task 2's question is largely a format question, and the answer
feeds the engine decision rather than depending on it. The load-time
conformance dump should run the same SFZ through **both** engines. Two
independent implementations disagreeing about stacked `amplitude_oncc` would
settle the XY question by measurement instead of argument.

## E5. Test material — both artefacts now exist

**XY morph (pre-existing).** `kromosynth-vi/data/vi-artifacts/ymn5rj11wx4kuhk/3f2477c2-b260-46ff-a08f-28cde501a84f/`

**Velocity layers + round-robins (rendered this session).**

```
~/Developer/apps/synth.is/kromosynth-vi/data/vi-artifacts/anonymous/b234fe6d-8423-489c-b60d-275b4471b069/
```

62 MB, 42 samples. Sound `01KPXZJ94GPQEEAZFA6F91YR1W`, generation 1869 of
`01KPV7RX0XMRTKEC2KVRCCRVA6_quality-musicality_enriched-cppn-dsp_hybrid-phase_run`.
7 pitches, 3 velocity bands (1-42, 43-84, 85-127), `seq_length=2` with
positions 1 and 2 on every band. Rendered in 14 s.

Hub copy at `/Volumes/kromosynth2/kromosynth-vi/vi-artefacts/anonymous/<artifactId>`.
Note the hub uses `ARTIFACT_STORAGE_PATH` with an `anonymous/<artifactId>`
layout, while the local dev tree uses `<runId>/<artifactId>`. Two layouts for
the same thing.

**`pitchSampling: "fifth"` was chosen deliberately**, not for musical reasons.
See E6.2: `octave` sampling double-covers every boundary key, which would have
confounded the round-robin test by matching two groups on the same note.
Resulting key zones are contiguous with no overlap: 42-51, 52-58, 59-65, 66-72,
73-79, 80-83, 84-90.

**The audio device prerequisite is cleared.** All outputs report 48000 Hz.
§A3.1e recorded 24 kHz; that is now stale or was a different machine.

**WAV format:** stereo, Int16, 48 kHz. **No `smpl` chunk** in either artefact,
so §D prediction 4 stands on evidence rather than inference: there are no
embedded loop points for sfizz to fall back on, and holding a key past the
sample duration will produce silence.

## E6. Two defects found outside the plugin

### E6.1 `kromosynth-recommend` — query results treated as arrays

`src/config/database.arcadedb.js` `query()` returns `{ records, summary,
getAll() }`, an object. Three sites in `src/routes/internalRoutes.js` treated it
as an array:

| line | endpoint | effect |
|---|---|---|
| 152 | user preferences | `result.length === 0` is never true, so `result[0].upv` threw on **every** call |
| 280 | genome | same, and it surfaced to `kromosynth-vi` as a 500 where a missing sound should have been a 404 |
| 384 | genome context | `metadataResult.length > 0` always false, so `metadataMap` stayed empty and every context response silently omitted class, score and generation |

The guard `if (!x || x.length === 0)` cannot fire on a wrapper object: it is
truthy, and `undefined === 0` is false. The third is the worst of the three
because it fails silently.

Fixed by calling `getAll()` and guarding the resulting array. A scanner for the
antipattern (a query result indexed, counted or iterated without an intervening
`getAll()`) was written, **validated against the pre-fix file** where it
correctly found all three, then run across the repository: no other occurrences.
254 `db.query` call sites against 247 `getAll()` call sites, so the codebase
does this correctly nearly everywhere.

Note: the fix is in the local checkout only. The hub runs its own checkout at
`/Users/bthj/Developer/synth.is/`, so the live service is unaffected until
deployed and restarted.

### E6.2 `kromosynth-vi` — `calculateKeyRange` double-covers boundary keys

`src/services/sampleMatrix.js`:

```js
low  = Math.ceil((pitchMidis[index - 1] + midiNote) / 2);
high = Math.floor((midiNote + pitchMidis[index + 1]) / 2);   // "minus 1 to avoid overlap"
```

The comment says the subtraction is there; the code has no subtraction. When
the interval between adjacent key centres is **even**, `ceil` and `floor`
coincide and the boundary key is covered by two regions.

`octave` (12) is the only sampling mode that triggers this, and it is the
default in `requestFullVI`. `fifth` (7), `minor3rd` (3) and `chromatic` (1) all
produce odd sums and are unaffected.

Visible in the existing XY artefact: key centres 48, 60, 72, 84 give zones
42-54, 54-66, 66-78, 78-90, so MIDI 54, 66 and 78 each match two regions **per
layer**. On a 5-layer XY instrument that is 10 voices on a boundary key where 5
is intended, plus audible doubling of two differently pitch-shifted samples.

Not fixed. It is in `kromosynth-vi`, not the plugin, and changing it changes
every future artefact. Flagged for a decision.

## E7. Predictions scorecard

Recorded because this document's value depends on predictions being falsifiable
and on wrong ones being reported.

| prediction | outcome |
|---|---|
| §D1 round-robin indexing is 1-based and correct | **Confirmed** — generated SFZ emits `seq_position=1` and `2` |
| §D2 XY morph misbehaves under sfizz | **Still untested** — needs the engine |
| §D3 `cutoff=0` caution does not apply, generator never emits it | **Confirmed** by reading both generated SFZ files |
| §D4 no `loop_mode`, holding a key gives silence | **Confirmed and strengthened** — no `smpl` chunk either |
| `sfizz::sfizz` target exists | **Confirmed** — `ALIAS sfizz_static`, src/CMakeLists.txt:358 |
| four forced options do not exist at 1.2.3 | **Confirmed** — `SFIZZ_LV2`, `SFIZZ_LV2_UI`, `SFIZZ_VST`, `SFIZZ_AU` |
| a link-library grep for `sndfile` would false-positive | **Confirmed** — `st_audiofile` is linked unconditionally |
| `fifth` avoids the key-range overlap | **Confirmed** in the generated SFZ |
| **first build failure would be the C++ standard clash** | **WRONG.** Setting sfizz to C++14 made it a non-issue. The real failures were architecture detection (E2) and a compiler-vintage conformance error (E3), neither anticipated |

## E8. Verified, versus still assumed

**Verified by measurement, not reading:**

- No libsndfile anywhere in the build. Across every built archive: 0 libsndfile
  symbols, 87 dr_libs symbols, located in `libst_audiofile*.a`.
- No extra runtime dependencies. `otool -L` on the built VST3 lists system
  frameworks only, despite `SfizzDeps.cmake` unconditionally probing for Qt5
  and finding Homebrew's libsamplerate 0.2.2. Both are used only by
  `benchmarks/` and `devtools/`, which are gated off.
- sfizz compiles at C++14 with `-mmacosx-version-min=10.9` while the plugin is
  at 11.0. Read off the generated `flags.make`, not inferred.

**Chosen deliberately and worth knowing:** sfizz is built at C++14 rather than
17 because `SfizzConfig.cmake` compiles a substitute global `operator
new`/`delete` when the standard is >= 17 and its aligned-new probe fails, and
that probe fails here because sfizz forces the deployment target to 10.9 while
aligned allocation needs 10.14. Replacing global `operator new` inside a static
library linked into a DAW-hosted plugin is not something to acquire by
accident. Safe because `sfizz.hpp`'s public API is `std::string`,
`std::vector` and `std::unique_ptr` only, with no absl types and no
`std::string_view`, so no type definition differs across the standard boundary.

**Still assumed, to be settled by the engine work:**

- That `sfizz::Sfizz::loadSfzFile` parses our SFZ at all.
- Whether stacked `amplitude_oncc` bindings multiply or sum.
- Whether `<curve>` blocks with sparse `vNNN` points are interpolated or
  treated as zero between defined points.
- Whether `set_ccN` defaults are honoured. If not, CC85/86 default to 0 and the
  XY instrument opens with Center silent and West plus South at full, which is
  a loud and unambiguous symptom needing no measurement.
- Whether round-robin cycling and velocity switching behave.
- Voice count under XY morph: five groups match every note regardless of CC
  position, so whether sfizz culls zero-amplitude voices decides between 5 and
  1 voice per note.

---

# F. TASK 2, HALF 1 — LOAD-TIME CONFORMANCE — session of 2026-08-27 (second)

Half 1 of Task 2 is done. Both artefacts load, nothing is silently dropped, and
four of the six §E8 open items are now settled. The tool that settled them is
`tools/sfz-probe`, a console executable that links sfizz and nothing else: no
JUCE, no plugin wrapper, no DAW, no audio device.

**The plugin still does not reference sfizz.** That was a deliberate reordering
(see F1) and it means the linker still drops the archive. Unchanged from §E.

## F1. Why a harness instead of the plugin engine

The §E brief split Task 2 into a load-time half and an "audible behaviour"
half, the second described as needing MIDI and a real render. The second half
needs neither MIDI hardware nor a DAW: `noteOn`, `cc` and `renderBlock` are
plain calls on `sfz::Sfizz`, and `enableFreeWheeling()` exists precisely so an
offline caller gets deterministic output (`Synth.cpp:1115` calls
`filePool.waitForBackgroundLoading()` under that flag).

So both halves are offline, both are measurements rather than auditions, and
neither needs the plugin. Given the standing rule about A/B listening, making
Half 2 a number rather than an impression is worth the reordering.

The cost is stated plainly: the archive-dropped problem is still open, and the
plugin engine is still unwritten.

**One deviation from the brief.** The brief asked for the SFZ path as a compile-
time define. That is right for the plugin and wrong for the harness: the point
of the dump is comparing two artefacts, and a define makes that two
reconfigures. `sfz-probe` takes argv. The define stays in the plan for the
plugin engine.

## F2. liquidsfz — not built, and the reason is not the maintenance question

§E4 recommended pointing the conformance dump at both engines. Checked before
building it: **libsndfile is not optional in liquidsfz.** Its README lists
`libsndfile1-dev` as a required package; `--without-jack` and `--without-lv2`
exist and there is no `--without-sndfile`; distro packaging carries libsndfile
as both a build and a runtime dependency. It is also autotools rather than
CMake.

That runs straight into the constraint `cmake/Sfizz.cmake` has two assertions
defending. The fair counter is that a developer tool which never ships is not a
distribution event, so this is arguably a "keep it out of the repo and out of
the plugin target" question rather than a veto. Not legal advice and not
verified properly.

Deferred rather than rejected. The XY question that motivated it is answered
below by sfizz's own parsed state plus its modulation-matrix source, so the
second engine is no longer on the critical path.

Also noted from the same check: liquidsfz's licence changed from LGPL-2.1 to
MPL-2.0 upstream, reflected in distro packaging around October 2025. §E4's
MPL-2.0 line is right for current releases and wrong for older ones.

## F3. What sfizz does with our files

Both dumps under `build/probe-out/` (gitignored). Headline numbers:

| | RR artefact | XY artefact |
|---|---|---|
| `load.ok` | true | true |
| regions | 42 | 20 |
| `<group>` headers | 7 | 5 |
| preloaded samples | 42 | 20 |
| `num_curves` | 7 | 13 |
| `unknown_opcodes` | **0** | **0** |
| stderr | one `default_path` line | one `default_path` line |

Nothing was rejected. No parse errors, no parse warnings, no unsupported
control opcodes.

**`num_curves 13` is correct, not a defect.** `CurveSet::createPredefined`
seeds 7 (`Curve.h:86`), and `addCurve` with an explicit index resizes the
vector to index+1, leaving skipped slots null. Three curves at 10, 11 and 12
therefore give 13, with 7-9 as null gaps. The RR file, which declares no
curves, reports 7. The dump prints the arithmetic alongside the number.

## F4. The §E8 scorecard

| §E8 item | outcome |
|---|---|
| `loadSfzFile` parses our SFZ at all | **Confirmed.** Both files, zero unknown opcodes. |
| `set_ccN` defaults honoured | **Confirmed, and the predicted failure does not happen.** `/cc85/default` and `/cc86/default` both read 0.503937006, which is 64/127. The "Center silent, West and South at full" symptom is off the table. |
| stacked `amplitude_oncc` multiply or sum | **Multiply, from source; not yet measured.** See F5. |
| sparse `vNNN` interpolated or zero-filled | **Interpolated, from source; not yet measured.** See F5. |
| round-robin cycling and velocity switching | **Structurally intact; behaviour not yet measured.** All 42 RR regions carry `seq_length=2` with positions 1 and 2, and the three velocity bands reconstruct as 1-42, 43-84, 85-127, exactly as written. |
| voice count under XY morph | **Still open.** Needs the render half. |

The XY amplitude bindings survived byte-faithfully. Region 0 (center) carries
`cc85 amplitude depth=100 curve=10` and `cc86 amplitude depth=100 curve=10`;
west and south carry curve 12 on their own axis; east and north carry curve 11.
`ampeg_sustain_oncc72=-80` is retained with its negative depth intact.

## F5. Two answers read out of the source, held as hypotheses

Both are consistent with everything the dump shows and neither has been
measured. Recorded as hypotheses on purpose.

**Stacked `amplitude_oncc` multiply.** `ModId::Amplitude` carries
`kModIsMultiplicative` (`modulations/ModId.cpp:55`), and `ModMatrix` does
`buffer[i] *= ...` for each source after the first into a multiplicative
target, filling 1.0 when there are none (`ModMatrix.cpp:456`, `:481`). So sfizz
agrees with what `generateXYMorphSFZ` asserts, and the ARIA-divergence worry in
WP §3 does not bite here. Unchecked: the depth scaling and the interaction with
the region's base amplitude.

**Sparse `vNNN` points are linearly interpolated.** `buildCurveFromHeader`
marks only the given indices as filled and calls `fill()`; the interpolator is
hard-coded Linear because the spline branch is `#if 0` (`Curve.cpp`). Separately
worth knowing: it pre-seeds v000=0 and v127=1.0 *before* reading the opcodes, so
a curve omitting `v127` silently gets 1.0. Ours sets both explicitly.

## F6. Three things the load-time dump cannot see

These are limits of the method, and the third is the one that could bite.

1. **`getUnknownOpcodes()` is weaker than it looks.** It deduplicates by opcode
   name across the whole file, so it never says how many times or where. It is
   fed only from `buildRegion`, so `<control>` opcodes never reach it at all:
   `handleControlOpcodes` sends unsupported names to `DBG()` and nothing else.
   And it is populated when `Region::parseOpcode` returns false, so "accepted"
   is not "has an effect".

   **Demonstrated, not argued.** A deliberately bad file with two bogus region
   opcodes and two bogus control opcodes reports `unknown_opcodes.count 2`,
   naming only the region ones, with the control ones appearing solely on
   stderr as `Unsupported control opcode:`. Always capture stderr separately.

2. **There is no `/curve&/...` introspection path.** Only `/num_curves`. Curve
   shape is not readable at load time.

3. **A curve index is reported whether or not the curve exists.** The bad file
   above declares no curves and reports `num_curves 7`, yet region 0 still
   reports `cc85 amplitude depth=100 curve=10`. sfizz stores the index on the
   connection without validating it, and `CurveSet::getCurve` falls back to
   `Curve::getDefault()`, a linear 0-to-1 ramp, for a missing index.

   So a typo'd `curve_index` would turn every XY tent into a linear ramp, and
   the dump would look identical either way. `num_curves` is the only load-time
   signal and it is indirect. **This is a render-half test, and it is the
   strongest argument for doing the render half at all.**

## F7. Two prior findings confirmed from sfizz's side

**§E6.2 confirmed, with the number.** Of the 49 keys in the XY instrument's
range, 46 match 5 regions and **3 match 10** (MIDI 54, 66, 78). §E6.2 predicted
"10 voices on a boundary key where 5 is intended" from reading
`calculateKeyRange`; sfizz's own parsed key ranges agree exactly. The decision
about `calculateKeyRange` is still outstanding.

**`fifth` sampling does avoid it.** Every key in the RR instrument's range
matches exactly 6 regions: 3 velocity bands times 2 round robins, no more. The
deliberate choice recorded in §E5 was the right one and is now measured rather
than reasoned.

## F8. Three traps in the introspection API, recorded because they cost time

1. **`/region&/group` is not the `<group>` header index.** It returns
   `region.group`, the SFZ `group=` opcode used for polyphony and `off_by`. Our
   generator never emits it, so it reads 0 for all 20 regions of a file with
   five `<group>` headers. The dump labels it `poly_group`. Header count is
   `num_groups`.

2. **The `ampeg_*`, `offset` and `end` CC paths are never nil.** They read a
   `CCMap` with `getWithDefault`, which returns a zero entry for all 128 CCs,
   unlike `amplitude`/`volume`/`pan`/`pitch`, which go through
   `Region::ccModDepth` and return `absl::nullopt` when absent. Unfiltered this
   is roughly 43k lines of nothing for a 42-region file. The dump suppresses
   depth-zero-with-default-curve and says so.

3. **Every region carries bindings the file never asked for.** `amplitude` on
   cc7 and cc11 with curve 4, and `pan` on cc10 with curve 1. That is sfizz
   implementing MIDI volume, expression and pan. Consequence for absolute
   level: cc7 defaults to 100/127 and curve 4 is x squared, so there is a
   standing amplitude factor of about 0.62, not 1.0. Any level comparison
   against a reference render has to account for it.

## F9. A bug in the diagnostic, found before it was believed

The first version of the dump reconstructed MIDI velocities by parsing its own
printed output. `Default::hiVel` carries `kFillGap`, so `hivel=42` is stored as
`nextafter(43/127, 0)` rather than `42/127`, which makes adjacent bands
half-open and non-overlapping. Printed at `%g` that is `0.338583`; read back and
multiplied by 127 it is `43.000041`, which ceils one too high. The bands came
out as 1-43 and 43-84, **overlapping at 43** — which looks exactly like the
§E6.2 defect and is entirely an artefact of the formatter.

Fixed by keeping the numeric value alongside the text and printing at `%.9g`.
Correct output is 1-42, 43-84, 85-127, and at 9 digits the one-ULP gap between
`0.338582635` and `0.338582665` is visible.

Filed under the existing standing rule. The failure mode here is narrower and
worth naming separately: **never round-trip a measurement through its own
display string.** Had this not been caught, §E6.2 would have acquired a second,
fictitious instance in the RR artefact.

## F10. Next

- Render half: curve shape (F6.3 is the priority), multiply-versus-sum by
  measurement, round-robin cycling by fingerprinting successive renders,
  velocity switching, and voice count under XY morph.
- Then the plugin engine, which is what stops the archive being dropped.

## F11. `loadSfzFile` returns true when every region was dropped

Found while turning the negative control into a committed test file
(`tools/sfz-probe/testdata/negative-control.sfz`). With an unresolvable sample
path, the dump reports:

```
load.ok true
num_regions 0
```

and stderr carries `File not found, could not resolve ...` followed by
`Removing the region with sample ...`. Regions are culled after parsing
(`Synth.cpp:901`, "Remove bad regions with unknown files") and `loadSfzFile`
still returns true.

**A VI artefact with a broken `default_path` would load "successfully" and play
silence.** Given that §E5 already records two different on-disk layouts for the
same artefact — the hub uses `ARTIFACT_STORAGE_PATH` with
`anonymous/<artifactId>` while the local dev tree uses `<runId>/<artifactId>` —
this is a live failure mode, not a hypothetical.

The plugin engine must therefore check `getNumRegions() > 0` after loading, not
just the boolean. Cheap, and it converts a silent instrument into a diagnosable
one.

---

# G. TASK 2, HALF 2 — OFFLINE RENDER — session of 2026-08-27 (second)

Task 2 is answered. Every remaining §E8 item is now settled by measurement, and
one of them the opposite way round from the §D prediction.

No DAW, no audio device, no MIDI hardware. `sfz-probe render` and
`sfz-probe curve` drive `noteOn` / `cc` / `renderBlock` directly under
`enableFreeWheeling()`.

## G1. The scorecard, closed

| question | answer |
|---|---|
| stacked `amplitude_oncc`: multiply or sum | **Multiply.** Measured. |
| `<curve>` sparse `vNNN`: interpolated or zero | **Linearly interpolated.** Measured, exact. |
| round-robin cycling | **Works.** Alternates correctly. |
| velocity switching | **Works**, at exactly the declared boundaries. |
| voice count under XY morph | **5 per note.** No culling of silent voices. |

## G2. Multiply, decisively — §D2 was wrong

One region, one sample, two `amplitude_oncc` bindings on curve 10, everything
else stripped away. Holding cc86 at 32 (where curve 10 reads 0.5) and sweeping
cc85:

| cc85 | measured gain | multiply predicts | sum predicts |
|---|---|---|---|
| 32 | **0.2500** | 0.25 | 1.00 |
| 64 | **0.5000** | 0.50 | 1.50 |

With cc86 held at 64 (curve 10 reads 1.0) the sweep reproduces the plain tent
exactly, which is the control.

So `generateXYMorphSFZ`'s source comment — "giving the center layer tent(X) x
tent(Y) natively, no workarounds needed" — is **correct for sfizz**, not only
for sforzando and Decent Sampler. §D2 predicted the XY morph would misbehave
under sfizz. It does not. The ARIA-divergence caution in WP §3 does not bite
here, and the F5 source reading (`kModIsMultiplicative`) is confirmed.

## G3. The tent curve is exact, and interpolation is linear

Curve 10 declares `v000=0 v032=0.5 v064=1.0 v096=0.5 v127=0`. Measured against
an unmodulated reference render of the same region:

```
cc    0   8    16    24    32    40    48    56    64
gain  0  .125  .250  .375  .500  .625  .750  .875  1.000
```

Every declared point lands exactly, and every undeclared point sits exactly on
the straight line between its neighbours. F6.2 recorded that curve shape is not
introspectable at load time; this is the render half supplying what the dump
structurally could not see.

## G4. XY morph costs five voices per note, at every position

`getNumActiveVoices()` reports **5** at (64,64), (0,64), (127,64), (64,0),
(64,127) and (32,32) alike. sfizz does not cull voices whose amplitude
modulation has driven them to zero — they render silently and still occupy a
voice.

The §E8 question was "5 or 1". It is 5, always. A four-note chord on an XY
instrument is 20 voices before any release tails overlap, and sfizz's default
polyphony is finite. This is a real constraint on the XY design rather than a
detail, and it should inform both the voice-count budget in the plugin and
whether `generateXYMorphSFZ` should emit five layers at all.

## G5. Round-robin works. The artefact is what does not.

Alternation is correct: a two-region control gives `ABABAB`, and the full
artefact gives `BABABA` wherever the two round-robin samples actually differ.

**But 10 of the 21 round-robin pairs in the RR artefact are byte-identical.**
Checked by md5 across all 42 WAVs: 42 files, 32 distinct checksums.

```
048_v2, 055_v1, 055_v3, 062_v2, 069_v1, 069_v3,
076_v2, 083_v1, 083_v3, 084_v2      rr1 == rr2
```

Roughly half the round-robin variation this artefact claims to have does not
exist. On those keys the feature is decorative: sfizz alternates dutifully
between two identical files.

This is a `kromosynth-vi` finding, not a plugin one, and it is unexplained. It
is not uniform across velocity bands (055 has identical v1 and v3 but a
genuinely varying v2), which argues against a simple "round robins were never
implemented" reading and toward something conditional in the render path.
**Flagged for a decision alongside §E6.2.**

## G6. Velocity switching, proven with a scale-invariant statistic

Crest factor (rms/peak) is unchanged by a gain change and changes when the
sample changes. Note 55:

| velocity | band | crest |
|---|---|---|
| 10, 25, 42 | v1 | 0.231797 |
| 43, 60, 84 | v2 | 0.225290 |
| 85, 110, 127 | v3 | 0.206552 |

Identical to six decimals within each band, different across. The transitions
land exactly at 42-to-43 and 84-to-85, matching the file. Peak is also very
nearly continuous across the boundaries (0.019283 to 0.020485), so
`amp_veltrack` does not produce an audible step at a band edge.

## G7. Three more bugs in the diagnostic, all caught before the result

Continuing the F9 theme. None of these were sfizz's.

**G7.1 — Overlapping passes.** The first round-robin run rendered repeats
back-to-back without draining, so each pass carried the previous note's release
tail and `max_voices` climbed to 2. It produced `ABBBBB`, which reads as
"round-robin fires once then sticks". Fixed by rendering to
`getNumActiveVoices() == 0` between passes and reporting the settle count, so a
contaminated pass is visible rather than silent.

**G7.2 — Wrong velocity band, and it nearly became a sfizz bug report.** The
corrected run still gave `AAAAAA`. The cause was mine: I ran at velocity 100
believing it was band v2. The bands are 1-42, 43-84, 85-127, so 100 is band
**v3** — and 055_v3 is one of the ten identical pairs in G5. A minimal
two-region control file settled it in one run by making sfizz's behaviour
observable in isolation. Building the minimal control was the cheap move and it
should have come first.

**G7.3 — A normalised hash that could not work.** To separate "different
sample" from "same sample, different gain" I first fingerprinted the
peak-normalised signal. Dividing by peak and quantising at 1e-6 amplifies float
rounding, badly at low levels, so renders of the *same* sample at velocities 10
and 25 hashed differently. A hash needs exact equality and normalisation cannot
deliver it. Replaced with crest factor, which is scale-invariant by
construction and compared against a tolerance rather than for equality.

**And one guard that worked.** The first curve sweep referenced every point
against cc=127 — which is silent for a tent curve. The silence guard aborted
instead of printing a column of 0/0. Referencing a sweep against a point inside
the sweep only works if you already know the answer; it now references an
unmodulated render, which also makes the numbers absolute gains rather than
ratios.

## G8. Updated §E7 predictions

| prediction | outcome |
|---|---|
| §D2 XY morph misbehaves under sfizz | **WRONG.** Multiply is what sfizz does. |
| §D4 no `loop_mode`, holding a key gives silence | Confirmed again from the engine side: every region reports `loop_mode no_loop`. |
| F5 stacked `amplitude_oncc` multiply (from source) | **Confirmed by measurement.** |
| F5 sparse `vNNN` linearly interpolated (from source) | **Confirmed by measurement, exactly.** |
| §E8 voice count 5 or 1 | **5.** No culling. |

## G9. What is left

Task 2 is done. The open items are no longer about the SFZ format:

- **The plugin engine.** Still unwritten; the archive is still dropped. It must
  check `getNumRegions() > 0` after loading (F11), and budget for G4's five
  voices per note.
- **`kromosynth-vi` decisions**, both now measured rather than argued: the
  §E6.2 `calculateKeyRange` overlap (3 keys matching 10 regions), and G5's
  duplicate round robins.

---

# H. §E6.2 FIXED — `calculateKeyRange` boundary overlap

Fixed in `kromosynth-vi`, verified with `sfz-probe`. Staged but **not committed**
— that repo has unrelated WIP.

## H1. The cause

```js
// Halfway to next pitch (minus 1 to avoid overlap)
high = Math.floor((midiNote + pitchMidis[index + 1]) / 2);
```

The comment already described the correct behaviour. The code never implemented
it. There is no minus 1.

It fires if and only if adjacent sampled pitches are an **even** number of
semitones apart:

- octave (12): `(48+60)/2` is exactly 54. `floor` gives 54 as the lower
  region's `high`, `ceil` gives 54 as the upper region's `low`. Both claim it.
- fifth (7): `(48+55)/2` is 51.5. `floor` gives 51, `ceil` gives 52. No overlap.

That is the whole explanation for why §E5's `fifth` choice escaped and why
`octave`, the default in `requestFullVI`, did not. §F7 measured the symptom
without accounting for it; this is the account.

## H2. The fix

`Math.ceil((midiNote + next) / 2) - 1`, in **two** places:

- `src/services/sfzGenerator.js` `calculateKeyRange`
- `src/services/dsPresetTemplater.js` `dsKeyRange`

The second is a hand-copied duplicate labelled "Same logic as
sfzGenerator.calculateKeyRange". Fixing only the first would have left every
Decent Sampler export wrong, and that divergence would be far harder to find
later than it was to fix now. Both now carry a note saying they are duplicated.

Identical to the old `floor` for odd intervals, so **existing fifth-sampled
instruments are byte-unchanged.**

## H3. Verified three ways

1. Arithmetic across intervals 3, 4, 7, 12, an irregular set, and the
   single/two-pitch edge branches: overlaps eliminated, **no gaps introduced**,
   odd intervals bit-identical.
2. End-to-end through the real `generateXYMorphSFZ`, not a reimplementation.
3. **By `sfz-probe dump` on the regenerated SFZ pointed at the real samples**:
   `49 keys match 5 regions`, none matching 10, 20 regions, zero unknown
   opcodes. Before: 46 at 5, 3 at 10.

The tool built to answer Task 2 turned out to be the regression test for a bug
in a different repository. Worth remembering when the next "should this be
keepable" question comes up.

## H4. Consequence for the plugin

The worst-case voice count drops from 10 per key to 5 (§G4). The engine's
polyphony budget should assume 5, not 10 — but only for artefacts regenerated
after this fix. **Every XY artefact currently on disk still has the overlap**,
including the one in `data/vi-artifacts/ymn5rj11wx4kuhk/`. Regenerating them is
a separate decision.

---

# I. TASK 3 — THE PLUGIN ENGINE — session of 2026-08-27 (third)

The plugin plays. `SamplerEngine` exists, the archive is no longer dropped, and
the AU renders audio under `auval` at every sample rate it tests.

**The headline is a prediction failing in the useful direction.** The open
question flagged as "most likely to block Stage 1" — release-build cold-open
time — is not a problem, and the Debug numbers that made it look like one
overstated it by roughly two orders of magnitude.

## I1. The archive is no longer dropped, measured rather than asserted

Every prior session recorded "nothing in `src/` references sfizz, so the linker
drops the archive" as a reasoned claim. It is now a measurement, before and
after:

| | before | after |
|---|---|---|
| `nm Synth.is \| grep -c sfizz` (Debug AU) | **0** | **17** |
| Debug AU binary | 34.3 MB | 40.5 MB |

The Debug archive is 37.5 MB and the binary grew by 6.2 MB, so the linker is
still discarding most of it correctly. Only what the engine references is
pulled in.

## I2. Release cold-open — the Stage 1 blocker is not one

`release` could never have answered this: it inherits `stage0`, which sets
`KROMOSYNTH_BUILD_SAMPLER=OFF`, and it also sets `KROMOSYNTH_UNIVERSAL_BINARY=ON`,
which `cmake/Sfizz.cmake` hard-errors on (§E2). **The question was
unmeasurable, not unmeasured.** Added `sampler-release`: Release, sampler ON,
native arch, separate build dir.

| | Debug | Release |
|---|---|---|
| `libsfizz_internal.a` | 37.5 MB | **3.8 MB** |
| AU binary | 40.5 MB | **12.6 MB** |
| `loadSfzFile`, cold | 44.9 ms | **31.9 ms** |
| `loadSfzFile`, warm | 19.7 ms | **6.0–7.6 ms** |
| full `auval` wall clock | 2.96 s | **0.91 s cold, 0.32 s warm** |

Loading a 42-region, 15 MB artefact costs single-digit milliseconds warm. The
reason is `config::preloadSize` — sfizz reads the first 8192 frames of each
sample and streams the rest, so a 42-sample instrument preloads about 2.75 MB,
not 15 MB.

**The earlier 641–1661 ms figures in this session's planning were a bad
proxy.** They were whole-process wall clock for `sfz-probe`, which is dominated
by process startup and the probe's own 255-path introspection. Measuring the
load from inside the thing that does it gave a number 100× smaller. A proxy
that shares no code with the thing it stands in for is not a measurement of it.

## I3. `renderBlock` renders silence, twice over, with no Release diagnostic

Found by reading, before writing the engine, and it is why the render is
chunked rather than passed straight through.

`Synth::renderBlock` zeroes the output buffer first (`Synth.cpp:1102`), then
asks `BufferPool::getStereoBuffer(numFrames)`. `BufferPool` sizes its buffers
to exactly the last `setBufferSize` and returns an empty holder when asked for
more (`BufferPool.h:133-136`). `renderBlock` then returns at
`Synth.cpp:1130-1133` behind a bare `DBG()`, which is compiled out in Release.

The same trap a second time: `setSamplesPerBlock` carries
`ASSERT(samplesPerBlock <= config::maxBlockSize)` with `maxBlockSize` 8192
(`Config.h:25`), and `ASSERT` is also a no-op in Release
(`utility/Debug.h:60`).

So an oversized block is a silent instrument with an empty log, by two
independent routes. The engine chunks at `min(hostBlock, preparedBlock, 8192)`.

**Verified rather than assumed.** The first auval run reported
`max_chunks=1` — the loop had never looped, so the defence was untested code
guarding an untested failure. Temporarily forcing the prepared block to 64 and
rerunning gave `max_chunks=64` against a 4096-frame host block, with auval
still passing and audio still rendering. Reverted; the finding stays in the
source comment.

That experiment also showed **chunking is not bit-identical**: the same auval
render peaked at 0.157102 unchunked and 0.157467 at a forced 64. sfizz smooths
controls per block, so a smaller block is finer granularity. Inherent to any
block-size change rather than a defect, and the argument for chunking at the
prepared size rather than at some fixed small one.

## I4. `suspendProcessing` IS the OFF barrier — read from JUCE, not assumed

The open question was: `setSampleRate` and `setSamplesPerBlock` are documented
OFF, JUCE calls `prepareToPlay` before `processBlock`, but hosts change sample
rate mid-session — what is the correct guard?

`suspendProcessing(true/false)` is sufficient, for both our formats at this
JUCE pin:

- Both wrappers hold `AudioProcessor::getCallbackLock()` across the **whole**
  `processBlock` call and test `isSuspended()` **inside** that lock
  (`juce_audio_plugin_client_AU_1.mm:2189-2203`,
  `juce_audio_plugin_client_VST3.cpp:3714-3735`).
- `AudioProcessor::suspendProcessing` takes the same lock
  (`juce_AudioProcessor.cpp:583-587`).

So it cannot return while a block is in flight, and no later block can enter
our code until the flag clears. The same lock supplies the happens-before edge
that makes the engine pointer swap legal without atomics.

One AU path calls `prepareToPlay` while **already holding** that lock
(`AU_1.mm:804-809`, the offline-render property setter).
`juce::CriticalSection` is a recursive pthread mutex
(`juce_SharedCode_posix.h:42`), so this re-acquires rather than deadlocking —
and on that path the audio thread is already excluded by the same lock, so it
does not need to be a barrier.

**Held as a source reading, not a measurement.** It is a statement about these
two wrappers at JUCE 9.0.1, and a host that changes sample rate mid-session has
not actually been observed doing it here.

## I5. `auval` exits 0 for a completely broken instrument

Pointed `KROMOSYNTH_SFZ_PATH` at `tools/sfz-probe/testdata/negative-control.sfz`
and rebuilt. The F11 guard fired exactly as designed:

```
sampler_load ok=0 regions=0 voices=64 ms=1.6
  detail="parsed, but every region was culled: check default_path ..."
```

and **auval exited 0 anyway**, with every test passing.

This is worth stating plainly because it bounds what the Task 1 and Task 3
auval results mean. `auval` validates the AU *contract* — properties, formats,
sample rates, channel configurations, render without crashing. It does not
validate that the instrument makes a sound. A VI artefact with a broken
`default_path` would pass every automated check in this repository and be
silent in the DAW. The `sampler_load` and `sampler_render_stats` log lines are
the only things that catch it.

## I6. The render did happen — but only after the diagnostic was fixed

The first auval run under the finished engine reported
`blocks=0 frames=0 peak=0.000000`, which reads exactly like the F11 failure
surviving to the audio thread.

It was the diagnostic. `logRenderStats` had a once-only guard, and auval calls
`releaseResources` during its property and configuration passes long before it
renders anything, so the single line it emitted latched at zero for a plugin
that went on to work correctly. Removing the guard and logging cumulatively
gives the real answer:

```
sampler_render_stats reason=destructor blocks=24 frames=18633
  peak=0.157102 max_host_block=4096 max_chunks=1
```

**Sixth instance across five sessions of the standing rule.** A diagnostic that
reports the exact failure it was built to detect, for a healthy build, is worse
than no diagnostic — it would have sent the next hour into sfizz. Caught only
because the rule says to check the instrument first.

Both artefacts render: RR peaks at 0.157102, XY at 0.151058, 20 regions, load
27.0 ms Debug.

## I7. What the engine does, and the three things that are deliberate

`SamplerEngine`, one concrete class, no abstraction — there is still only one
engine (top-level `CMakeLists.txt`), and the Faust engine is still gated on
`EXPERIMENT_01`.

- **The engine does not own the OFF barrier; the processor does.** The barrier
  is a JUCE concept, and an engine reaching up into `suspendProcessing` would
  be harder to test and to reuse.
- **The `sfz::Sfizz` instance is created lazily on the first `prepareToPlay`,
  not in the constructor.** Hosts instantiate during scans and time out doing
  it. This keeps the scan footprint at exactly what Task 1 measured and makes
  every millisecond sfizz costs attributable to activation. Given I2, this now
  looks like more caution than the numbers require — keeping it anyway, because
  the numbers are for two artefacts on a warm page cache.
- **A failed load leaves `loaded_` false, so the caller retries on every
  `prepareToPlay`.** A failed load of a culled file costs 0.7 ms, and an
  artefact that appears later is then picked up. The cost is a repeated log
  line, which the negative-control run shows as three identical lines.

MIDI translation is deliberately dumb and complete. Every CC goes through
verbatim, because sfizz implements all-sound-off, all-notes-off and
reset-all-controllers itself inside `cc()` (`Synth.cpp:1485-1492`) — filtering
would break panic handling. Two consequences: sfizz's API has **no channel
argument anywhere**, so the instrument is omni by construction; and §F8.3's
injected cc7/cc10/cc11 bindings now move when the host moves them.

`KROMOSYNTH_SFZ_PATH` is a cache variable with a **configure-time** existence
check, deliberately not unified with the harness's argv (§F1). Configure-time
because the runtime symptom of a typo is an instrument that loads and plays
silence — the same symptom as I5, and indistinguishable from it in a DAW.

## I8. Still open after this session

- **`getTailLengthSeconds()` returns 0.0 and is now wrong.** Both artefacts
  declare `ampeg_release=0.3` with `ampeg_release_oncc73=8`, so the real tail
  runs 0.3 s to 8.3 s depending on CC73. A host that believes 0.0 truncates
  release tails on bounce or freeze — exactly the artefact §C's A/B trap would
  surface as "the plugin sounds wrong compared to the audition". Left wrong
  with the reasoning in the source. The Stage 1 fix is to read `ampeg_release`
  and its CC depth through the introspection API **at load time**, which is
  legal inside the suspend window where no RT call can be in flight.
- **VST3 is entirely untested with audio.** `auval` is AU-only. §A3.1c found
  the formats diverge on UI layout; whether they diverge on audio is unknown.
  This is the largest untested surface left.

  **Correction, same session:** an earlier draft of this line said "and there is
  no VST3 host on this machine". That is false, and §A3.1c three sections above
  says so — Reaper 7.79 and Live 12 Beta were both driven in VST3 during Task 1,
  and Live 11 Suite is also installed. The true statement is narrower: there was
  no known way to drive one *without a human at the keyboard*. That is also
  false. `REAPER -renderproject file.rpp` renders and exits, so an AU-versus-VST3
  audio comparison is automatable in a host already in the matrix, with no new
  dependency. Recorded rather than quietly edited because the error was reading
  past a finding in this same document.
- **Voice count under a real chord in a real host.** Left at sfizz's default 64
  (`Config.h:37`), which is about twelve notes at §G4's five voices each.
  `setNumVoices` deliberately not called: voice stealing is audible and that
  decision should follow a measurement.
- **Does it sound right, or only measure right?** Everything in this section is
  a number. Nobody has listened to it in a DAW.
- **Signing** (§A2) is still load-blocking on Apple hosts and still unaddressed.

---

# J. VALIDATION AND THE FIRST RENDER — session of 2026-08-27 (third, continued)

Four of §I8's open items closed. The plugin has now been driven by two
validators and one real DAW, and there is audio on disk.

## J1. AU and VST3 do not diverge on audio

`pluginval` 1.0.4 (installed this session), strictness 5 and 10, GUI tests
skipped. Both formats **SUCCESS**, zero failures, across 3 sample rates x 5
block sizes x sub-block variants — 33 `prepareToPlay` calls per run, one load,
986 blocks rendered at strictness 10.

The first comparison looked like divergence: VST3 peaked at 0.055314, AU at
0.062567, on identical block and frame counts. **It was pluginval's
randomisation.** With `--random-seed` pinned:

| seed | VST3 peak | AU peak |
|---|---|---|
| 0x1234 | 0.056927 | **0.056927** |
| 0x9999 | 0.045847 | **0.045847** |

Identical to six decimals at two seeds. Third instance this session of the
instrument rather than the plugin being the thing that misbehaved.

**This also partly settles §I4.** The `suspendProcessing` barrier was a source
reading; pluginval changes sample rate and block size repeatedly across a live
instance, which is the scenario it guards, and nothing failed. Not proof — the
guard protects against a race, and a race not firing is weak evidence — but it
is the first evidence of any kind.

One real AU/VST3 divergence found, and it is not audio: at strictness 10 the AU
reports `!!! WARNING: Current program is -1`, which the VST3 does not. Our
`getCurrentProgram()` returns 0 and the AU wrapper reports -1 through it.
Program handling is a Stage 1 placeholder; noted, not chased.

## J2. Reaper renders headlessly, and the AU project could not be hand-written

`tools/reaper-render/make_projects.py` generates Reaper projects that play
fixed MIDI through the plugin, rendered by `REAPER -renderproject`. Generated
rather than committed as a fixture because a `.rpp` carries absolute paths and
a host-specific plugin identifier, read here out of Reaper's own scan caches
rather than guessed.

**The VST3 leg works.** The AU leg does not, and the failure is instructive.

The first AU render produced a WAV with `peak 0.000000` — an entirely silent
file, which is exactly the §I5 / F11 symptom of a loaded-but-culled instrument.
The plugin's own log said `processor_constructed host=REAPER wrapper=VST3` and
nothing else: **there was never an AU instance at all.** The hand-written
`<AU>` FXCHAIN block was not parsed, so the track was empty.

Five header formats were tried against the log as an oracle — a reliable one,
since a constructed instance always writes `processor_constructed`. All five
produced no plugin instance of any kind. Reaper's AU serialisation is not
documented and the search space is unbounded, so this was stopped rather than
continued.

**Fourth instance this session of the verify-the-diagnostic-first rule, and the
strongest.** A silent render file is indistinguishable from a broken
instrument. Only the plugin's own log separated "the AU produced silence" from
"there was no AU". Without §I6's instrumentation this would have become a bug
hunt in the AU wrapper.

Two smaller things learned the same way: `RENDER_SRATE` in a hand-written
project has no effect (44100 either way), and the invented `RENDER_CFG` blob
means Reaper falls back to defaults for everything the blob does not encode.
The renders are therefore 44100 files of a 48000 plugin run, resampled on
write — which explains the 0.284902 internal peak against 0.290205 in the file,
and is fine for listening but not for a bit-exact comparison.

Getting the AU leg needs a project Reaper saved itself. That is a two-minute
GUI step, not a code problem.

## J3. Velocity layers survive the whole plugin path

The Reaper VST3 render, analysed with §G6's crest-factor method — scale-
invariant, so a gain change leaves it untouched and a sample change moves it.
Four notes at velocities 20 / 60 / 100 / 127, which land in bands v1 / v2 / v3 / v3:

| segment | peak | crest |
|---|---|---|
| vel 20 (v1) | 0.004177 | 0.193114 |
| vel 60 (v2) | 0.038666 | 0.191251 |
| vel 100 (v3) | 0.106672 | **0.190306** |
| vel 127 (v3) | 0.172051 | **0.190306** |
| 4-note chord | 0.289982 | 0.138783 |

Velocities 100 and 127 are **identical to six decimals** — same sample,
different gain — while the three bands give three distinct values. §G6 was
measured offline against bare sfizz; this is the same result through JUCE, the
VST3 wrapper, Reaper's audio graph and a WAV encoder. The plugin path does not
alter what the harness measured.

The XY artefact behaves as its structure predicts: crest constant at 0.375438
across velocities 60/100/127, because it has no velocity layers — only gain
changes. **Unexplained:** its velocity-20 segment reads 0.215197 rather than
0.375438. Not chased.

## J4. `getTailLengthSeconds` fixed, and the fix corrects my own claim

§I8 said the tail runs "0.3 s to 8.3 s". Both ends are right and the emphasis
was wrong. The render shows the signal at **exactly 0.0** from one second after
the last note-off, so the *typical* tail at default CC is 0.3 s; 8.3 s is the
worst case with CC73 held at maximum.

Now read from sfizz at load time via `/region&/ampeg_release` and
`/region&/ampeg_release_cc&`, and cached in an atomic. Returns 8.300 for both
artefacts, matching the values read out of the SFZ by hand.

**This does not break the standing rule about the introspection API.** The
rule's content is that `sendMessage` must not run concurrently with
`renderBlock`. The read happens inside `load()`, which the processor already
runs inside the OFF barrier where no RT call can be in flight — the same
condition that makes the sfz-probe harness safe. The rule is now precise rather
than blanket.

Only positive CC depths are summed. A CC that shortens the release cannot
lengthen the worst case, and subtracting it would under-report the tail, which
is the direction that truncates audio.

**Cost, measured because it would otherwise not have been.** F8.2 records that
the `ampeg_*` CC paths are never nil, so this is 129 queries per region — 5418
for the RR artefact. The first version timed only `loadSfzFile` and would have
reported a load cost that silently excluded all of them. Timed separately:
**0.6 ms**. Negligible, and now visible in the log as `tail_ms`.

## J5. Still open

- **The AU leg of the Reaper comparison** (J2). Needs a Reaper-saved project.
- **Voice count under a real chord in a real host.** The render's four-note
  chord did not misbehave, but `getNumActiveVoices` is not sampled during a
  render, so this is still inferred rather than measured.
- **Nobody has listened to it.** `rr.wav` and `xy.wav` exist; that is not the
  same thing.
- **Signing** (§A2), still load-blocking on Apple hosts, still unaddressed.

## J6. The "headless" renders were not headless, and the AU diagnosis was wrong

Björn reported that Reaper was putting up a modal dialog on every invocation,
which he had been dismissing by hand — including five times during the §J2
probe loop. So every render in §J2 to §J4 was human-attended without my knowing
it. Two consequences, one procedural and one factual.

**Procedural.** `REAPER -renderproject` is not unattended by default. Three
flags fix it, and the third is not cosmetic:

```
-nosplash                  no splash window
-ignoreerrors              do not show errors on load
-splashlog /path/to.log    write the load messages to a FILE, not a dialog
```

**Factual — §J2's conclusion was wrong.** §J2 said five hand-written `<AU>`
formats "all failed to instantiate anything" and inferred the block was not
being parsed. The splashlog says otherwise:

```
Project: loading t1 FX: AUi: Synth.is: Synth.is
Project: offline t1 FX: AUi: Synth.is: Synth.is     [1914 ms]
```

Reaper **parses the block, resolves the name, attempts the load, spends 1.9
seconds on it, and then shelves the FX offline.** That is a completely
different failure from "the syntax is wrong", and I had no way to see it
because the evidence was in a dialog on Björn's screen rather than on stdout.

The control that settles the reading — and which should have been run first,
again — is the VST3 project that demonstrably works:

| | message | time | instance constructed |
|---|---|---|---|
| VST3 | `Project: loaded t1 FX` | 1316 ms | **yes** |
| AU | `Project: offline t1 FX` | 1914 ms | no |

`loaded` versus `offline` is the discriminator, and neither is visible in the
rendered WAV.

**Cause, partially identified, not fixed.** Reaper keeps two AU caches:
`reaper-auplugins_arm64.ini`, which holds display names, and
`reaper-auplugins_arm64-bc.ini`, which holds the four-character
type/subtype/manufacturer codes needed to actually instantiate a component.
Our plugin is in the first (`Synth.is: Synth.is=<inst>`) and **absent from the
second**. Reaper knows the name and not how to load it.

Adding a hand-computed line for `aumu`/`Krm0`/`Sthi` — codes confirmed against
`auval` — changed the failure from 1914 ms to **10 ms**, so the entry is read
and the diagnosis is in the right area, but it still shelves the FX. The `-bc`
format has fields still being guessed at. The edit was reverted and the file
verified byte-identical to its backup; guessing at another application's
config on someone else's machine is where this stops.

**The fix is a Reaper-saved project**, which populates both caches properly.

**Also noted:** Reaper additionally warns that a previously-selected audio
device (AirPods) is unavailable. Renders still complete, since offline
rendering does not need the device, but §A3.1e already flagged that this
machine's audio device configuration is unusual, and a host whose device is
missing is not a clean measurement environment for anything realtime.

**Fifth instance this session of the verify-the-diagnostic-first rule, and the
first one where the missing evidence was not on my screen at all.** Worth
generalising: when a tool is driven non-interactively, its interactive output
channel still exists and is invisible. Ask what the human sees.

## J7. The dialog, screenshotted — two invented project tokens, and a resampler

Björn screenshotted what Reaper had been showing. Two dialogs, and both matter.

**"Project Load Warning: Project tokens not recognized: RENDER_CHANNELS,
RENDER_SRATE."** Both were invented. §J2 recorded that `RENDER_SRATE` "has no
effect"; the truth is that it is not a project token at all and Reaper was
saying so on screen the whole time.

Note the trap for next time: **both strings are present in the Reaper binary**,
so `strings | grep RENDER_` finds them and looks like confirmation. They exist
in some other scope. Grepping a binary for a token proves the token exists
somewhere, not that it is valid where you are about to write it.

The real control is `RENDER_FMT`, whose **first field is the render sample
rate** (0 meaning "follow the project"). And `SAMPLERATE`'s second field is a
force flag: at 0 Reaper follows the audio device, which on this machine is
missing, so the rate was an arbitrary fallback. Corrected to
`SAMPLERATE 48000 1 0` and `RENDER_FMT 48000 2 0`.

**This removes a resampler that was sitting inside every measurement.** Before,
the plugin ran at 48 kHz and Reaper wrote a 44.1 kHz file, and §J2 explained
the resulting peak mismatch as resampling without treating it as a defect. With
the rates matched:

| | plugin internal peak | file peak |
|---|---|---|
| before (48k plugin, 44.1k file) | 0.284902 | 0.290205 |
| after (48k throughout) | 0.285012 | **0.285012** |

Exact to six decimals. The render harness is now transparent, which it needed
to be before anything is compared through it. The §J3 velocity-layer result
survived the resampler because crest factor is scale-invariant, but that was
luck rather than design.

**"Error opening CoreAudio device: AirPods Pro."** Separate dialog, with a "Do
not show this window again" checkbox. Offline rendering does not need the
device, so renders complete — but combined with the `SAMPLERATE` force flag
above, a missing device was silently choosing the project sample rate.

**Also visible in the screenshot: the title bar reads "REAPER v7.79 —
EVALUATION LICENSE."** Relevant only because an unlicensed Reaper shows a nag
dialog on startup, which is a third thing standing between this harness and
being genuinely unattended. Worth knowing before relying on it in any automated
loop.

**Generalisation, now a standing rule.** When a tool is driven
non-interactively, its interactive output channel still exists and is invisible
from here. Two separate wrong conclusions in this session came from evidence
that was on a screen rather than on stdout. Ask what the human sees, early.

---

# K. LISTENING FOUND WHAT MEASURING MISSED

Björn listened to the renders. His report: *"I hear one sound getting louder 3
times, then a chord, seemingly of the same sound."*

He is right, my §J3 conclusion was wrong, and the evidence had been in front of
me the whole time.

## K1. What §J3 got wrong

§J3 reported the three velocity bands giving "three distinct crest values" and
concluded the velocity layers work. The values were **0.193114 / 0.191251 /
0.190306** — a 1.5% spread. §G6's note 55, the case that motivated the method,
spread 2.8% and 8.3%.

I called them distinct without asking *distinct by more than what*. §G7.3
already established that a statistic compared for equality needs a tolerance;
I applied the statistic and skipped the tolerance. A 1.5% spread is not
evidence of three different samples — it is evidence of three nearly identical
ones, which is exactly what it turned out to be.

**The plugin was never the problem.** It switches samples correctly at exactly
the declared boundaries, as §G6 measured. The samples it switches between are
the same sound.

## K2. The measurement, done properly

For each pair of velocity layers, fit the best scalar gain `k` minimising
`|A - kB|`, then report the residual energy. Near 0% means "same waveform,
different gain only" — which is what a listener hears as one sound getting
louder.

All 21 velocity-layer pairs in the RR artefact, across all 7 keycentres:

| key | v1 vs v2 | v1 vs v3 | v2 vs v3 |
|---|---|---|---|
| 048 | 1.23% | 3.37% | 0.71% |
| 055 | 1.93% | 5.33% | 1.10% |
| 062 | 1.39% | 3.73% | 0.75% |
| 069 | 1.56% | 4.18% | 0.83% |
| 076 | 2.02% | 5.32% | 1.03% |
| 083 | 2.03% | 5.32% | 1.03% |
| 084 | 1.76% | 4.64% | 0.91% |

**Median 1.76%, maximum 5.33%.** Every velocity layer in the instrument is the
same waveform at a different gain. The feature is decorative, exactly as §G5
found for round robins.

## K3. Root cause — velocity barely reaches the synthesis, and normalisation
finishes the job

Two independent mechanisms, both verified:

**1. Velocity enters the CPPN in exactly one place, and is commented out in
another.** `kromosynth/util/gpu-functions.js`:

```js
line 16:  return ((rangeFraction * 2) - 1); //* this.constants.velocity;
line 27:  return extraInput * this.constants.velocity;
```

It survives only as a scalar on one input signal to the network. That is enough
to perturb the output slightly — which is why the residuals are 1-5% rather
than 0% — and nothing like enough to make a velocity layer.

**2. Every rendered sample is normalised to full scale.** All 42 WAVs in the
artefact peak at **exactly 0.999969** (32767/32768). Whatever level difference
velocity did produce is erased before the sample reaches the SFZ. The SFZ's
`amp_veltrack` then re-applies a pure gain at playback.

Net effect: one sound, three gains. Which is what was heard.

## K4. §G5 explained — round robins are asked for the same thing twice

§G5 found 10 of 21 round-robin pairs byte-identical and left it "unexplained",
leaning toward "something conditional in the render path".

It is not conditional. `kromosynth-vi/src/services/sampleMatrix.js` builds
`renderParams` inside the round-robin loop, but `rr` only reaches it through
`layerParams`, which is null when there are no per-sound layer parameters. With
`layerParams` null, `renderParams` for `rr=1` and `rr=2` is **identical**: same
`noteDelta`, same `velocity`, same `duration`, null geometrics.

The render service is handed the same genome and the same parameters twice.
Byte-identical output is the *correct* result. The 11 pairs that do differ
differ through non-determinism in the render path, not intent — which also
explains §G5's "not uniform across velocity bands" observation, recorded there
as evidence for a conditional path. It is evidence of non-determinism instead.

Noted in passing, not the active bug: `velocity: layerParams?.velocity || vel.velocity`
uses `||`, so a legitimate layer velocity of 0 would silently fall through to
the default.

## K5. Why the instruments needed a listener

Three separate measurements said this instrument was fine:

- §F3: 42 regions, zero unknown opcodes, nothing rejected.
- §G6: velocity switching at exactly the declared boundaries.
- §J3: velocity layers surviving the whole plugin path.

All three are true. None of them asked whether the samples differ, because
every one of them was a test of the *sampler*, and the sampler is correct.
Sample content was never in the test set.

Björn heard it in about ten seconds.

**Standing rule.** A measurement chain that only ever tests the playback path
cannot detect an instrument whose content is degenerate. Every statistic here
was scale-invariant *by design* — chosen precisely so gain changes would not
confuse it — which made the whole apparatus structurally blind to "same sound,
different gain". When every instrument in a chain is invariant to X, nothing in
the chain can see X.

## K6. Also: the test file is badly shaped

"not for four seconds" — the render is 10 s with the last note-off at 5 s and a
0.3 s release, so roughly 4.7 s of trailing silence. That is the test MIDI's
fault. Worth fixing before the next listen, along with holding notes long
enough to hear the body rather than the attack.

## K7. Consequences

- The plugin work in §I and §J stands. Nothing here is a plugin defect.
- **Velocity layers and round robins are both hollow in `kromosynth-vi`
  output.** Two of the three variation axes the VI generator advertises
  currently produce one sound. This is a generator finding and it is more
  consequential than anything in the plugin.
- §E6.2/§H fixed the *key range* overlap. This is a different and larger
  problem in the same generator.
- Every VI artefact on disk is affected.

## K8. CORRECTION — §K3 and §K4 over-claimed, and §K7 built a plan on it

Björn's response to §K, and he is right on both counts.

**On velocity (§K3).** Velocity *is* a signal into the CPPN networks and *can*
affect articulation — "sometimes it sounds like hitting a string at different
intensities". So the mechanism is intended and working, and
`gpu-functions.js:16` being commented out is a design choice, not a regression
I had discovered. Further, normalising each sample and letting `amp_veltrack`
supply the level is **standard sampler practice**, not a bug that "erases" the
velocity difference.

What survives is much narrower and still worth having: **for this particular
genome, velocity produces about 2% timbral change**, so its three velocity
layers are perceptually one sound. That is a property of the sound, not a fault
in the pipeline. Some genomes will articulate strongly under velocity; this one
does not.

**On round robins (§K4).** The web app has performance pads that can change
network weights, and per-RR-layer settings can be assigned. That is exactly
what `layerParamsPerSound` carries. It is an optional request field defaulting
to `null` (`vi.js:158`), and the artefact under test was generated without it.

So §K4's "the render service is asked for the same thing twice" is accurate as
a description and **wrong as a diagnosis**. Nothing is broken. Round robins
were not given variation because none was requested. §G5's byte-identical pairs
are the correct output for that request.

The residual product question — whether accepting `roundRobins: 2` without per-
layer params should silently produce duplicates — is a footgun worth a thought,
not a defect.

**On scope (§K7), which is the real error.** Björn: *"aren't we just playing the
files as they are defined by the SFZ?"* Yes. The plugin's contract is to play
what the SFZ points at. Whether those files differ from each other is a VI
generation concern and was never a plugin question.

§K correctly attributed the observation away from the plugin and then
incorrectly escalated it into "two of the three variation axes are hollow" and
a recommendation to change project priorities. **A finding about one test
artefact was generalised into a finding about the generator, and then into a
plan.** The measurement in §K2 stands; every inference stacked on top of it
did not.

**Standing rule.** Attributing a finding outside the component under test is
the *start* of the reasoning, not the end. Before escalating it into a defect
in that other component, check whether the behaviour is configured, intended,
or documented there — and prefer asking the person who built it over inferring
from its source. Two of the three §K conclusions would have been avoided by one
question.

**What actually remains actionable**, and it is small: a VI whose layers are
near-identical is larger than it needs to be for no benefit. A cheap residual
check at generation time — fit a gain between layers, report the leftover —
could warn "these 3 velocity layers are 98% identical, 1 would do". That is a
genuine improvement and it is nobody's bug.

---

# L. SIGNING — infrastructure built, certificate outstanding

§A2 established that signing is load-blocking on Apple hosts rather than a
Stage 1 formality. Everything except the certificate is now in place.

## L1. The target configuration, measured off working plugins

§A2 asked that its own inference — that Developer ID + notarisation avoids the
"Lower Security Settings" dialog — be re-verified rather than trusted. Read off
the two commercial samplers already installed on the dev machine:

| plugin | signature | hardened runtime | entitlements | `spctl -t install` |
|---|---|---|---|---|
| sforzando | Developer ID (Plogue) | **no** | none | accepted, Notarized Developer ID |
| DecentSampler | Developer ID (Decidedly) | yes | none | accepted, Notarized Developer ID |
| **ours, before** | ad-hoc | no | none | **rejected** |

Three things follow.

**1. §A2's inference is confirmed at the assessment level.** `spctl` rejects
ours and accepts both of theirs. That assessment is what produces the dialog.

**2. Neither commercial plugin ships any entitlements.** This is the finding
that saves work: the hardened runtime needs no exceptions for a plugin of this
shape, *including the webview*, because WKWebView's JIT runs in a separate
WebContent process and is not ours to declare. There is deliberately no
entitlements plist in this repository. If one ever becomes necessary that is a
finding to record, not a file to add quietly.

**3. sforzando is notarised without the hardened runtime; DecentSampler has
it.** Apple has required hardened runtime for new submissions since 2019, so
sforzando is a legacy allowance and DecentSampler is the model to copy. Reading
only sforzando would have produced the wrong conclusion — a second reference
was worth the thirty seconds.

## L2. Ordering was the part that could have failed silently

`juce_add_plugin` attaches two POST_BUILD steps to each format target, in this
order (`JUCEUtils.cmake:1044`, `:1055`):

1. `_juce_adhoc_sign` — the "Replacing invalid signature with ad-hoc signature"
   line in the build log
2. `_juce_copy_dir` — copy to `~/Library/Audio/Plug-Ins/...`

Anything added afterwards runs after **both**. So signing only the build
artefact leaves the **installed** copy carrying JUCE's ad-hoc signature — and
the installed copy is the one every host loads. The build log would show
successful signing, `codesign --verify` on the build tree would pass, and the
plugin would still be refused by GarageBand.

`cmake/Signing.cmake` therefore re-signs the artefact and then re-copies it,
reusing JUCE's own `copyDir.cmake` so the two cannot drift.

**Verified rather than assumed.** With `KROMOSYNTH_SIGN_IDENTITY="-"` (ad-hoc,
but through our path rather than JUCE's), the *installed* bundles moved from
`flags=0x2(adhoc)` to `flags=0x10002(adhoc,runtime)`. The hardened runtime
reached the copy the hosts load, which is what the ordering question was about.

**`auval` still passes with the hardened runtime enabled.** Worth checking
separately: the hardened runtime restricts dyld behaviour and JIT, and a plugin
that loads fine unsigned can fail under it.

## L3. What is built

- `cmake/Signing.cmake` — `KROMOSYNTH_SIGN_IDENTITY` (empty default, so current
  behaviour is unchanged) and `KROMOSYNTH_HARDENED_RUNTIME` (ON). Signs with
  `--timestamp --options runtime`, then verifies with
  `--verify --deep --strict` at build time, because a signature `codesign`
  accepts but strict verification rejects is a real failure mode and finding it
  at notarisation costs an upload round trip.

  Deliberately **not** `--deep` when signing. Apple documents it as deprecated,
  and it applies the outer bundle's flags to nested code. These bundles have no
  nested code — sfizz and JUCE are both static — so a single non-recursive
  signature is correct and honest about that assumption.

- `scripts/notarize.sh` — zip via `ditto -c -k --keepParent` (the `zip` command
  mangles bundle symlinks), submit, staple both bundles, then verify with
  `spctl`. Pre-flight rejects an ad-hoc or non-hardened build **before**
  uploading; confirmed working against the current ad-hoc build.

  Stapling is the step that gets skipped. Without it every load needs a
  successful network check against Apple; a stapled bundle carries its own
  ticket. For a plugin loaded during a DAW's startup scan, unstapled on a flaky
  network means a plugin that intermittently fails to appear.

## L4. Outstanding — one certificate

Björn has an Apple Developer Program membership (used for iOS App Store
publishing). **A Developer ID Application certificate is a different
certificate from the Apple Distribution one used for the App Store**, though
both come with the same membership. It needs creating and installing on this
Mac:

```
Xcode > Settings > Accounts > Manage Certificates > + > Developer ID Application
```

Only the team's Account Holder or an Admin can create one. Then:

```
security find-identity -v -p codesigning        # confirm it landed
cmake --preset sampler-release -DKROMOSYNTH_SIGN_IDENTITY="Developer ID Application: NAME (TEAMID)"
cmake --build --preset sampler-release
xcrun notarytool store-credentials "kromosynth" --apple-id ... --team-id ... --password <app-specific>
scripts/notarize.sh
```

**And then test somewhere else.** §A2 records that GarageBand on this machine
has been granted a permanent security exception, so a clean load here proves
nothing about a fresh install. This is the one part of the signing question
that cannot be answered on the development machine.
