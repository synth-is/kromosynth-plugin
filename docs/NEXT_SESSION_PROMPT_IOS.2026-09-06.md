# Next session — kromosynth-plugin, iOS track

A parallel track to `NEXT_SESSION_PROMPT.md`, which stays the desktop plugin.
Nothing here touches the desktop build.

## Read this first: iOS is a second backend, not a port

**Nothing in the plugin today can run on an iPhone.** The pinned wasmtime C API
exposes exactly two strategies, `WASMTIME_STRATEGY_CRANELIFT` and
`WASMTIME_STRATEGY_WINCH`, and both are JIT compilers. There is no interpreter
strategy in the artefact and no iOS build published for it. iOS forbids JIT for
third-party code.

`cmake/Wasmtime.cmake` says this already, and says the abstraction gets built
"when there is a second real consumer rather than an imagined one." **iOS is now
that consumer.**

### Expect two to four voices, and design for that rather than around it

wasm3 measured a median **5.56× slower** than wasmtime/Cranelift on single-voice
RTF across the 120-elite archive — min 2.02×, p10 3.26×, p90 9.41×, max 24.49×.
At the ≥4× bar that took the median voice cap from **16 to 4**, on this Mac. An
iPhone core is slower again.

So the plausible landing zone is 2–4 voices. That is not fatal: 76% of the
corpus is `textural`, and a two-to-four voice textural instrument in AUM or
Drambo is a coherent product. It is just a **different** product from the
desktop plugin, and knowing that before designing around polyphony you will not
have is worth more than optimism.

## The one question this session answers

**Gate 0 on real hardware: what voice cap does an interpreter give on an
iPhone?** Path B, 8 voices, slowest decile.

If it comes back at 2, iOS is a two-voice textural instrument and everything
downstream is shaped by that. If it comes back at 6 or more, an AUv3 is worth
building properly. Until that number exists, every other iOS decision is a
guess.

## Scope: a plain iOS app that prints numbers

**Not an AUv3.** App-extension packaging, a plugin UI on a phone screen, and
inter-app audio are three separate problems, and none of them matter if the
answer above is bad. Building them first means discovering the blocker after
paying for the packaging.

What it needs to be:

- a plain iOS app, run on a **real device**
- bundling ~10 elites spanning the measured RTF range, and the **slowest decile
  must be in there** — that is the point of the gate, not an afterthought
- linking wasm3, driving it from the existing code
- printing per-elite RTF and the resulting cap at the ≥4× bar

## What to reuse, and what to leave alone

**Reuse `probe/gate0/host_wasm3.c`** (533 lines). It produced the reference
numbers, and the ABI documented at the top of `FaustEngine.h` was derived from
it — that header says so explicitly. `host_wamr.c` sits beside it if WAMR is
worth a second look; all three hosts were written and measured against each
other.

**Do not port `FaustEngine` wholesale.** Voice allocation, per-pitch levelling,
the forced release, the multiplying envelope, the trim table — none of it is
needed to answer the question, and all of it is shaped by the desktop. Porting
it first turns a measurement into a project.

**Do not abstract the backend yet.** The cmake note is deliberate: the
abstraction gets built when there is a second real consumer. A measurement
harness is not yet that consumer. Measure, then decide what the seam has to
carry.

### Runtime options, none yet evaluated for iOS

1. **wasm3** — driving code exists, numbers exist, known quantity. Start here.
2. **WAMR** — `host_wamr.c` exists. Its AOT mode is not obviously usable under
   the JIT prohibition, but its interpreter is.
3. **wasmtime built from source for Pulley**, its portable interpreter backend.
   Keeps one API and one ABI, which is worth something given how much of
   `FaustEngine.h` documents wasmtime's exact behaviour. Requires a Rust
   cross-compile to a static library, and its speed against wasm3 is unmeasured
   here. Worth a look only if wasm3's number is marginal.

## Pitfalls, each of which has already cost time once

**Math imports compute in DOUBLE.** faustwasm binds `_powf` to `Math.pow`; a C
host calling `powf()` diverged by −50.5 dB on a math-heavy elite.
`host_wasm3.c` already has this right. Do not "fix" it.

**Peak is a function of SAMPLE RATE.** Up to 3.8 dB at one pitch between 48 kHz
and 44.1 kHz, and not by a uniform scaling. Measure at the rate iOS will
actually use.

**RTF does not transfer between platforms.** V8 against wasmtime gave ratios of
0.48–0.99 with no usable scaling factor. The desktop numbers predict nothing
about the phone, which is the entire reason this has to run on hardware.

**Real device, not the simulator.** The simulator runs host-architecture code
and tells you nothing about a phone's clocks or thermal behaviour.

**Note whether the number was taken cold.** A cap measured on a cool phone that
collapses after four minutes in AUM is not the cap. If there is time, take it
twice.

## After Gate 0, if it is viable

In order, and detailed in `NEXT_SESSION_PROMPT.md`:

- **B1** per-rate peak tables. Peaks are portable across runtimes — the null
  test has wasmtime and V8 bit-identical over 33 elites — but not across sample
  rates, so one stored table is not valid.
- **B2** split the contract's trust rule. `ARTEFACT_CONTRACT` §3.2 gates
  everything on `rtfPlatform` matching; peaks should gate on **sample rate**
  alone and only `rtf` needs runtime identity. The rule conflated them because
  one render produces both.
- **B3** ask whether `rtf` is needed at load at all. Its only consumer is the
  voice cap, which could start conservative and adapt from real CPU during
  playback. If that works, a matching-rate artefact needs **no load-time render**
  — 1.2–4.8 s becomes instant, which on an interpreter is the difference between
  viable and not.

Then, and only then, AUv3.

## Out of scope

The desktop track. `A-i` and everything else in `NEXT_SESSION_PROMPT.md`.

## One measurement from 2026-09-05 that will matter here

Forced stops are **binary across the archive, and hold length does not change
them**: over 33 elites at 32 notes each, 644 forced stops at a 0.1 s hold
against 640 at 0.8 s. 20 of 33 elites force-stop *every single note*; the other
13 force-stop none.

So for roughly 60% of the archive, every note currently ends in the plugin's
blunt 0.2 s fade after sitting at full level through a 1.5 s grace period,
rather than ending on its own. On a 2–4 voice cap that is not a cosmetic
problem — a voice held at full level for 1.5 s after note-off is a voice you do
not have. Whatever the iOS voice policy becomes, it cannot inherit the desktop's
grace period unexamined.
