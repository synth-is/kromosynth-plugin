# Next session — kromosynth-plugin, iOS track

A parallel track to `NEXT_SESSION_PROMPT.md`, which stays the desktop plugin.
Nothing here touches the desktop build.

**Amended again 2026-09-06, later the same day, and one correction is
load-bearing.** The harness described under "The hardware plan" now exists and
is built: `faust-genome-substrate/probe/gate0-ios/`, with its own `README.md`
covering what was verified without the device. Two things changed while
building it:

- **The archive's RTF ranking does not reproduce.** 22 of 120 elites read more
  than 10% faster on re-measurement, 21 of them in the same direction, and the
  archive's slowest decile keeps only 7 of its 12 members. Peaks are
  bit-identical across all 120, so the audio was always right and only the
  timing was wrong. `REBASELINE_2026-09-06.md` has it. **Every reference to
  "the slowest decile" below should be read as "the slow end of the
  distribution"** — at this measurement precision the decile is a region, not a
  set of twelve names, because per-elite repeat spread reaches 29% while the
  band separating decile members is 24%. The 8-voice verdict is untouched and
  the reason is in §4 of that document.
- **wasm3 needed no porting at all.** It and `host_wasm3.c` cross-compile clean
  for `arm64-apple-ios`, and the wasm3 build configuration resolves *identically*
  to the macOS build the reference numbers came from. H0 was smaller than this
  document assumed.

Previous version preserved at
`NEXT_SESSION_PROMPT_IOS.2026-09-06.md`. The revision adds a fourth runtime
candidate that the earlier draft did not list, corrects the premise on which
that candidate was originally dismissed, folds in two findings from
`EXPERIMENT_01_3_DESKTOP_RESULT` that narrow the gate, and replaces "a plain
iOS app that prints numbers" with a staged hardware plan. The one question is
unchanged.

## Read this first: iOS is a second backend, not a port

**Nothing in the plugin today can run on an iPhone.** The pinned wasmtime C API
exposes exactly two strategies, `WASMTIME_STRATEGY_CRANELIFT` and
`WASMTIME_STRATEGY_WINCH`, and both are JIT compilers. There is no iOS build
published for the prebuilt C API tarball. iOS forbids JIT for third-party code.

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

---

## The one question this session answers

**Gate 0 on real hardware: what voice cap does an interpreter give on an
iPhone?** Path B, 8 voices, slowest decile.

If it comes back at 2, iOS is a two-voice textural instrument and everything
downstream is shaped by that. If it comes back at 6 or more, an AUv3 is worth
building properly. Until that number exists, every other iOS decision is a
guess.

### Two things the desktop already settled — do not re-litigate them

**No runtime rescues the slowest decile.** `EXPERIMENT_01_3_DESKTOP_RESULT` §
"the result that reframes the gate": 8 voices at ≥4× needs ≥32× single-voice,
and the slowest decile clears that **0/12 under V8/TurboFan**, 0/12 under wasm3,
0/12 under WAMR. *"The binding constraint on the tail is the cost of the
genomes, not the execution strategy."* So the runtime comparison below is a
question about the **middle of the distribution and the size of the gated
catalogue**, not about saving the tail. Do not go into this session expecting a
faster interpreter to change the tail's answer. It will not.

**Check memory before RTF.** `EXPERIMENT_01_4.1` §1.4 flagged that memory may
bind before throughput inside an AUv3 extension, and §8.4 of the desktop result
repeats it. Desktop peak RSS holding 8 voices: wasm3 5.1 / 12.3 / 22.1 MB
(min/median/max), WAMR 13.7 / 17.9 / 22.7 MB, one instance per voice. On desktop
22 MB is unremarkable. Whether it is unremarkable inside an audio-unit extension
on a phone is an open question and it is **cheaper to answer than RTF** — take
it in the same run.

---

## The runtime candidates, corrected

The earlier draft listed three: wasm3, WAMR, wasmtime/Pulley. All three are the
same kind of answer — interpret **the same module bytes the platform emitted**.
There is a fourth kind, and the reason it is not in the list is a mistake worth
naming.

### The row that is missing from `EXPERIMENT_01_DSP_EXECUTION` §1

That table has four paths. Row D reads:

| # | path | how the program arrives | iOS? |
|---|---|---|---|
| D | **libfaust JIT** | `.dsp` compiled in-process by embedded LLVM | **no — Apple forbids it** |

