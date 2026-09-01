# Next session — kromosynth-plugin

Previous prompt preserved as `NEXT_SESSION_PROMPT.2026-08-31b.md`.

## Where things stand

**Stage 1 acquisition is complete and verified end to end on a live host** —
unchanged from the previous prompt, see `NEXT_SESSION_PROMPT.2026-08-31b.md`
for the detail.

**Gate 0's desktop half is now measured and Stage 2 is unblocked.** The full
result is `kromosynth-projects/Evolutionary Sound Discovery Platform/daw-plugin/EXPERIMENT_01_3_DESKTOP_RESULT.md`.
Read it before touching any of this — it contains two corrections to itself
(§2.5, §2.7), and reading only §0 will leave you with the version that was
wrong.

Deliberately **not** named `EXPERIMENT_01_RESULT.md`. That filename is still
reserved for the document that fires a §5 branch, and no branch fires — see
"What §5 needs" below.

### What was built

`kromosynth-projects/.../faust-genome-substrate/probe/gate0/` — four files of
host, driven by `probe/13-gate0-prepare.mjs`:

- `host_wasm3.c` — wasm3 interpreter, `--math float|double`, `--block N`,
  `--loadonly N`
- `host_wamr.c` — WAMR fast-interp, same CLI and JSON shape
- `host_wasmtime.c` — wasmtime v48 / Cranelift JIT, plus `--blockstats`
- `sweep.mjs`, `compare.mjs`, `build.sh`, `CMakeLists.txt`

Runtimes clone/download into `probe/out/wasm-runtime/` (gitignored). wasm3 and
WAMR build from source; wasmtime is a prebuilt C API tarball. `build.sh` does
wasm3; the other two have their own lines in the result doc's header.

Results in `probe/out/gate0/` — `survey.json`, `selection.json`, `all.json`,
`sweep-{wasm3,wamr,wasmtime}.json`, `sweep-{wasm3,wasmtime}-archive.json`.

### What was found

1. **Every elite plays on every platform. None is unplayable anywhere.** Only
   the voice count varies. Desktop under Cranelift: 8–32+ voices, median 16,
   100% of the archive. iOS under wasm3: 1–32 voices, median 4, 100% of the
   archive.
2. **The math imports must compute in DOUBLE.** faustwasm binds `_powf` to
   `Math.pow`, which is double and narrows on return. A C host calling `powf()`
   diverges to **−50 dB** on a math-heavy elite — audible. In double, all four
   engines are bit-identical to the platform reference across 33 elites.
   **This is the single most load-bearing implementation detail for Stage 2.**
3. **A native host is up to 2.2× faster than the browser** on the same module,
   because every math import crosses wasm→JS in a browser and that crossing is
   expensive. The web app is a floor this work clears, not a ceiling.
4. **Block size is a lever in JavaScript and not one in C** — flat 128→1024.
   Negative result, recorded so it is not measured again.
5. The ≥4× headroom bar decomposes into ~1.3× measured genome variance and
   ~3× unmeasured session budget.

## Task — pick one

### 1. Real-time probe with a time-constrained thread (recommended)

The only part of the headroom bar still resting on judgement is the session
term — how much CPU one instrument may claim before a busy DAW glitches. It
decides whether the voice caps above are the ≥4× column or the ≥2× one, which
on iOS is the difference between a median cap of 4 voices and 8.

Add a real-time mode to `host_wasmtime.c` and `host_wasm3.c`: render one block,
sleep to the next deadline, count misses. Report missed deadlines and worst
overrun.

**It must raise itself to a time-constrained thread policy** (macOS
`thread_policy_set`, `THREAD_TIME_CONSTRAINT_POLICY`) — the same thing
CoreAudio does. §2.6 already showed a normal-priority process invents overruns
that are not there: `blocksOverBudget` bounced 1, 4, 0, 0, 0 across five
identical runs while p99/mean sat still at ~1.2. Measuring the session term at
normal priority would repeat that error at larger scale and produce a bar that
is too conservative.

Then Björn runs one command with a typical session open and pastes the output.
Ask him; do not guess the number.

### 2. iOS memory, which may bind before RTF

`EXPERIMENT_01_4.1_RESULT` §1.4 says check memory before throughput on iOS, and
given §2.7's caps it is plausible memory is what actually binds. Measured peak
RSS at 8 voices: wasm3 5.1–22.1 MB, WAMR 13.7–22.7 MB, wasmtime 17.6–45.1 MB.

The hosts use **one instance per voice** — own module, own interpreter
bytecode, own linear memory. faustwasm's poly path shares code and duplicates
only the DSP struct, which would report lower. §5 measured the compute cost of
that choice at 1–4%; its **memory** cost is unmeasured and is the number that
matters for an AUv3 extension budget.

### 3. Stage 2 proper — the webview↔engine bridge

Gate 0 no longer blocks it. Note the audition-routing problem from Stage 0 §O
is still open and is the most consequential unresolved design decision.

### 4. Developer ID certificate and notarisation

Unchanged from the previous prompt.

## What §5 needs, and why no branch fired

§5's rules all ask "does B clear 8 voices at ≥4× on the slowest decile". The
measurement says the useful question is "how many voices does each genome get
on each platform" — a continuous answer where the rule wants a pass/fail.

§7 of the result doc proposed an amendment (per-genome cost gate + voice cap).
**§2.7 supersedes it** — the gate half solves a problem that does not exist,
since nothing is unplayable. What survives is the cap half: carry measured
single-voice RTF alongside the artefact, derive the per-platform cap
arithmetically. One render to compute at export; the predictor was validated
30/30.

Amending §5 is Björn's call and has not been made. Do not apply it unilaterally
and do not write `EXPERIMENT_01_RESULT.md` until a branch actually fires.

## Standing rules

- Read `STAGE0_FINDINGS.md` §O before touching plugin code.
- `git:git_commit` via MCP behaves like `git commit -a`. `kromosynth-projects`
  usually has unrelated pre-staged work — use `git add -- <paths>` then
  `git commit -F msg -- <paths>` from the terminal.
- **`git commit -F msg -- <paths>` silently does nothing for UNTRACKED files.**
  It exits non-zero and prints nothing. `git add` first, then verify with
  `git log`. This cost a silent no-op commit this session.
- `~/.gitignore_global` line 10 is a bare unanchored `render`. Never name a
  directory that.
- `filesystem:write_file` writes to the real disk; `create_file` writes to a
  sandbox. Never use `create_file` for project work.
- `filesystem:edit_file` `oldText` must be byte-perfect. Dry-run first — a
  dry-run this session caught an edit that would have collapsed two lines into
  one and swallowed a section heading.
- Nested functions and statement expressions are GCC extensions clang rejects.
- Do not run builds or clones while a timing sweep is running.
- The local MCP servers went unresponsive for a stretch this session — both
  `local-terminal` and `filesystem`, with a bare `echo alive` timing out. If
  that recurs, ask Björn to restart them rather than retrying.
