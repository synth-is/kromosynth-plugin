#!/usr/bin/env bash
# ios-log.sh — find and pull the plugin's log off the phone.
#
# STATUS: THE RETRIEVAL PATH IS UNVERIFIED. Read the next section before
# trusting anything this prints.
#
# WHY THAT WARNING IS THE FIRST THING HERE
#
# AUV3_IOS_FINDINGS C16 is a RETRACTION of C8. C8 claimed devicectl could read
# the App Group container; C16 established that the view devicectl returns is
# frozen as of install time and "lies plausibly" — the plugin's own display was
# right and the tool was believed for an hour instead.
#
# So this script PROBES rather than asserts. It tries every container domain the
# log could live in, prints which answered and which did not, and pulls only
# from one that actually produced a file. If none does, it says so plainly
# rather than reporting an empty log as an empty session.
#
# When this has been confirmed against a real device, replace this header with
# what was actually true and delete the domains that turned out to be wrong.
#
# ── Where the log is, inside whichever container ─────────────────────────────
#
# PluginProcessor.cpp:201 —
#
#     userHomeDirectory / Library/Logs/Synth.is /
#         stage0-<hostName>-<wrapperType>.log
#
# hostName comes from File::hostApplicationPath, which in an OUT-OF-PROCESS
# extension resolves to the appex's own executable, not to AUM. So do not go
# looking for a filename with the host's name in it: for the AUv3 expect the
# appex, and expect the standalone app to write an entirely different file.
# That is also why this globs the directory instead of guessing a filename.
#
# ── What you actually want out of it ────────────────────────────────────────
#
# The C22 measurement. block_over_budget lines carry a reconstructed wall clock,
# so they read directly against the instrument_load line above them.
# --correlate prints just those two kinds of line, in order, which puts the
# whole comparison on one screen:
#
#   block=3 with voices=0, a few hundred ms after a load  -> resume transient
#   a four-digit block= with voices=3                     -> the genome
#
# ── Two mechanical traps, both already paid for ─────────────────────────────
#
# `devicectl device copy from --destination` needs a full FILE path, not a
# directory. And devicectl prints "null" for several unrelated failures
# including a malformed --help, so a zero exit is not evidence on its own —
# which is C25's error in miniature and why a listing only counts when it
# actually mentions a .log.
#
# Written for bash 3.2.57, which is what /bin/bash is here: no mapfile, and no
# bare empty arrays, which trip `set -u` on that version.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICE="${KROMOSYNTH_IOS_DEVICE:-00008110-000120D9228A801E}"
QUERY_TIMEOUT="${KROMOSYNTH_IOS_QUERY_TIMEOUT:-25}"
OUT_DIR="${KROMOSYNTH_IOS_LOG_DIR:-$REPO_ROOT/build/device-logs}"
LOG_SUBDIR="Library/Logs/Synth.is"

CORRELATE=0
[ "${1:-}" = "--correlate" ] && CORRELATE=1

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say()  { printf '%s\n' "$*"; }
fail() { printf '\nFAIL  %s\n' "$*" >&2; exit 1; }

# Callers MUST use `rc=0; run_bounded ... || rc=$?`. Writing
# `if ! run_bounded ...; then rc=$?` reads the status of the NEGATION, which is
# always 0, so the timeout branch would never fire. That bug was in this file
# before it ever ran.
run_bounded() {
  local secs="$1" out="$2"; shift 2
  : > "$out"
  "$@" > "$out" 2>&1 &
  local pid=$! waited=0
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$waited" -ge "$secs" ]; then
      kill -TERM "$pid" 2>/dev/null || true
      sleep 1
      kill -KILL "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 124
    fi
    sleep 1
    waited=$(( waited + 1 ))
  done
  wait "$pid" || return $?
}

# Every place the log could be, most to least likely for the AUv3.
# "domain-type|domain-identifier"
DOMAINS="appDataContainer|is.synth.plugin.pluginAUv3
appDataContainer|is.synth.plugin
appGroupDataContainer|group.is.synth.plugin"

# ── Is the device answering at all? ─────────────────────────────────────────

rc=0
run_bounded "$QUERY_TIMEOUT" "$TMP/lock" \
  xcrun devicectl device info lockState -d "$DEVICE" || rc=$?

if [ "$rc" = 124 ]; then
  fail "device did not answer within ${QUERY_TIMEOUT}s — almost always a locked
      phone. Unlock it and rerun.

      This is the SAME hang that makes an empty listing indistinguishable from
      an empty log directory, which is why it stops here."