**That dismissal is correct about LLVM and wrong about libfaust.** libfaust has
an interpreter backend that emits Faust Byte Code (FBC) and runs it on a
built-in virtual machine, generating no machine code at all. Per Grame's own
documentation it *"has been first written to allow dynamical compilation on iOS,
where Apple does not allow LLVM based JIT compilation to be deployed."* The
entitlement question row D turns on never arises for it.

So the taxonomy needs a **row D′ — libfaust interpreter**, iOS: yes. The
external discussion that surfaced this (`faust dsp on ios`, 2026-09-04) is wrong
about several other things — see "what that discussion got wrong" below — but it
was right about this one, and it was right about something a document in this
repo gets wrong.

### Two axes, not one list

Once D′ exists, the choice stops being a list of runtimes and becomes two
questions:

1. **What does the device execute?** wasm bytecode, or Faust FBC.
2. **Where does the translation happen?** On the server, or on the device.

Those are independent, and the combination that has not been considered anywhere
is **FBC produced server-side and shipped**. `interpreter-machine-dsp.h`
declares `writeInterpreterDSPFactoryToBitcode` /
`readInterpreterDSPFactoryFromBitcode` (and the file-based pair). If that reader
is available in a build without the compiler front-end, the device links an
interpreter and not a compiler, `.dsp` source never leaves the platform, and
**"the platform compiles; the plugin plays" survives intact**.

Whether Faust ships a compiler-free "machine" library build is **NOT VERIFIED**.
The header exists and the API is declared; that is all this section claims.
Verify it before costing D′, because it is the difference between linking
something like 8 MB of compiler and linking a virtual machine.

### The four, ordered by cost to a number on hardware

1. **wasm3** — interprets `instrument.wasm` unchanged. Driving code exists
   (`probe/gate0/host_wasm3.c`, 533 lines), numbers exist, and the sources are
   already on this machine at `probe/out/wasm-runtime/wasm3`. Dependency-free C
   compiled straight into the binary per `probe/gate0/build.sh` — there is no
   cross-compile to get wrong, which is what makes it the first one on the
   phone. Measured **1.3× faster than WAMR throughout** on desktop.

2. **WAMR fast-interp** — same bytes, `host_wamr.c` exists and was built and
   measured (`probe/gate0/out/host_wamr`). Slower than wasm3 on every row of the
   desktop table. Its AOT story is what made it interesting, and §4.3 ruled that
   out on iOS on both technical and policy grounds. Include it only because the
   marginal cost once wasm3 is running is a CMake platform variable.

3. **libfaust interpreter (D′)** — different bytecode, and the only candidate
   whose numbers are entirely unknown here. Build via Faust's CMake: there is an
   `ioslib` target for a static iOS libfaust, and backends are selected with
   `BACKENDS=interp.cmake TARGETS=interp.cmake`. The `make ios INTERPRETER=1`
   invocation quoted in the external discussion **does not appear in Faust's
   documentation and should not be budgeted against**.

4. **wasmtime/Pulley** — same bytes, same C API, same ABI that `FaustEngine.h`
   documents at length, which is real value. Two corrections to the earlier
   draft's framing:
   - Pulley is **not** reached through the strategy enum. It is a compilation
     *target*: `config.target("pulley64")`, after which the C API works as
     usual. So "the C API exposes only two strategies" is true and beside the
     point.
   - Wasmtime's own documentation says to *"expect a ~10x order-of-magnitude
     slowdown relative to native code or Cranelift."* wasm3 measured **5.56×**
     against Cranelift here. On the vendors' own numbers Pulley is the slower
     interpreter, which moves it from "worth a look if wasm3 is marginal" to
     **last**.
   - No official iOS C API artifacts are published, so this needs a Rust
     cross-compile from source. A third-party SwiftPM package exists that
     vendors an iOS xcframework and forces `pulley64`; it is an existence proof
     that the path works and **not** a dependency this project should take, per
     `VENDORING.md`.

---

## What D′ costs, if it wins on speed

Three costs, each already documented somewhere in this repo, none of them
visible to an outside adviser.

**1. Bit-identity stops being free.** `ARTEFACT_CONTRACT` §3.2 trusts the
artefact's `peak`, `trim`, `articulation` and `hasGate` precisely because
faustwasm-on-V8 *is* the reference `FaustEngine` matches at 33/33, maxAbsDiff
0.0. wasm3, WAMR and Pulley execute those same bytes, so they inherit that by
construction — modulo the double-precision math imports, which `host_wasm3.c`
already gets right. A Faust interpreter compiled from the same source by a
different backend does not. iOS would then have to measure peaks itself, which
is exactly what **B3** exists to eliminate.

