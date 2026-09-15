# Next session: the chrome is done, the page it sits on is not

Rotated 2026-09-12. Previous: `NEXT_SESSION_PROMPT_IOS.2026-09-11.md`.

Read `docs/AUV3_IOS_FINDINGS.md` first — C0 through C39, newest first. That file
is the record. Do not re-derive it from commit messages.

---

## 1. The single most useful thing to know

**The strip is 51pt and it was never the problem. The shell page's own furniture
is 172pt, and the instrument list gets 20.**

Measured in GarageBand's 736×242 letterbox, off a 3x screenshot, C39:

| band | pt | what |
|---|---|---|
| 0–51 | **51** | the chrome strip |
| 51–107 | **56** | shell header |
| 107–149 | **42** | filter + sort row |
| 149–169 | **20** | **the instrument list**, for four instruments |
| 169–206 | **37** | footer status |
| 206–242 | **37** | load line |

The previous rotation said 51pt of chrome leaves 191 for content and called that
four rows at 44. It counted everything below the strip as content. WP §11's
criterion of three rows fails by about six times.

**BEFORE WRITING ANY OF THE LAYOUT WORK, READ §2.** The loop cannot see the
viewport the work is for.

## 2. The loop cannot test the acceptance viewport

Everything verified on 2026-09-11 was at 402×874 (simulator standalone) or
428×926 (AUM on device). **736×242 has only ever existed on Björn's phone, in
GarageBand.** The standalone simulator fills the screen; there is no way to
letterbox it. So a layout change aimed at §8's acceptance viewport currently
cannot be checked before it reaches him — and "I could not see it" is how three
separate wrong claims got made in one day (C35's `calc()`, C38's cache
magnitude, C39's 191).

**Close this first.** Cheapest option: serve `kIndexHtml` to a desktop browser
sized to 242pt tall. It tests the CSS with no plugin, no build and no device. It
needs browser access, and `chrome-devtools`' profile is held by a stale
automation Chrome — **pid 31346, running since 2026-09-08 with Puppeteer flags**.
Ask Björn to quit it; it is not a browser he is using.

Other options, untried: a compile flag forcing the standalone editor to 736×242,
in the spirit of `KROMOSYNTH_EDITOR_STUB`; or an AUv3 host on the simulator, if
one exists.

## 3. Open, in order

1. **The letterbox layout**, per C39's proposal. In this order, because the first
   two are deletions of redundancy and the last two are new responsive
   behaviour:

   1. **Drop the footer's `#status`.** "Nothing loaded yet — pick an
      instrument." is currently on screen TWICE — in the strip and in the
      footer — costing 37pt, nearly twice the whole list. Self-inflicted by the
      `setStatus` mirroring in `e393a5f`: right in strip mode, redundant in
      library mode. −37pt.
   2. **Fold the load line into the strip's 16pt status row.** −37pt.
   3. **Compact the header** under `@media (max-height: 320px)`. 56 → ~36pt.
   4. **Hide the filter row** at short heights or when the list is short.
      −42pt.

   1 and 2 alone take the list from 20 → 94pt. All four reach ~155, which is
   three rows at 44 with room.

2. **`Cache-Control: no-cache` and an `ETag` on `/`** (C38). synth.is serves
   `index.html` with neither, so WebKit falls back to heuristic freshness and a
   deploy reaches running clients after an unpredictable delay — minutes to more
   than a day. Not permanent staleness; that was the overstatement C38 corrects.
   `/assets/` is already right, though it carries two `Cache-Control` headers.
   Cloudflare reports `DYNAMIC` on the HTML, so likely settable there.

3. **Rebuild the device against HEAD.** The phone's binary dates from 14:20 on
   2026-09-11, before `21f438a` and `7928b41`. Neither the ChromeBar deletion
   nor the border-box fix has ever run on hardware. Nothing is known to be
   wrong; it is simply untested there.

4. **The Output and Release panel** (WP §9). The control exists as a host
   parameter with no UI, buried in AUM's generic list.

   **`#MKV9X` DOES NOT EXIST.** Checked 2026-09-14 against nine device logs:
   instrument labels come from `acquisition_sentinel` as
   `Virtual Instrument #XXXXX`, and the full set ever seen is DDJ9Y HPG05 MBDZW
   NE1ZR RE89N WMSEM XKBRY YKK21 RPGW9 VDJTF EWBVW TCSG1 APB30 CPRZX HCR3Z
   HXMA3 WDQMJ YVC37 8F9V2 6PKTV G3A0B T9CC2 5302Z Z222F. `MKV9X` is in none of
   them. It was mistranscribed on 2026-09-10 and copied through three prompt
   rotations, so "the only fix for something audible" rested on an instrument
   nobody could load. Do not reintroduce the label; if the chord report is
   chased again, get the id off the log.

   **The premise of this item was wrong and is fixed.** It said one note at full
   velocity reaching full scale *IS* preview parity. It is not. That was parity
   with `renderFaustGenome`'s output buffer, and the web app never plays that
   buffer at its peak — a card's PLAY button sends it through 0.25, an 0.8
   master and a DynamicsCompressor. Measured 2026-09-13 over 12 elites spanning
   26 dB of raw peak: **the plugin was 9.0 dB louder than the audition**, median,
   spread 0.7 dB peak-to-peak. Both pre-registered rules passed. The Output
   default is now −9 dB, which also lifts single notes off the 0.95 limiter they
   previously rode on every key. See `render-socket/src/measure-audition-parity.js`.

   So this item is now only about the UI, not about the level.

   **On device, 2026-09-13, and the verdict is soft.** Installed to the phone
   (container `56CE498F`) and auditioned: "seems to be an improvement — perhaps
   we will revisit". Recorded at that strength deliberately. It is one listener,
   one sitting, no A/B against the site open alongside, and it is consistent
   with the 9.0 dB being right without being evidence that it is. Three specific
   checks were asked for and only the general impression came back, so these two
   remain genuinely open:

   - **the quiet-genome case.** Genomes measuring ~22 dB below full scale were
     also 9 dB loud, and a trim-ceiling change would never have reached them
     — that is the whole reason the fix went on the Output default instead.
     Not separately confirmed by ear. If these did *not* move with the loud
     ones, the unconditional-vs-attenuate-only reasoning is wrong and
     everything above needs revisiting before anything is built on it.
   - **the chord report.** Cannot be rechecked as written, because the
     instrument it names does not exist. The audible half of this item is
     neither confirmed dead nor alive, and now has no subject either.

   Do not let "seems to be an improvement" harden into "parity confirmed" on a
   later reading of this file. It is not the same claim.

   Two candidates, one test: one note clean and two distorted means summing; two
   clean and three distorted means voice stealing on the 2-voice cap. Not run.


## 4. The loop, and how to drive it

**AUM hands the plugin at least five different rectangles in a single session**
— 428×926, 428×879, 428×280, 409×524, 428×574, including one narrower than the
screen. GarageBand adds 926×428, 242×736 and 736×242. Anything that sizes
against the editor cannot read one `editor_resized` line and call it the layout.

The simulator loop is still the right tool for everything except the letterbox
(§2) — same `cmake` invocation below. Corrections to how to drive it:

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
xcrun simctl io <udid> screenshot /Users/bjornpjo/Developer/shot.png
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

## 5. What landed on 2026-09-11

Nine more commits after the two below, across three repos. In brief:
`21f438a` deleted ChromeBar and the disposable probe block; `7928b41` fixed the
`ResizeObserver` box (C37); `3da5a1c` corrected `kStripHeight`'s comment;
`0f8eab7` in `kromosynth-projects` turned over `WP_UNIFIED_CHROME` §4 with §2,
§3, §8 and §11 moved to match; `2db74a9` and `7f9e848` in `kromosynth-desktop`
narrowed `hideNav` and fixed where it left the user section; `c101d55`,
`33e12d3` and the C39 commit are the record.

The two that started it:

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

## 5b. What landed on 2026-09-10

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

## 6. The icon: do not pick this up casually

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

## 7. Things I got wrong, so you do not repeat them

Recorded because every one cost real time and each has a recognisable shape.
Dated, because some are older than the last rotation.

**2026-09-12: arithmetic stood in for looking, three times in one day.** C35's
`calc()` — a real reading with an obvious interpretation I took instead of
testing. C38's cache magnitude — a real mechanism with a severity I invented,
disproved by one phone with different history. C39's 191 — a correct subtraction
of the wrong quantity, treating everything below the strip as content when
172pt of it is page furniture. *The common shape is a real measurement applied
to a quantity nobody checked was the one that mattered.*

**2026-09-11: I asserted layout from source twice and was wrong both times.**
`2db74a9` claimed `justify-between` would leave the user section where it sat;
it puts a lone child at the START. eslint passed and vite HMR'd cleanly both
times. *Those checks say nothing about where flexbox puts things, and passing
them is not evidence about layout.*

**2026-09-11: I skipped the `dryRun` pass twice** and the second one closed
`showSite` with `}` while two statements still followed. Caught before building.
The convention exists so it does not get that far; used on every edit afterwards,
it caught a duplicated pair of list items in this file.

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

## 8. Conventions that held

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
- **`local-terminal` also returns a bare `null` for multi-line commands
  carrying a shell variable assignment**, in seconds rather than after a hang.
  It looks identical to the trap above and is not it. Single-line literals work.
  Confirmed twice on 2026-09-11.
- **`timeout` and `gtimeout` DO exist on the laptop** (`/opt/homebrew/bin`),
  unlike the Mac Studio. And `grep -c` returning 0 matches exits non-zero, which
  the MCP surfaces as an error; it is not a failure.
- Corrections go where the wrong claim was written, not only where it was
  noticed. `ShellPage.h`'s `nav#chrome` comment carries its own retraction.
