# Next session: the strip is built, and only the phone can approve it

Rotated 2026-09-11. Previous: `NEXT_SESSION_PROMPT_IOS.2026-09-10.md`.

Read `docs/AUV3_IOS_FINDINGS.md` first — C0 through C35, newest first. That file
is the record. Do not re-derive it from commit messages.

---

## 1. The single most useful thing to know

**The HTML nav is measured in a host and it clears its budget. `ChromeBar` can
go.**

This section said, for one day, that everything turned on one unmeasured number
— the safe-area inset in an AUv3. C36 has it: **0**. The strip is 51pt in AUM
(50 of bar plus the border) against 113 in the standalone, confirmed by two
independent sources, and the `ResizeObserver` never fired twice because there
was nothing to land.

So GarageBand's 736×242 leaves 191pt for content, which is the four-rows-at-44
budget exactly. **GarageBand is a look-and-feel check now, not a viability
gate.** Start at §3.

The useful thing to carry forward instead: **AUM hands the plugin at least five
different rectangles in a single session** — 428×926, 428×879, 428×280, 409×524,
428×574, including one narrower than the screen. Anything that sizes against the
editor cannot read one `editor_resized` line and call it the layout.

The simulator loop is still the right tool for UI work — same `cmake` invocation
below. Three corrections to how to drive it:

- **Poll the log for `page_finished_loading`; do not sleep.** It lands ~6.8s
  after launch. I shot a screenshot at 10s, got a pure white frame, and nearly
  filed the launch screen as a broken stylesheet.
- **Re-derive the data container after every `install`.** Each one produced a
  fresh UUID.
- **`xcode-27-headless` `DeviceInteractionSynthesize` does drive the
  simulator**, despite `skillToTrigger` insisting on a subagent, and despite
  reporting `applicationState: "NotRun"` while the hierarchy dump beside it
  shows the pid running. Web content is a `RemotePlaceholder`, so DOM positions
  are unavailable — compute tap coordinates from the CSS box. `t 80 79` hit
  Feed first try that way. Sessions expire after a couple of calls; restart
  rather than debug.

For the device: `ios-log.sh`'s retrieval path **WORKS** and its header still says
otherwise. `appDataContainer` / `is.synth.plugin.pluginAUv3` answered and
produced `stage0-Synth-AUv3.log`. Its C16 warning should be rewritten to say
which domain was right rather than left as a probe.

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
# iPhone 17 Pro, 27.0, booted: AC0AA08E-1F64-4E5D-931D-9D505753118B
# Literal UDID, NOT a shell variable -- see §6 on local-terminal and bare null.
xcrun simctl install <udid> build/ios-sim/src/KromosynthPlugin_artefacts/Release/Standalone/Synth.is.app
xcrun simctl launch <udid> is.synth.plugin
xcrun simctl io <udid> screenshot /Users/you/Developer/shot.png
xcrun simctl get_app_container <udid> is.synth.plugin data   # re-derive EVERY install; the UUID changes
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

**Unfinished plumbing — CLEARED 2026-09-11.** The app was installed and launched
once (pid 77509) but the screenshot showed a launch/home texture rather than our
UI. It was not a plumbing fault: the app needed `terminate` then `launch` to
come frontmost, and the texture was the LAUNCH SCREEN, caught because
`page_finished_loading` lands ~6.8s after launch and the screenshot went at 10s
with the animation still up. Poll the log, do not sleep. Verified end to end:
the UI renders, and the screenshot economics above hold — a full frame at 300px
wide is ~10 KB, a bar strip at 700px ~7.5 KB.

---

## 2. What landed on 2026-09-11

Two commits, both here.

**`e393a5f` — the nav is in the shell's document.** ChromeBar's hand-rolled JUCE
buttons are replaced by a `<nav>` in the page the shell already serves. Real S
mark on a `/logo.svg` resource route, lucide 0.454.0 path data inline, labels
dropping below 639px instead of truncating to "Instrume…", one Panic instead of
two. `showSite` resizes the shell rather than hiding it — hiding it would take
the chrome with it — and `body.strip` removes everything below the nav rather
than clipping it.

Both taps verified in the log, not inferred: Feed gives `surface_site` then
`/network`; Library gives `surface_shell`. The composite renders with synth.is
live below the strip, `rootChildren:1`, no blank and no memory kill. A3.2's
probe result holds as a layout.

The strip's height is **reported by the page**, via `chromeHeight` and a
`ResizeObserver`. `kStripHeight` is only the bar and only a fallback seed. See
§1 for the number that is still missing.

`kNativeChrome` restores the old bar in one character; both paths compile.

**`081a4fd` — C35.** The four readings that placed the empty strip, the `calc()`
misattribution recorded next to the claim it corrects, the two instruments of
mine that were wrong before the code was, and the deploy skew.

Nothing was touched in `kromosynth-desktop`, so its genome-inspector WIP is
still unstaged.

---

## 2b. What landed on 2026-09-10

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

