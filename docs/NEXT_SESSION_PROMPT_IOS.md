# Next session: the chrome, and a loop that no longer needs the phone

Rotated 2026-09-10. Previous: `NEXT_SESSION_PROMPT_IOS.2026-09-09.md`.

Read `docs/AUV3_IOS_FINDINGS.md` first — C0 through C34, newest first. That file
is the record. Do not re-derive it from commit messages.

---

## 1. The single most useful thing to know

**There is now a simulator path, and UI work should not touch the phone.**

Yesterday's loop was: build, ask Björn to force-quit AUM, install, ask him to
look, discover nothing installed, repeat. It cost most of a session and he said
so plainly. The simulator removes all of it.

```
cmake -S . -B build/ios-sim -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=ATA8MQ4L7K \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
  -DKROMOSYNTH_BUILD_FAUST=ON -DKROMOSYNTH_BUILD_SAMPLER=ON \
  -DKROMOSYNTH_WASM3=ON \
  -DKROMOSYNTH_WASM3_SOURCE_DIR="<repo>/../kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/probe/out/wasm-runtime/wasm3/source" \
  -DKROMOSYNTH_WEB_APP_URL=https://synth.is \
  -DKROMOSYNTH_VI_SERVICE_URL=https://vi.synth.is \
  -DKROMOSYNTH_WEBVIEW_PROBE=ON -DKROMOSYNTH_BROWSER_INSET=0
```

`build/ios-sim` is already configured this way and builds clean — wasm3 and
sfizz both compile for the simulator. Then:

```
cmake --build build/ios-sim --config Release --target KromosynthPlugin_Standalone
SIM=AC0AA08E-1F64-4E5D-931D-9D505753118B          # iPhone 17 Pro, 27.0, booted
xcrun simctl install $SIM build/ios-sim/src/KromosynthPlugin_artefacts/Release/Standalone/Synth.is.app
xcrun simctl launch $SIM is.synth.plugin
xcrun simctl io $SIM screenshot /tmp/shot.png
xcrun simctl get_app_container $SIM is.synth.plugin data   # the log lives under Library/Logs/Synth.is
```

**Seeing the screenshot yourself, economically.** A full frame is too large to
pull into context. Crop to the region in question and JPEG it:

```
magick /tmp/shot.png -crop 1206x230+0+0 +repage -resize 560x -strip -quality 60 /tmp/bar.jpg
```

That is about 5,000 base64 characters for the chrome bar strip — cheap enough
to iterate on. ImageMagick and PIL 10.1.0 are both installed. 35 simulators are
available including an iPhone 13 matching Björn's device.

**What the simulator cannot cover, so do not try:** AUM does not exist there.
The icon, AUM's 256-frame block delivery, the AU parameter list and
GarageBand's 736x242 letterbox all need the phone. Those are measurement and
host-integration questions. Everything about appearance and navigation is
simulator work.

**Unfinished plumbing, and it is the first thing to do.** The app was installed
and launched once (pid 77509) but the screenshot showed a launch/home texture
rather than our UI, and subsequent `simctl launch` calls started returning
empty. Get the app frontmost and confirm the chrome bar renders before building
anything else. This is one round of plumbing, not an obstacle.

---

## 2. What landed on 2026-09-10

Twenty-three commits here, one in `kromosynth-render`, one in
`kromosynth-desktop`. In rough order of consequence:

**The cap question is answered.** `844223d` captures over-budget blocks with
voices and a reconstructed wall clock; `704bd1e` fixes a denominator that had
been wrong the whole time (`getBlockSize()` is the host's declared maximum, AUM
declares 2048 and delivers 256, so every "of 42.67 ms" reading named a deadline
the host never used — the real regime is 5.33 ms); `cb5e763` and `c5b3bc6`
measure the load distribution per voice count. C22 → C27 → C30 → C32.

**The verdict is TOO TIGHT, not too generous** (C32), which is the opposite of
where C22 and C29 pointed for two days. `p50@cap` is 0.3 against a 0.5
threshold. **How much** too tight is deliberately not claimed: it rests on a
one-bucket difference at 0.1 resolution.

**`capForRtf` is untouched and should stay that way** until the slope is
resolved. C32 names what that needs: finer buckets where the distribution
actually lives (everything measured sits under 0.5, so 21 uniform buckets waste
most of their range) and readings at 3 and 4 voices, because two points cannot
tell a linear cost from a flattening one.

