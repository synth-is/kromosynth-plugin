#!/usr/bin/env bash
# gate.sh — the agent loop's check for this repo (bdloop: harness.config.jsonc).
#
# KEEPABLE. Exit codes are the contract with the runner:
#   0  green
#   1  a check ran and failed (null test, acquisition self-test)
#   2  it did not build
#
# What it covers, in order of cost:
#   1. scripts/build.sh (verify): AU + VST3 compile, Faust engine and sampler
#      ON, nothing installed or signed.
#   2. acquire_probe selftest: the acquisition / render-outbox path.
#   3. scripts/null-test.sh: FaustEngine's output, sample for sample, against
#      docs/BASELINE_FAUSTENGINE_2026-09-07.txt (needs the kromosynth-projects
#      sibling, which the runner links beside its worktrees).
# What it does NOT cover: voice allocation under real MIDI, the processor's
# parameter path, the editor, iOS (synthis-59r.16 is the gate for the
# processor). A green gate is not a reviewed change.

set -uo pipefail
cd "${HARNESS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}" || exit 2
B="build/verify-nocopy"

echo "── 1/3 build (scripts/build.sh verify)"
bash scripts/build.sh || exit 2
cmake --build "$B" --target acquire_probe faust_probe -j "$(sysctl -n hw.ncpu)" >/dev/null || {
  echo "the probes did not build"; exit 2; }

echo "── 2/3 acquire_probe selftest"
AP="$(find "$B" -name acquire_probe -type f -perm +111 | head -1)"
[[ -x "$AP" ]] || { echo "acquire_probe not found under $B"; exit 2; }
T="$(mktemp -d)"; "$AP" selftest "$T"; rc=$?; rm -rf "$T"
(( rc == 0 )) || { echo "acquire_probe selftest failed (rc=$rc)"; exit 1; }

echo "── 3/3 null test (FaustEngine against the baseline)"
# Absolute: null-test.sh cd's into the reference directory before running it.
KROMOSYNTH_FAUST_PROBE="$PWD/$(find "$B" -name faust_probe -type f -perm +111 | head -1)" \
  bash scripts/null-test.sh || exit 1
echo "gate: green"
