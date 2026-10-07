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
#   4. scripts/processor-diff.sh: a fixed arpeggio (voice stealing, a
#      re-trigger, a release-parameter write) rendered through
#      KromosynthAudioProcessor itself, twice; the two renders and voice counts
#      must be identical (synthis-59r.16/.32). ~40 s.
# What it does NOT cover: whether the processor's output is RIGHT (step 4
# proves it deterministic; compare against an older build with
# processor-diff.sh --revert/--ref-probe), the editor, iOS. A green gate is not
# a reviewed change.

set -uo pipefail
cd "${HARNESS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}" || exit 2
B="build/verify-nocopy"

echo "── 1/4 build (scripts/build.sh verify)"
bash scripts/build.sh || exit 2
cmake --build "$B" --target acquire_probe faust_probe -j "$(sysctl -n hw.ncpu)" >/dev/null || {
  echo "the probes did not build"; exit 2; }

echo "── 2/4 acquire_probe selftest"
AP="$(find "$B" -name acquire_probe -type f -perm +111 | head -1)"
[[ -x "$AP" ]] || { echo "acquire_probe not found under $B"; exit 2; }
T="$(mktemp -d)"; "$AP" selftest "$T"; rc=$?; rm -rf "$T"
(( rc == 0 )) || { echo "acquire_probe selftest failed (rc=$rc)"; exit 1; }

echo "── 3/4 null test (FaustEngine against the baseline)"
# Absolute: null-test.sh cd's into the reference directory before running it.
KROMOSYNTH_FAUST_PROBE="$PWD/$(find "$B" -name faust_probe -type f -perm +111 | head -1)" \
  bash scripts/null-test.sh || exit 1

echo "── 4/4 processor render (scripts/processor-diff.sh: candidate against itself)"
bash scripts/processor-diff.sh; rc=$?
case "$rc" in
  0) ;;
  1) echo "the processor rendered the same script differently twice"; exit 1 ;;
  *) echo "processor-diff.sh could not run (rc=$rc)"; exit 2 ;;
esac
echo "gate: green"