**2. A second libfaust is a drift risk this project already named.**
`faust-renderer.js` refuses to add faustwasm as a dependency of
kromosynth-render because *"a second copy could drift to a different libfaust
than the one the substrate was evolved and measured against."* A native iOS
libfaust is a larger version of the same risk: different platform, different
build, possibly a different version from the **faustwasm 0.16.1** in
kromosynth's tree. Shipping server-produced FBC narrows this to a bytecode
format-version question rather than eliminating it.

**3. The artefact carries neither the source nor the compile args.**
`ARTEFACT_CONTRACT` §1 lists `instrument.wasm`, `instrument.json`,
`synthis.json`. Adding `instrument.dsp` is nearly free —
`compileFaustArtefact(source, args)` already holds the source. The args are the
trap: nothing records them anywhere, and `faust-artefact.js:333` already carries
the comment that the same args must reach both compile and measure. **Source
shipped without its args is a program the device may compile differently from
the one the platform measured.** If D′ proceeds, `measured` gains a
`compileArgs` field and the schema version increments.

And a product question, not an implementation one: shipping `.dsp` ships the
genome in readable form. Under "own what you generate" that may be fine. It is a
decision, not a detail, and the FBC variant sidesteps it.

---

## The hardware plan

**This machine, checked 2026-09-06:**

- Xcode 27.0 (27A5252f), iOS 27.0 SDK
- **`iPhone bthj` — iPhone 13 Pro Max (iPhone14,3), A15, UDID
  `00008110-000120D9228A801E`, paired and available**
- Signing identity `Apple Development: Bjorn Thor Jonsson (A63TE5AF96)`
- wasm3, WAMR 2.4.5 and the wasmtime C API tarball all already cloned under
  `probe/out/wasm-runtime/`
- `probe/gate0/out/` holds built `host_wasm3`, `host_wasmtime` and `host_wamr`

The A15 is a 2021 part, which is the right direction for a floor: a cap measured
there understates a current phone rather than overstating it. Say so when
reporting the number.

### One harness, runtimes added in stages

A plain iOS app. **Not an AUv3** — app-extension packaging, a plugin UI on a
phone screen and inter-app audio are three separate problems, and none of them
matter if the answer is bad. One tiny C interface (`load / render / free`), one
implementation per runtime behind it, one results table printed on screen and
written as JSON to the app container so it can be pulled with `devicectl`.

It bundles **~10 elites spanning the measured RTF range, and the slowest decile
must be in there** — that is the point of the gate, not an afterthought.

- **H0 — wasm3 on the phone.** No cross-compile, no new dependency, existing
  driving code. This alone answers the session's question. Get here first, in
  one sitting, before anything else is attempted.
- **H1 — WAMR fast-interp.** CMake platform variables changed; the gate0
  `CMakeLists.txt` already sets every relevant one explicitly.
- **H2 — libfaust interpreter.** Only after H0 has produced a number, because
  H2's build is the one that can eat a session. Verify the machine-only build
  question first.
- **H3 — Pulley.** Only if H0–H2 all land marginal. Rust cross-compile, last on
  the vendors' own performance claim.

**`SESSION_PROMPTS.md:150` says do not install wasm3, WAMR or libfaust without
asking first.** wasm3 and WAMR are already on disk. **libfaust for iOS is a new
install and needs asking.**

### Pre-registered decision rule

Written before the measurement, per standing practice. A failed rule is the
result, not a reason to re-tune.

1. The reported cap is the **largest voice count at which the slowest decile
   holds ≥4× RTF**, at the sample rate iOS actually uses, one instance per
   voice, taken on the physical device.
2. The winning runtime is the one with the highest such cap, **except** that a
   runtime within **15%** of the best does not win on merit. Ties break toward
   the one that preserves bit-identity — wasm3, WAMR or Pulley over D′ — because
   §"what D′ costs" is a real bill and 15% is not worth paying it.
3. Peak RSS at the reported cap is recorded in the same run. If it exceeds the
   AUv3 extension budget, that is the binding constraint and the RTF number is
   informational.
4. Every number is taken **twice**: once cold, once after four minutes of
   sustained render. If they differ by more than 10%, the thermal number is the
   cap.

---

## What to reuse, and what to leave alone

**Reuse `probe/gate0/host_wasm3.c`.** It produced the reference numbers, and the
ABI documented at the top of `FaustEngine.h` was derived from it — that header
says so explicitly. `host_wamr.c` sits beside it.

