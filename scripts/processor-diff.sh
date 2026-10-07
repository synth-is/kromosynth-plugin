#!/usr/bin/env bash
# processor-diff.sh — render a fixed MIDI arpeggio through the plugin's own
# KromosynthAudioProcessor on two builds, headless, and diff the audio.
#
# KEEPABLE. The gate that reaches PluginProcessor (synthis-59r.16). faust_probe
# and engine_probe drive an engine directly; nothing else exercises the
# processor's parameter path, voice allocation under host MIDI or the ADSR
# adoption on load, which is how the 2026-09-05 polyphony regression shipped.
#
#   scripts/processor-diff.sh                      # candidate against itself:
#                                                  #   deterministic? (the gate)
#   scripts/processor-diff.sh --apply 520b122      # candidate against the same
#                                                  #   tree with that commit
#                                                  #   applied (re-introduced)
#   scripts/processor-diff.sh --revert <commit>    # ... or reverse-applied
#   scripts/processor-diff.sh --ref-probe <bin>    # against a processor_probe
#                                                  #   you built elsewhere
#
# Exit codes: 0 identical, 1 DIVERGED (audio or voice counts differ),
#             2 the harness could not run (build failed, instrument did not
#               load, silent render) -- never read as a result.
#
# No REAPER, no installed plugin, no GUI: tools/processor-probe compiles
# PluginProcessor.cpp into a console executable, so this runs on the Studio and
# in the agent loop as well as on the laptop.
#
# WHAT REPLACES -splashlog. make_projects.py insists the Reaper log is read on
# every render because a silent WAV and an unloaded FX slot look the same. The
# probe prints the same facts itself and this script refuses to compare without
# them: loaded=1, load_ok=1 (measurement finished), param_release_found=1 (the
# parameter write reached a real parameter), render_peak > 0 (not silence).
#
# VOICE CAP IS PINNED (KROMOSYNTH_PROBE_VOICES, default 16). The plugin derives
# it from a timed render, so it moves with machine load and two renders of one
# build then differ for reasons that are not the code. The reference tree gets
# the same pin because it is a copy of this one.
#
# Fixtures: two Gate 0 elites copied into a throwaway library
# (KROMOSYNTH_PROBE_LIBRARY): `adsr` publishes att/dec/sus/rel, so the ADSR
# adoption in 520b122 shows; `plain` does not. Override the artefact directory
# with KROMOSYNTH_GATE0_ARTEFACTS (same variable as null-test.sh).
#
# The reference tree for --revert lives in build/processor-diff-ref and is kept,
# so the second run is an incremental build.

set -uo pipefail
HERE="$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)"
cd "$HERE" || exit 2

PATCH=""; PATCH_MODE=""; REF_PROBE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply)     PATCH="${2:?--apply needs a commit}";  PATCH_MODE=apply;  shift 2 ;;
    --revert)    PATCH="${2:?--revert needs a commit}"; PATCH_MODE=revert; shift 2 ;;
    --ref-probe) REF_PROBE="${2:?--ref-probe needs a path}"; shift 2 ;;
    *) echo "usage: $0 [--apply <commit> | --revert <commit> | --ref-probe <processor_probe>]" >&2; exit 2 ;;
  esac
done

ART="${KROMOSYNTH_GATE0_ARTEFACTS:-$HERE/../kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/probe/out/gate0}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
CFG=(-G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DKROMOSYNTH_BUILD_SAMPLER=ON
     -DKROMOSYNTH_BUILD_FAUST=ON -DKROMOSYNTH_COPY_AFTER_BUILD=OFF -DKROMOSYNTH_SIGN_IDENTITY=)

build_probe() {   # <source-dir> <build-dir>  -> prints the probe path
  cmake -S "$1" -B "$2" "${CFG[@]}" >/dev/null 2>&1 || { echo "configure failed: $1" >&2; return 1; }
  cmake --build "$2" --target processor_probe -j "$JOBS" >"$2/processor_probe.log" 2>&1 \
    || { echo "processor_probe did not build; see $2/processor_probe.log" >&2; return 1; }
  find "$2" -name processor_probe -type f -perm +111 | head -1
}

echo "── candidate: build processor_probe"
CAND="$(build_probe "$HERE" "$HERE/build/verify-nocopy")" && [[ -x "$CAND" ]] || exit 2
CAND="$HERE/${CAND#"$HERE/"}"

if [[ -n "$REF_PROBE" ]]; then
  REF="$REF_PROBE"; REFNAME="$REF_PROBE"
