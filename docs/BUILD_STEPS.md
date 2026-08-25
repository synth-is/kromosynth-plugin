# Build steps — Stage 0

Run these in order. Stop at the first one that fails and report it; a failure
here is data, not an obstacle to route around.

Total time on a warm machine: **5 minutes of typing, 15–40 minutes of waiting**
(JUCE's first compile dominates).

---

## 0. Preflight — 30 seconds

```sh
cd /Users/bjornpjo/Developer/apps/synth.is/kromosynth-plugin
bash scripts/check-deps.sh
```

Nothing is verifiable about your toolchain remotely — the only evidence on disk
was four `CMakeCache.txt` files from December 2023. This script checks the real
current state: macOS + arch, `xcode-select` developer dir, clang version, macOS
SDK and the AudioUnit/AudioToolbox frameworks, cmake ≥ 3.24, make, git, disk
space, plugin install directories, which of the four hosts are installed, and
whether `https://synth.is` responds.

Exit 0 = good to proceed. Exit 1 = fix the `MISS` lines first.

If you want it on PATH as an executable:

```sh
chmod +x scripts/check-deps.sh
```

### If something is missing

| Missing | Fix |
|---|---|
| developer directory / clang / SDK | `xcode-select --install` |
| cmake, or cmake < 3.24 | `brew install cmake` / `brew upgrade cmake` |
| pluginval | Not required at Stage 0. |

**Install ninja.** The preflight showed **GNU Make 3.81** — that is the 2006
version Apple still ships (they froze at the last GPLv2 release). It works,
but it is slow and its parallel-build handling is creaky, and the JUCE first
compile is the long pole of this whole session. Homebrew is clearly already
present (cmake came from `/opt/homebrew/bin`), so:

```sh
brew install ninja
```

Then use `--preset stage0-ninja` everywhere below instead of `stage0`.

The earlier advice to default to Makefiles was about avoiding a dependency on
a machine that might not have a package manager. This machine does. Take the
faster build.

**Full Xcode is not required** — though you have it. Command Line Tools are
enough for AU + VST3. Full Xcode matters only for the `stage0-xcode` preset,
which exists so you can attach a debugger to a host.

---

## 1. Initialise the repository — 1 minute

```sh
cd /Users/bjornpjo/Developer/apps/synth.is/kromosynth-plugin
git init
git add .
git commit -m "Stage 0: build layer for the Synth.is plugin shell"
```

Do not push. (Session constraint.)

---

## 2. Vendor JUCE — 3–10 minutes

### Which tag

**Use the latest 9.x** (9.0.1 at time of writing; check for newer).

This machine is macOS **26.3.1**, SDK **26.5**, Apple clang **21.0.0**. JUCE's
CHANGE_LIST on master lists *"Added macOS/iOS 26 support"* and *"Fixed some
LLVM 21 compiler warnings"* — those are on the **9.x** line. That is exactly
this toolchain, named explicitly.

(An earlier draft of this document recommended 8.0.15 on the belief that it
carried the macOS 26 work. It does not; master does. Recorded because the
reasoning was wrong in a way worth not repeating: deferring to
`WP_SYNTHIS_PLUGIN.md`'s "JUCE 8" when the WP is known stale, and treating
"the API may have moved" — a ninety-second compile error — as if it were a
subtle risk.)

The decisive argument is about the experiment itself: **Task 1 is a test of
the webview.** JUCE 9.0.1 added a TypeScript npm package for WebView
integration, so the webview is under active development in 9. Running Task 1
on the EOL 8.x branch risks reporting a WKWebView failure that JUCE already
fixed, and then changing the WP on the strength of a false negative. That is
the opposite of this session's purpose.

List what is actually available before choosing:

```sh
git ls-remote --tags https://github.com/juce-framework/JUCE.git \
  | grep -oE '9\.[0-9]+\.[0-9]+$' | sort -V | uniq | tail
```

### What to expect from 9

Two changes matter here; neither is serious.

1. **`juce_audio_processors_headless` is new.** JUCE 9 split the module:
   headless holds the classes for loading and playing processors without UI,
   and `juce_audio_processors` layers UI on top. `PluginProcessor.h` includes
   `<juce_audio_processors/juce_audio_processors.h>`, which should still
   resolve transitively. If it does not, that is the first compile error and
   the fix is one line in `src/CMakeLists.txt`.

2. **Breaking changes are mostly `AudioChannelSet`.**
   `create9point0point4` and its 9.1.4 / 9.0.6 / 9.1.6 variants now map to the
   `_W` VST3 layouts. Irrelevant to a stereo-out synth with no bus-layout
   logic — which is what Stage 0 is.

Also removed from `PluginEditor.cpp`: `.withBackend(Options::Backend::webview2)`.
It was Windows-only, ignored on macOS, and a needless compile risk against
enums that cannot be verified from here.

### Shallow clone — you want this

The preflight reported **12 GB free**. That clears the bar but is not roomy: a
full JUCE clone carries the whole history (~1.5 GB) and the Debug build tree
adds several GB on top. Clone only the tag:

```sh
cd /Users/bjornpjo/Developer/apps/synth.is/kromosynth-plugin
git clone --depth 1 --branch 9.0.1 \
  https://github.com/juce-framework/JUCE.git third_party/JUCE
git submodule add https://github.com/juce-framework/JUCE.git third_party/JUCE
git add .gitmodules third_party/JUCE
git commit -m "Vendor JUCE 9.0.1 as a pinned submodule"
```

`--depth 1 --branch <tag>` works with tags as well as branches and saves
roughly a gigabyte. `git submodule add` on a directory that is already a git
repo adopts it rather than re-cloning.

If you later need full history: `git -C third_party/JUCE fetch --unshallow`.

### Licence

Verified against `LICENSE.md` on master: the JUCE Framework modules are
dual-licensed under the AGPLv3 and the commercial JUCE licence. So
`WP_SYNTHIS_PLUGIN.md` §0's assumption holds for 9.x as well.

That is the repository-root file, though. `cmake/JuceLicenceCheck.cmake` reads
the **module header** (`modules/juce_core/juce_core.h`), which is the stricter
check and the one that governs what ships inside the binary. Let it run.

---

## 3. Configure — and this answers the licence question

```sh
cmake --preset stage0
```

**Read the output.** Two lines matter:

```
-- JUCE 8.x.x — module declares license: <whatever it says>
-- JUCE licence assertion passed: module header mentions AGPL
```

If instead you get a `FATAL_ERROR` from `kromosynth_assert_juce_licence`, that
is the answer to `WP_SYNTHIS_PLUGIN.md` §0's open question, and it is a finding
to record — **not a check to delete.** It would also reopen §9's deferred
question about pairing JUCE with libfaust (GPL) under tier 2. Paste the error
and we take it from there.

Expected other failure at this step: `third_party/JUCE is empty` means step 2
did not complete.

---

## 4. Build — 15–40 minutes the first time

```sh
cmake --build --preset stage0
```

JUCE compiles once and then caches. Subsequent builds of just the plugin
sources are seconds.

`COPY_PLUGIN_AFTER_BUILD TRUE` installs to:

- `~/Library/Audio/Plug-Ins/Components/Synth.is.component` (AU)
- `~/Library/Audio/Plug-Ins/VST3/Synth.is.vst3` (VST3)

Confirm:

```sh
ls -la ~/Library/Audio/Plug-Ins/Components/ ~/Library/Audio/Plug-Ins/VST3/
```

---

## 5. Validate the AU before opening any host — 1 minute

```sh
auval -a | grep -i synth.is          # is it registered at all?
auval -v aumu Krm0 Sthi              # full validation
```

`aumu` = music device (instrument), `Krm0` = `PLUGIN_CODE`, `Sthi` =
`PLUGIN_MANUFACTURER_CODE`, both from `src/CMakeLists.txt`.

Doing this first is worth the minute: Logic refuses to load an AU that fails
validation and its error message will not tell you why. `auval` will.

If it does not appear in `auval -a`, reset the AU cache:

```sh
killall -9 AudioComponentRegistrar
```

---

## 6. The host matrix — the actual experiment

### First: how much of the matrix you can fill

The preflight's host list was **wrong** on first run — `check_host` returned on
the first glob match, so with both Live 11 and Live 12 installed it reported
only "Ableton Live 11 Suite" and hid the 12. Fixed; it now lists every match.
Re-run `scripts/check-deps.sh` to see the true picture.

With **Live 12** confirmed and **Reaper** obtainable, the matrix goes from an
anecdote to something worth reporting:

- **Reaper** — https://www.reaper.fm/download.php — ~20 MB, fully functional
  unlimited evaluation, no account. Highest-value install by a wide margin:
  most permissive scanner, best error reporting, and the recommended *first*
  host below.
- **Live 12** — the one to use. Live 12 changed plugin scanning and sandboxing
  relative to 11, and it is also the version `kromosynth-live` targets (the
  Ableton Extensions SDK is 12.2+). Testing the plugin in the same Live that
  runs the extension keeps the two findings comparable. If Live 11 is also
  installed, testing both is a free extra data point — note which is which.
- **Bitwig** — https://www.bitwig.com/download/ — demo is unlimited but
  **cannot save projects**, so the "save → quit → reopen" row is unavailable
  there. Everything else works.

**Logic is the one worth the effort, eventually.** It is the sandboxed,
hardened, AU-only host — the single most likely place for the
`localStorage`-in-host-container problem from §B to surface, and §C's precedent
from `kromosynth-live` (Live's modal webview having an ephemeral data store)
raises the odds that something in that family is real. `auval` (present)
covers AU *validation*, which is genuinely useful, but it says nothing about
whether a webview survives inside a sandboxed host.

Suggested sequencing: run Reaper + Live 12 + Bitwig first. If all three pass
cleanly, Logic is a confirmation step and can wait. If any of them shows
storage or focus trouble, Logic becomes the priority, because it is where that
class of problem is worst.

### Order

Open each host, insert **Synth.is** on an instrument track, and work through
section A of `docs/STAGE0_FINDINGS.md`, filling the table as you go.

Go **Reaper → Bitwig → Live → Logic**, easiest to hardest:

1. **Reaper** — most permissive, fastest scan, best error reporting. If it
   fails here, it fails everywhere, and you found out cheaply.
2. **Bitwig** — sandboxes plugins in a separate process by default. A crash
   here kills the plugin host, not Bitwig, so you get a clean report.
3. **Live** — stricter about unsigned binaries. If it refuses to scan, that is
   a *signing* finding, not a webview finding; note which.
4. **Logic** — sandboxed and hardened, AU only, strictest. Most likely to
   expose the `localStorage`/container problem.

If only Live is available, still do it — but label the result as single-host
and do not let it settle the Stage 0 gate on its own.

While testing, watch the log:

```sh
tail -f ~/Library/Logs/Synth.is/stage0-*.log
```

You want to see, per host:

```
processor_constructed host=... wrapper=...
navigating to https://synth.is
page_about_to_load https://synth.is
page_finished_loading https://synth.is
PROBE {"href":"...","crossOriginIsolated":"...","localStorage":"...",...}
```

The `PROBE` line is the one to paste back. `editor_constructed` /
`editor_destroyed` pairs show what the host actually did when you closed and
reopened the window.

### Things to try deliberately, not just observe

- Close the plugin window and reopen it. Did the page reload or resume?
- Save the project, quit the host, reopen. Same question.
- Drag the window corner slowly. Does the page reflow or clip?
- Click into a search field and type. Do the keystrokes arrive?
- Press **spacebar** with focus in the webview. Does the host start playback
  instead of typing a space? That is the keyboard-focus question, and it is
  the one most likely to differ between hosts.
- Add a **second** instance in the same project.
- Delete the plugin from the track. Does the host survive?

---

## 7. If a host crashes

That is the most valuable outcome available this session, per the WP. Capture
it rather than retrying:

```sh
ls -lt ~/Library/Logs/DiagnosticReports/ | head
```

Grab the newest `.ips` for that host. What matters in it is whether the
crashing thread is in `WebKit` / `WKWebView` / `com.apple.WebKit.WebContent`,
or in JUCE, or in the host. WP §7 anticipates the first case and says it does
*not* reopen the framework choice — iPlug2 would fail identically. Confirm
which case it actually is before concluding that.

---

## What NOT to do at this stage

- Do not add native integration, a resource provider, or a JS bridge. Both
  change the page's origin semantics and would invalidate the probe.
- Do not sign or notarise. Stage 1.
- Do not touch `third_party/sfizz` — Task 2 is gated on Task 1 passing.
- Do not "fix" anything you find. Record it.