**The palette is unified** (`28f106a`). `src/Palette.h` is the single source,
generating the shell's `:root` block and exposing the same constants as
`juce::Colour` for native drawing. Read off the web app's actual usage, counted
across its source. **The class names lie:** `blue-600` is teal `#35aaa3`,
`gray-300` is light green, `gray-900` is dark blue. The ramp runs green at the
light end and blue at the dark end.

**One persistent chrome bar** (`8480833`). `SiteBar` → `ChromeBar`, always
present, content swaps beneath it. Five destinations from `NavigationBar.jsx`.
Nothing disabled — Björn corrected an earlier design that greyed Biomes and
locked Garden, and he was right.

**`chrome=host` is finally sent** (`5521287`). `hostDrawsChrome()` tests for
`?chrome=host`; the plugin had only ever sent `?client=synthis-plugin`, so
`App.jsx:1450`'s stand-down had **never once fired** and the site drew its own
nav inside the plugin from the beginning. WP §2 and I both claimed "half of
this work is already done". The code was. The trigger was not.

**iOS scripts that did not exist** (`6390140`, plus `5ae5d2f` `3535fe2`
`030a012` `99ed5ed` `25ed3d5` `3a26581`): `ios-build.sh`, `ios-install.sh`,
`ios-log.sh`, `ios-icon-experiment.sh`. `ios-log.sh` is now **verified** — the
log lives in `appDataContainer` / `is.synth.plugin.pluginAUv3` under
`Library/Logs/Synth.is`, and C16's warning did not bite.

---

## 3. Open, in order

1. **Confirm the simulator shows our UI**, per §1. Everything below assumes it.

2. **Move the nav into HTML, shared with the web app.** The A3.2 probe
   (`0f79e16`, reverted in `5521287`) **answered yes**: two WKWebViews composite
   simultaneously in an AUv3 — the shell's header rendered in a 72px strip with
   synth.is live below it, no blank, no memory kill. Most of the risk was
   already retired, since both browsers have always used
   `withKeepPageLoadedWhenBrowserIsHidden` and two content processes coexist in
   every shipped build.

   So the design is: **the bar lives in the shell's own document**, which never
   navigates, sized to a strip while the site fills the rest. WP §4 rejects HTML
   chrome because it would mean injecting into synth.is or maintaining the bar
   in two documents — it never considered this third option, and its argument
   does not reach it. **Update §4 when this lands.**

   That buys the real logo SVG (`synthis_S_logo.svg`), the lucide icons, Funnel
   Sans and the web app's own metrics, and it fixes the truncated
   "Instrume…" that icon-only-below-`sm` handles properly.

   Björn asked for this directly. The current native buttons are hand-rolled
   and look it.

3. **The Output and Release panel** (WP §9). Still the only fix for something
   audible: `#MKV9X` distorts on a chord and its preview did not. The mechanism
   is documented in `PluginProcessor.cpp`'s own comment — one note at full
   velocity reaches full scale, *that is* preview parity, so chords do not fit.
   The control exists as a host parameter with no UI, buried in AUM's generic
   list. **Do the nav first** so this is built once, in whichever technology
   wins.

   Two candidates for the distortion and one test separates them: one note
   clean and two distorted means summing; two clean and three distorted means
   voice stealing on the 2-voice cap. Not yet run.

4. **Resolve the load slope** (C32), then and only then touch `capForRtf`.

5. **`p50 <= 0.5` has a user-visible corollary worth recording**: a 1-voice cap
   makes an instrument monophonic, which is a categorical loss rather than a
   conservative one. Björn hit this. The asymmetry — occasional overruns AUM may
   absorb, against no chords at all — is not captured by C31's bar.

6. **Deploy `94e7471`** in `kromosynth-render` whenever the hub next restarts.
   Committed, not pushed. New artefacts keep being minted with ladder caps until
   it ships.

7. **Finish consolidating `capForRtf`** (C26). All three copies agree
   numerically; there are still two of them. Giving it a headroom parameter
   touches `FaustEngine.cpp` and sits behind the 120/120 null test.