elif [ "$rc" != 0 ]; then
  fail "could not query the device (rc=$rc)."
fi
say "device is answering"
say ""

# ── Probe ───────────────────────────────────────────────────────────────────

found_type=""
found_id=""

while IFS= read -r entry; do
  [ -n "$entry" ] || continue
  dtype="${entry%%|*}"
  did="${entry##*|}"

  printf '  %-22s %-34s ' "$dtype" "$did"

  rc=0
  run_bounded "$QUERY_TIMEOUT" "$TMP/ls" \
    xcrun devicectl device info files -d "$DEVICE" \
      --domain-type "$dtype" --domain-identifier "$did" \
      --subdirectory "$LOG_SUBDIR" || rc=$?

  if [ "$rc" = 124 ]; then printf 'timed out\n'; continue; fi
  if [ "$rc" != 0 ];   then printf 'no\n';        continue; fi

  if grep -qi '\.log' "$TMP/ls"; then
    printf 'FOUND\n'
    cp "$TMP/ls" "$TMP/listing"
    found_type="$dtype"
    found_id="$did"
    break
  fi

  if grep -qix 'null' "$TMP/ls"; then
    printf 'null (means nothing — see C25)\n'
  else
    printf 'answered, no .log\n'
  fi
done <<EOF
$DOMAINS
EOF

if [ -z "$found_type" ]; then
  say ""
  fail "no container produced a log file.

      This does NOT establish that the log is absent — see the header, and see
      C16. It establishes that these three domains did not return one.

      Next, in order:
        1. Confirm the app is installed:
             xcrun devicectl device info apps -d $DEVICE | grep -i synth
        2. Read the figures off the plugin's own footer in AUM instead. The
           live load numbers are on screen and need no log at all; only the
           block_over_budget lines require this file.
        3. Trust the device over the tooling (C16). If the plugin displays a
           path, that path is the real one."
fi

say ""
say "listing from $found_id:"
sed 's/^/  /' "$TMP/listing"

# ── Pull ────────────────────────────────────────────────────────────────────

mkdir -p "$OUT_DIR"
stamp="$(date '+%Y%m%d-%H%M%S')"

# Filenames read OUT of the listing rather than guessed: hostName is not the
# host's name in an out-of-process extension, so guessing is how you pull
# nothing and conclude the session was empty.
names="$(grep -oE '[A-Za-z0-9._-]+\.log' "$TMP/listing" | sort -u)"

[ -n "$names" ] || fail "the listing mentioned a .log but no filename could be
      parsed out of it. Pull by hand with devicectl device copy from."

pulled_index="$TMP/pulled"
: > "$pulled_index"

while IFS= read -r name; do
  [ -n "$name" ] || continue
  dest="$OUT_DIR/$stamp-$name"
  say ""
  say "pulling $name"

  rc=0
  run_bounded 120 "$TMP/cp" \
    xcrun devicectl device copy from -d "$DEVICE" \
      --domain-type "$found_type" --domain-identifier "$found_id" \
      --source "$LOG_SUBDIR/$name" --destination "$dest" || rc=$?

  if [ "$rc" = 0 ] && [ -f "$dest" ]; then
    printf '%s\n' "$dest" >> "$pulled_index"
    say "  -> $dest"
  elif [ "$rc" = 0 ]; then
    say "  copy exited 0 but nothing landed — see C16."
  else
    say "  failed (rc=$rc). --destination needs a full FILE path, which this"
    say "  passes; if it still fails the domain is probably wrong."
    sed 's/^/    /' "$TMP/cp"
  fi
done <<EOF
$names
EOF

[ -s "$pulled_index" ] || fail "nothing was pulled."

# ── Report ──────────────────────────────────────────────────────────────────

say ""
while IFS= read -r f; do
  [ -n "$f" ] || continue
  over="$(grep -c 'block_over_budget' "$f" || true)"
  loads="$(grep -c 'instrument_load ' "$f" || true)"
  lines="$(wc -l < "$f" | tr -d ' ')"
  say "$(basename "$f"): $lines lines, $loads loads, $over over-budget blocks"
done < "$pulled_index"

if [ "$CORRELATE" = 1 ]; then
  say ""
  say "loads and over-budget blocks, in order:"
  say ""
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    grep -hE 'instrument_load |block_over_budget' "$f" | sed 's/^/  /' || true
  done < "$pulled_index"
  say ""
  say "voices=0 with a low block= shortly after a load is a resume transient."
  say "A high block= with voices>0 is the genome. That distinction is the"
  say "measurement; C22's peak figure on its own is not."
fi
