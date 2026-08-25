# Stage 0 findings

Status: **template — nothing tested yet.** Fill in from real hosts.

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

| | Logic | Live | Reaper | Bitwig |
|---|---|---|---|---|
| AU validates (`auval -v aumu Krm0 Sthi`) | | n/a | n/a | n/a |
| VST3 scan succeeds | | | | |
| Editor opens, page renders | | | | |
| Survives close → reopen (page state intact?) | | | | |
| Survives host project save → reload | | | | |
| Resize: page reflows, no clipping | | | | |
| Typing into a web input reaches the page | | | | |
| Host steals keyboard focus (spacebar → transport?) | | | | |
| Two instances in one project | | | | |
| Host still alive after removing the plugin | | | | |

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