8. **`WP_EXTRACTION_QUOTA.md` OQ7** — entry or sound. **Not urgent, and I said
   otherwise for most of a session before correcting it.** §7.2's mandated
   implementation, re-download the same entry and never re-render, avoids the
   hazard OQ7 describes. It gates the single-shot surface, which does not exist.

9. **The desktop signed-out test** (C11, C14). Still costs nothing.

10. **Stage D** — the ticket across the process boundary (C2).

---

## 4. The icon: do not pick this up casually

C34 records five dead hypotheses. **Do not propose a sixth bundle hypothesis.**
Everything checkable is checked: 30 catalog renditions at scales 1, 2 and 3,
both idioms, all four plist icon keys present in both bundles with names that
resolve, loose PNGs at correct pixel sizes, `AudioComponentBundle` eliminated in
**both** directions (stripped and pointed at the containing app), and registry
staleness eliminated by a full delete, reboot and single install.

The pattern in C24 through C28 was five rounds of plausible elimination on the
wrong layer, and the control test that settled it — *do other vendors' AUv3s
show icons in this AUM?* — was one glance and came four sessions late. They do.

**The reframe that matters:** the `NEMUR NET EHF.` and `NEMUR NET EHF` entries
survived a full delete and reboot, so they are separate older builds under their
own bundle ids, not stale registrations. Three different JUCE-built AUv3s of
ours across years, none with an icon, while Moog's and Space Mushrooms' work.
That points at JUCE's iOS plist generation, not at this build.

If picked up: read the system log while AUM draws the row. Console.app with the
device attached — `log stream --device` is unsupported by this macOS `log`, and
libimobiledevice is not installed. That would **observe** the failure instead of
guessing at it.

**The device currently has a modified build** with `AudioComponentBundle`
pointing at `is.synth.plugin`. Byte-identical in code, harmless, but not the
plain build. Overwrite it on the next install.

---

## 5. Things I got wrong yesterday, so you do not repeat them

Recorded because every one cost real time and each has a recognisable shape.

**I asserted `chrome=host` was already working**, several times, because the
code existed and WP §2 said so. Nothing failed loudly, because a nav appearing
is what a nav does. *Check that the trigger fires, not just that the handler
exists.*

**I over-tightened the install gate on my own inference.** A comment I wrote
claimed a suspended host is still a refusal because C15's crash was inside
`requestIcon` during a rescan — asserted as though it came from the record. It
did not. Björn was then blocked from installing anything, and a gate the
operator cannot clear is a stop, not a safety measure. `3a26581` splits it: our
appex loaded is absolute, a resident host is overridable with `--force`.

**Three separate times a truncated `grep | head` nearly became a finding** —
`Assets.car` "missing", four unmapped colour literals, `otool`'s filename in a
checksum. C25 is about exactly this and the guards that caught them exist
because of C25. *Write the guard that refuses rather than the check you read.*

**I handed Björn decisions that were mine to make.** He said so: "It is not
clear to me what you need from me." Four of five items I had labelled as his
dissolved on inspection — one was already settled by his own written policy at
`PluginProcessor.cpp:42`. *Arrive with a proposal and a reason.*

**A confirmed prediction from an invalid test is worth nothing.** C29 predicted
the cap would fail on the override clause and it did — and C30 showed the bar
was reading the operating system, since a block with `voices=0` took 195.5% of
the deadline. Booking that as a win would have retired the cap question on a
scheduling hiccup. *Registering a bar in advance is only useful if you will also
throw it away.*

---

## 6. Conventions that held

- Explicit pathspec on every `git add` and `git commit`. `kromosynth-desktop`
  has unrelated genome-inspector WIP; never stage it.
- Commit message files under `/Users/bjornpjo/Developer/`, not `/tmp`.
- Compile with Faust ON in `build/desk-verify`, which matches
  `build/ios-auv3`'s option pair. **`build/sampler` is dead as a control** —
  its cache is from 28 August and fails on `KROMOSYNTH_VI_SERVICE_URL` in
  unmodified code at HEAD.
- `filesystem:edit_file` batches are atomic: one bad `oldText` rejects all of
  them. Box-drawing characters and em dashes must match byte for byte.
- MCP `local-terminal` calls time out around four minutes. Keep sleeps under
  ~100 s and poll a log file in a second call rather than waiting inline.
- `local-terminal` returns a bare `null` when a command hangs. That is not an
  empty result and it means nothing — the same trap as devicectl's `null`.
