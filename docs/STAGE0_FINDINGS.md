# Stage 0 findings

---

# TASK 1 VERDICT — the webview assumption holds, with three caveats

**Tested:** GarageBand only (via `AUHostingServiceXPC`). macOS 26.3.1, Apple
clang 21.0.0, JUCE 9.0.1, AU + VST3, ad-hoc signed Debug build.

**One host is not a matrix.** Everything below is a GarageBand result. Reaper,
Live 12 Beta, Bitwig and Logic remain untested.

## The WP §7 gate

| gate question | GarageBand | |
|---|---|---|
| Does it load? | **YES** | ~1.2 s, React mounts (`rootChildren: 1`) |
| Does it stay loaded? | **YES** | no crashes once our own bug was fixed |
| Does it resize? | **NO** | §A3.1 — cause unresolved |
| Does the host steal keyboard focus? | **NO** | space types a space; transport stays put |
| Does routing work outside a browser tab? | **YES** | `BrowserRouter` on the remote origin |
| Survives window close → reopen? | **YES** | but the page fully reloads — §A3.3 |
| Survives host quit → relaunch? | **YES** | `localStorage` persisted, age=105 s |
| Multiple instances? | **YES** | two processors, distinct pages — §A3.2 |

**The riskiest assumption in the WP survives.** A WKWebView pointed at the
deployed origin loads, renders the real app, keeps state across host restarts,
accepts keyboard input, and runs multiple instances — inside a sandboxed,
hardened Apple host that loads plugins out-of-process.

## Three things that change the WP

**1. Signing is load-blocking, not a Stage 1 formality (§A2).**
GarageBand refused the ad-hoc signed plugin until the user granted a
"Lower Security Settings" exception. §7 schedules signing for Stage 1; for
Apple hosts it belongs earlier, and no real user can ever be shown that dialog.

**2. Audition audio does not go through the plugin (§C).**
Confirmed by probe: the page has its own `AudioContext` (`state=suspended
sr=44100`), separate from the plugin bus. §5's "auditioning inside the plugin is
interaction and free" assumes audition behaves like a plugin. It does not — it
exits the system output, unrouted and unrecordable by the host. This is a
design decision the WP has not made.

**3. The app cold-starts on every window open (§A3.3).**
New page, fresh `sessionStorage`, ~1.2 s reload, all in-memory UI state lost.
`withKeepPageLoadedWhenBrowserIsHidden()` does not help because the host
*destroys* rather than hides. §4's "one UI codebase" premise assumes tab-like
persistence; the plugin window is a cold start. Mitigable by persisting UI
state to `localStorage` (proven to survive), but that is plugin-specific work
the web app does not do today.

## Two things that turned out fine

- **`localStorage` works and persists** (§B2). The `kromosynth-live` precedent
  suggested it would fail in a sandboxed host. It did not. §5's entitlement and
  token design is viable.
- **`crossOriginIsolated: false` is a non-finding** (§B1). Real WKWebView
  behaviour, but only three call sites depend on it and two actively prefer it
  false. No action needed.

## One new constraint discovered

**The embedded-webview User-Agent is detectable** (§B3) — no `Safari`/`Version`
token. OAuth providers block embedded webviews on exactly this signal. §5
already mandates RFC 8252 (system browser + PKCE); this is empirical
confirmation that the design is **forced, not merely preferred**.

## Outstanding before Task 2

1. **Reaper, Live 12 Beta, Bitwig.** Reaper matters most: it loads in-process,
   so it separates "webview in a plugin" from "webview in Apple's XPC sandbox",
   and it will settle whether §A3.1's resize failure is ours or GarageBand's.
2. **Resize cause** (§A3.1) — host limitation or our AU wrapper not advertising
   resizability. Unresolved, and it constrains UI design if it holds broadly.
3. **Release-build cold-open time.** Debug measured 1532 ms cold under `auval`.
   Hosts time out during scans.
4. **No Web Inspector** (§B6). JUCE 9 does not expose `isInspectable`. A known
   ergonomics cost for Stage 1's bridge; decide whether to carry a JUCE patch.

**Recommendation: do not start Task 2 yet.** The gate passes on one host. Reaper
is a 20 MB download and answers two open questions at once.

---

Status: **GarageBand tested — see the Task 1 verdict above. Four hosts
outstanding.**

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
