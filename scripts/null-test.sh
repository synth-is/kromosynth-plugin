#!/usr/bin/env bash
# null-test.sh — has FaustEngine's output changed?
#
# WHY THIS EXISTS
#
# The wasm3 backend for iOS has to touch FaustEngine.cpp, which the DESKTOP
# plugin compiles. "I did not change desktop behaviour" is a claim, and this
# turns it into a byte comparison.
#
# faust-probe links FaustEngine.cpp DIRECTLY (tools/faust-probe/CMakeLists.txt),
# so this exercises the engine rather than the runtime underneath it. With
# --plan and --ref it runs runNullTest: one voice, the plan's own rate and block
# size, rendered through FaustEngine and compared sample-for-sample against the
# platform's reference render.
#
# BASELINE, captured 2026-09-07 before any wasm3 work, on wasmtime/Cranelift:
#
#     PASS 120   FAIL 0   maxAbsDiff 0.000000e+00 on every elite
#
# Full per-elite record in docs/BASELINE_FAUSTENGINE_2026-09-07.txt. That is 120
# elites against the 33 ARTEFACT_CONTRACT §3.2 records, so it is a stronger
# baseline than the contract claims, not a weaker one.
#
# WHAT IT COVERS, AND WHAT IT DOES NOT
#
# Covers: rendering, levelling, trim, the envelope, parameter offsets, the
# module ABI, the math imports -- anything that changes a sample.
#
# Does NOT cover: voice allocation and stealing under real MIDI, host threading,
# the editor, acquisition. A pass here does not mean the plugin is fine; it
# means the audio path is unchanged.
#
#   ./scripts/null-test.sh                 # against the committed baseline
#   ./scripts/null-test.sh --write out.txt # record a new baseline

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART="${KROMOSYNTH_GATE0_ARTEFACTS:-$HERE/../kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/probe/out/gate0}"
BASELINE="$HERE/docs/BASELINE_FAUSTENGINE_2026-09-07.txt"

PROBE="$(find "$HERE/build" -name faust_probe -type f -perm +111 2>/dev/null | head -1)"
if [ -z "$PROBE" ]; then
  echo "faust_probe not built. Configure with -DKROMOSYNTH_BUILD_FAUST=ON and:" >&2
  echo "    cmake --build <builddir> --target faust_probe" >&2
  exit 1
fi
if [ ! -d "$ART" ]; then
  echo "artefacts not found at: $ART" >&2
  echo "set KROMOSYNTH_GATE0_ARTEFACTS to the directory holding" >&2
  echo "  <elite>.wasm / .json / .plan / .wasmtime-archive.f32" >&2
  exit 1
fi

WRITE=""
[ "${1:-}" = "--write" ] && WRITE="${2:-/dev/stdout}"

cd "$ART"
tmp="$(mktemp)"
pass=0; fail=0

for w in *.wasm; do
  b="${w%.wasm}"
  [ -f "$b.json" ] && [ -f "$b.plan" ] && [ -f "$b.wasmtime-archive.f32" ] || continue

  out="$(timeout 60 "$PROBE" "$w" "$b.json" --plan "$b.plan" --ref "$b.wasmtime-archive.f32" 2>&1)"
  d="$(echo "$out" | grep -oE 'maxAbsDiff [0-9.e+-]+' | awk '{print $2}')"

  if echo "$out" | grep -q '^PASS'; then pass=$((pass+1)); s=PASS; else fail=$((fail+1)); s=FAIL; fi
  printf "%-6s %-14s %s\n" "$s" "${d:-?}" "$b" >> "$tmp"
done

echo "PASS $pass   FAIL $fail   of $((pass+fail))"

if [ -n "$WRITE" ]; then
  cp "$tmp" "$WRITE"
  echo "baseline written to $WRITE"
  rm -f "$tmp"
  exit 0
fi

if [ ! -f "$BASELINE" ]; then
  echo "no committed baseline at $BASELINE — nothing to compare against" >&2
  rm -f "$tmp"; exit 1
fi

# Diffed rather than just counted. A run that passes 120/120 with a DIFFERENT
# maxAbsDiff has still changed the audio, and a count would hide that.
if diff -q "$BASELINE" "$tmp" > /dev/null; then
  echo "IDENTICAL to the committed baseline."
  rm -f "$tmp"; exit 0
fi

echo
echo "CHANGED against the committed baseline:"
diff "$BASELINE" "$tmp" | head -40
rm -f "$tmp"
exit 1