**Previous rotation's §3.1 (measure the AUv3 inset) PASSED — C36. The deletion it
unblocked is DONE (`21f438a`), and it exposed a defect C37 records: the strip's
`ResizeObserver` was watching the content box, so it could not see the inset
land. Fixed in `7928b41`.**

**Also done since: `WP_UNIFIED_CHROME` §4 turned over (`0f8eab7` in
`kromosynth-projects`, with §2, §3, §8 and §11 moved with it); `kStripHeight`'s
comment corrected (`3da5a1c`); `hideNav` narrowed (`2db74a9` in
`kromosynth-desktop`).**

**The deploy is DONE and verified on both the simulator and the phone — one
navigation bar, account controls kept (C38). `efe5386`, `2db74a9`, `7f9e848` are
all live.**

1. **`Cache-Control: no-cache` and an `ETag` on `/`.** New, from C38, and it
   affects every future deploy rather than one screen. synth.is serves
   `index.html` with **neither** header, so WebKit falls back to heuristic
   freshness from `last-modified` — roughly a tenth of the document's age. The
   first run after this deploy still showed two bars from a cached `index.html`;
   the phone caught up on its own about eight hours later.

   So the cost is an unpredictable delay between deploying and clients seeing
   it, scaling with how long the previous `index.html` had been up. Not
   permanent staleness — that was the overstatement C38 corrects. `/assets/` is
   already right (hash-named, `immutable`), though it carries two
   `Cache-Control` headers, which suggests two layers both setting it.
   Cloudflare reports `DYNAMIC` on the HTML, so this is likely settable there
   rather than in nginx.

2. **GarageBand, for the look.** 51pt of chrome leaves 191 of 242, which is four
   rows at 44 against WP §11's criterion of three, so this is no longer a
   viability gate. Two things to find out: whether the strip reads correctly in
   a 3:1 letterbox, and whether GarageBand reports a safe-area inset AUM does
   not. The reporting path handles either, and outside the standalone it has
   only ever seen zero.

3. **The Output and Release panel** (WP §9). Now the biggest piece of unstarted
   work here, and still the only fix for something audible: `#MKV9X` distorts
   on a chord and its preview did not. The mechanism
   is documented in `PluginProcessor.cpp`'s own comment — one note at full
   velocity reaches full scale, *that is* preview parity, so chords do not fit.
   The control exists as a host parameter with no UI, buried in AUM's generic
   list. The nav is built now, so build this in HTML in the shell.

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

## 5. Things I got wrong, so you do not repeat them

Recorded because every one cost real time and each has a recognisable shape.
Dated, because some are older than the last rotation.

**2026-09-11: I blamed `env()` inside `calc()` on one obvious reading.** The
page reported its nav as 50 tall under `height: calc(50px + env(…))`, which is
exactly the first term, so the `env()` term looked unsupported. I wrote that
into the CSS as measured and restructured the rule around it. The next reading
added `getComputedStyle` and showed `paddingTop: "62px"` — the stylesheet had
been resolving `env` correctly all along, `calc()` included. The real cause was
timing. **0 is what an absent inset and a broken function both look like.**
*Report the state of the thing, not just the number it produced.*

**2026-09-11: two of my own instruments were wrong before the code was.**
`chrome_height` logged only when the value changed, which made "the page never
called" and "the page reported the seed value" identically silent — precisely
the pair I then had to distinguish. And a pure white screenshot nearly became a
finding when it was the launch screen. *Fix the instrument before reasoning
about the reading.*

**2026-09-11: I reported a landed commit as having a visible effect it did not
have.** The session prompt said `5521287` made the site's nav stand down; I
repeated it, and then also asserted a consequence — that the plugin had lost its
account affordance. Both were wrong: `efe5386` is not deployed, so the site's
nav is still drawn and Sign In is still on screen. *A commit in one repo is not
a behaviour in production.* Same shape as the `chrome=host` entry below, one
layer further out.

**2026-09-11: I used a name grep for process discovery** — `pgrep -fl "Synth.is"`
— which matched Xcode's background index-build clang invocations and dumped
thousands of lines of compiler flags into the session. The convention says
`lsof -nP -iTCP` and not name grep, and this is why.

**2026-09-10: I asserted `chrome=host` was already working**, several times,
because the code existed and WP §2 said so. Nothing failed loudly, because a nav
appearing is what a nav does. *Check that the trigger fires, not just that the
handler exists.*

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
- Commit message files under `/Users/you/Developer/`, not `/tmp`.
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
- **`local-terminal` also returns a bare `null` for multi-line commands
  carrying a shell variable assignment**, in seconds rather than after a hang.
  It looks identical to the trap above and is not it. Single-line literals work.
  Confirmed twice on 2026-09-11.
- **`timeout` and `gtimeout` DO exist on the laptop** (`/opt/homebrew/bin`),
  unlike the Mac Studio. And `grep -c` returning 0 matches exits non-zero, which
  the MCP surfaces as an error; it is not a failure.
- Corrections go where the wrong claim was written, not only where it was
  noticed. `ShellPage.h`'s `nav#chrome` comment carries its own retraction.