elif [[ -n "$PATCH" ]]; then
  REFNAME="working tree, $PATCH_MODE $PATCH"
  echo "── reference: $REFNAME"
  RT="$HERE/build/processor-diff-ref"
  mkdir -p "$RT/src-tree" || exit 2
  # Everything but build products and the submodules, which are linked rather
  # than copied (they are hundreds of MB and never change here).
  rsync -a --delete --exclude .git --exclude build --exclude third_party \
        --exclude 'tools/*/out' "$HERE/" "$RT/src-tree/" || exit 2
  [[ -e "$RT/src-tree/third_party" ]] || ln -s "$HERE/third_party" "$RT/src-tree/third_party"
  # patch(1), not git apply: the tree sits inside this repository's build/, and
  # git apply there silently skips every path outside the current subdirectory
  # and reports success -- a reference that was never changed.
  PFLAGS=(-p1 -s); [[ "$PATCH_MODE" == revert ]] && PFLAGS+=(-R)
  git -C "$HERE" diff "$PATCH^" "$PATCH" -- src CMakeLists.txt cmake >"$RT/change.patch" \
    && (cd "$RT/src-tree" && patch "${PFLAGS[@]}" -i "$RT/change.patch") \
    || { echo "could not $PATCH_MODE $PATCH onto the working tree" >&2; exit 2; }
  if diff -rq "$HERE/src" "$RT/src-tree/src" >/dev/null; then
    echo "reference tree is identical to the candidate after $PATCH_MODE $PATCH; refusing to compare" >&2; exit 2
  fi
  REF="$(build_probe "$RT/src-tree" "$RT/build")" && [[ -x "$REF" ]] || exit 2
  REF="$HERE/${REF#"$HERE/"}"
else
  REF="$CAND"; REFNAME="the candidate again (determinism)"
fi

[[ -d "$ART" ]] || { echo "Gate 0 artefacts not found: $ART" >&2; exit 2; }
LIB="$(mktemp -d)"; OUT="$(mktemp -d)"
trap 'rm -rf "$LIB" "$OUT"' EXIT
install_fixture() {   # <id> <elite>
  mkdir -p "$LIB/Instruments/$1"
  cp "$ART/$2.wasm" "$LIB/Instruments/$1/instrument.wasm" && cp "$ART/$2.json" "$LIB/Instruments/$1/instrument.json" \
    || { echo "missing fixture $2 in $ART" >&2; exit 2; }
}
install_fixture adsr  elite_00_cppnwave_c31-21
install_fixture plain elite_00_deep_pad_c10-29

# kv <file> <key>
kv() { sed -n "s/^$2=//p" "$1" | head -1; }

# Runs one probe and refuses a result it cannot trust.
run() {   # <probe> <id> <tag>
  KROMOSYNTH_PROBE_LIBRARY="$LIB" KROMOSYNTH_PROBE_VOICES="${KROMOSYNTH_PROBE_VOICES:-16}" "$1" "$2" "$OUT/$3-$2.wav" >"$OUT/$3-$2.txt" 2>"$OUT/$3-$2.err"
  local rc=$?
  if (( rc != 0 )) || [[ "$(kv "$OUT/$3-$2.txt" loaded)" != 1 || "$(kv "$OUT/$3-$2.txt" load_ok)" != 1 \
        || "$(kv "$OUT/$3-$2.txt" param_release_found)" != 1 ]]; then
    echo "── $3/$2: probe did not load/measure/find the release parameter (rc=$rc)" >&2
    cat "$OUT/$3-$2.txt" "$OUT/$3-$2.err" >&2; return 1
  fi
  if ! awk -v p="$(kv "$OUT/$3-$2.txt" render_peak)" 'BEGIN { exit !(p > 0) }'; then
    echo "── $3/$2: rendered silence (render_peak=$(kv "$OUT/$3-$2.txt" render_peak)); not a result" >&2
    return 1
  fi
}

DIVERGED=0
COUNTS="note_ons note_offs starts_idle starts_reused starts_stolen force_stopped max_active_voices voice_cap"
for ID in adsr plain; do
  echo
  echo "── instrument: $ID    candidate vs $REFNAME"
  run "$CAND" "$ID" cand || exit 2
  run "$REF"  "$ID" ref  || exit 2

  printf '   %-20s %12s %12s\n' voice-count candidate reference
  for K in $COUNTS; do
    A="$(kv "$OUT/cand-$ID.txt" $K)"; B="$(kv "$OUT/ref-$ID.txt" $K)"
    MARK=""; [[ "$A" != "$B" ]] && { MARK="  <-- differs"; DIVERGED=1; }
    printf '   %-20s %12s %12s%s\n' "$K" "$A" "$B" "$MARK"
  done

  python3 "$HERE/tools/processor-probe/diff_wav.py" "$OUT/cand-$ID.wav" "$OUT/ref-$ID.wav" >"$OUT/diff-$ID.txt"
  DRC=$?
  (( DRC == 2 )) && { echo "diff_wav.py could not read the renders" >&2; exit 2; }
  sed 's/^/   /' "$OUT/diff-$ID.txt"
  (( DRC != 0 )) && DIVERGED=1
done

echo
if (( DIVERGED )); then
  echo "processor-diff: DIVERGED"; exit 1
fi
echo "processor-diff: identical"
