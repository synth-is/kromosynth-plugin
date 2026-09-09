# Next session: the plugin after AUv3

Rotated 2026-09-09. Previous: `NEXT_SESSION_PROMPT_IOS.2026-09-08.md`, which
planned AUv3 Stages A–D. **A, B and C are done and verified on device.** The
title drops "iOS" from its subject because most of what is now open is not
iOS-specific; iOS only found it first.

---

## 0. Read these, in this order

1. `docs/AUV3_IOS_FINDINGS.md` — C0 through C25, newest first. This is the
   record. Do not re-derive from commit messages.
2. Its **"Open, in order"** list at the bottom. That is the queue.
3. `../kromosynth-projects/Evolutionary Sound Discovery Platform/daw-plugin/WP_UNIFIED_CHROME.md`
   — designed, not built. §7.2 is the part that matters most.

`git log --oneline e542412..` is the session. Ten commits, and the messages
carry reasoning the findings file condenses.

---

## 1. State

**Working on an iPhone 13 Pro Max, iOS 26.6.1, in GarageBand and AUM:** the AUv3
instantiates, receives MIDI, plays, acquires instruments from the site,
compiles them through wasm3, and shares one library with the standalone app
across the process boundary. The App Group entitlement is live end to end.

**Verified this session:** the voice cap derives arithmetically and applies to
already-measured instruments; the log no longer loses lines to concurrent
writers; the null test refuses a stale probe; the instrument list no longer says
`silent` for a working sound or advertises another machine's polyphony.

**Not working:** the AU icon (C24). The app icon is on the home screen; hosts
draw a placeholder.

---

## 2. The three things I would do first

**a. Make the deadline numbers interpretable** (C22). `dc5cb17` measures block
load, and the first readings — `peak 142%`, `22 OVER BUDGET` of 4065 blocks —
cannot distinguish a heavy genome from one late block after a load. Log the
first few over-budget blocks with timestamps and correlate against
`instrument_load` in the same file.

Everything about polyphony waits on this. The question Björn asked is whether a
2-voice cap is *right*, and the honest answer is still "we don't know" until a
transient can be told from steady state. Do not argue the cap from the numbers
already in C22.

**b. The icon experiment** (C24). One throwaway run, not a build feature: strip
`AudioComponentBundle` from the built appex's `Info.plist`, re-sign, install,
look. If the icon appears, make it a post-build step. If not, the hypothesis is
wrong and the legacy `appiconset` against the iOS 27 SDK is next.

**c. Say "measuring" while measuring** (C21). Nine seconds of silence after
acquiring an instrument, which reads as a broken instrument. `measuring_` is
already an atomic "read by the editor" and the load outcome already carries
`measuring=true`. Nothing displays it.

All three are small. None needs a decision from Björn.

---

## 3. What needs Björn, not you

- **The container idiom** for the web app's render UI. The same
  `VirtualInstrumentPanel` is a page in `TakeoutView`, a modal in
  `FeedRenderModal`, and an inline panel in `SoundGarden`. Capability is already
  shared; only the container differs. The modal reads best in GarageBand's
  736×242; the panel reads best on a wide desktop. Possibly both should stay and
  the fix is that the garden and the feed should match *each other*.
- **What the merged library's two states are called** (`WP_UNIFIED_CHROME.md`
  §10.2).
- **The per-note gain clipping fix** (C23): clamp the boost, or take trim
  headroom on a wide spread.
- **Desktop parity for the chrome** — same component, or same structure at a
  different scale.

---

## 4. Blocked on something outside this repo

**The merged library** (`WP_UNIFIED_CHROME.md` §7.2) is the most consequential
open item and the one a user notices first: instruments acquired on a laptop are
invisible on a phone, though the account already knows about them and
re-acquiring is free.

It is blocked on `WP_EXTRACTION_QUOTA.md` **open question 7**. The re-download
exemption keys on the takeout *entry*, so the merged list must re-download the
same entry and never re-render — otherwise a user is charged for something they
already own, invisibly. Settle that question first.

---

## 5. How to work here

**Read from disk before asserting.** C25 records five times this session that I
claimed a mechanism did not exist because I could not see its output, and five
times it was already built. Absence from a log means the layer does not report.
Absence from one `grep` spelling means nothing.

**Gate on your checks, do not merely run them.** Installing under a live AU host
took AUM down (C15). The check `devicectl device info processes | grep -iE
"AUM\.app|GarageBand|Loopy|Cubasis|Drambo"` works; I once ran it in the same
command as the install and installed anyway. **An AU host is the hazard; our own
app running is not.**

**Null test before and after anything touching `FaustEngine.cpp`.** Baseline is
120/120 bit-identical. `./scripts/null-test.sh` now prints which probe ran and
refuses a stale one — rebuild `faust_probe` first, in `build/desk-verify` or
`build/faust`. **`build/wasm3` can no longer compile it.** And note the test
does *not* cover voice allocation, so a pass says nothing about a cap change.

**Explicit pathspec commits.** `kromosynth-projects` carried 25 items of
unrelated WIP this session, and something else is committing to it in parallel —
`54d2d5a` landed between two of my commits.

**Trust the device over the tooling.** devicectl's App Group container view is
frozen at install time and lies plausibly (C16). The plugin's own display was
right and I believed the tool for an hour.

---

## 6. Two claims worth not repeating

The load numbers in C22 look alarming and are not yet evidence. And "the 4×
headroom bar is about right" is an *inference* from two measured figures, not a
measurement — that is exactly what §2a exists to replace.