**Do not port `FaustEngine` wholesale.** Voice allocation, per-pitch levelling,
the forced release, the multiplying envelope, the trim table — none of it is
needed to answer the question, and all of it is shaped by the desktop. Porting
it first turns a measurement into a project.

**Do not abstract the backend yet.** The cmake note is deliberate: the
abstraction gets built when there is a second real consumer. A measurement
harness is not yet that consumer. Measure, then decide what the seam has to
carry — and note that D′ would make the seam carry a different *artefact*, not
just a different runtime, which is an argument for measuring before abstracting
rather than against.

---

## Pitfalls, each of which has already cost time once

**Math imports compute in DOUBLE.** faustwasm binds `_powf` to `Math.pow`; a C
host calling `powf()` diverged by −50.5 dB on a math-heavy elite.
`host_wasm3.c` already has this right. Do not "fix" it. *(Does not apply to D′,
which never goes through the wasm import table — which is itself a reason its
output cannot be assumed bit-identical.)*

**Peak is a function of SAMPLE RATE.** Up to 3.8 dB at one pitch between 48 kHz
and 44.1 kHz, and not by a uniform scaling. Measure at the rate iOS will
actually use.

**RTF does not transfer between platforms.** V8 against wasmtime gave ratios of
0.48–0.99 with no usable scaling factor, and the ratio tracked genome cost. The
desktop numbers predict nothing about the phone, which is the entire reason this
has to run on hardware. **The same caution applies to runtime-vs-runtime
ratios** — a Mac comparison of wasm3 against D′ would narrow the choice but
would not transfer as a number.

**Real device, not the simulator.** `EXPERIMENT_01_DSP_EXECUTION` §2 says it
outright: an iOS simulator result does not count, it runs on the host CPU and
will flatter the answer. There are eleven simulators paired on this machine and
exactly one phone. Check which one it deployed to.

**Note whether the number was taken cold.** A cap measured on a cool phone that
collapses after four minutes in AUM is not the cap. Rule 4 above makes this
mandatory rather than optional.

---

## After Gate 0, if it is viable

In order, and detailed in `NEXT_SESSION_PROMPT.md`:

- **B1** per-rate peak tables. Peaks are portable across runtimes — the null
  test has wasmtime and V8 bit-identical over 33 elites — but not across sample
  rates, so one stored table is not valid. *If D′ wins, "portable across
  runtimes" needs re-establishing before this can be relied on.*
- **B2** split the contract's trust rule. `ARTEFACT_CONTRACT` §3.2 gates
  everything on `rtfPlatform` matching; peaks should gate on **sample rate**
  alone and only `rtf` needs runtime identity. The rule conflated them because
  one render produces both.
- **B3** ask whether `rtf` is needed at load at all. Its only consumer is the
  voice cap, which could start conservative and adapt from real CPU during
  playback. If that works, a matching-rate artefact needs **no load-time render**
  — 1.2–4.8 s becomes instant, which on an interpreter is the difference between
  viable and not.

Then, and only then, AUv3 — and it brings a constraint nothing in this repo has
accounted for:

- **App Groups.** An AUv3 extension runs in its own sandbox and **cannot read
  the host app's Documents directory**. A synth downloaded by the standalone app
  is invisible to the extension unless both go through a shared container
  (`containerURLForSecurityApplicationGroupIdentifier`). `InstrumentLibrary`
  resolves against `~/Music/Synth.is/Instruments` today, which has no iOS
  analogue at all. This is a real change to the install path and it belongs
  ahead of any AUv3 work, not after it.

---

## What that external discussion got wrong

Recorded so the next session does not re-derive it, and because two of these are
the kind of error that looks authoritative.

- **"Apple does not allow dynamic `wasm` loading in native iOS apps."** Conflates
  data with code. A `.wasm` file read from disk and interpreted is data, like a
  MIDI file. What is prohibited is writing executable pages at runtime. Path B
  was never in jeopardy, and the whole "bypass WASM entirely" framing is built on
  this error.
- **Strategy 2, the pre-compiled "synth matrix".** Fifty `.dsp` files compiled to
  fifty C++ headers with an `if (synthID == 1)` factory. That is path A, which
  §1 already rejected because it fixes the catalogue at build time. For an
  open-ended evolutionary archive it is not a trade-off, it is a contradiction.
- **`make ios INTERPRETER=1`.** Not in Faust's documentation. See the `ioslib`
  target and `BACKENDS=interp.cmake` above.
- **It treats "bypass WASM" as free.** It is not: see "what D′ costs".

---

## Out of scope

The desktop track. `A-i` and everything else in `NEXT_SESSION_PROMPT.md`.

---

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
