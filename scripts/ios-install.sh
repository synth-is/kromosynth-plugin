#!/usr/bin/env bash
# ios-install.sh — gate, then install the built app on the phone.
#
# WHY THIS EXISTS AS A GATE AND NOT A COMMAND
#
# AUV3_IOS_FINDINGS C15: installing under a live AU host took AUM down. The
# lesson recorded there is not "run the check" — it is "GATE on your checks, do
# not merely run them", written because the check was run in the same command
# as the install and the install happened anyway.
#
# A human-read check is a check that gets skipped at 23:00. This refuses.
#
# ── The trap that made this worth a script rather than a snippet ────────────
#
# Every `devicectl device info` call against a LOCKED phone HANGS and yields
# nothing. An empty process list reads exactly like "no AU hosts are running" —
# so the naive one-liner passes its own gate most convincingly at the moment it
# knows least. Observed directly: `device info lockState`, `info apps`,
# `info processes` and even `--help` all returned nothing while the device was
# listed as connected.
#
# So this script treats three things as failures, not as permission:
#
#   a query that times out          — the phone is locked or unresponsive
#   a process list that is EMPTY    — no real device has zero processes
#   a known AU host in that list    — the C15 hazard itself
#
# ── What is NOT the hazard ──────────────────────────────────────────────────
#
# Our own app. C15's note is explicit: "An AU host is the hazard; our own app
# running is not." A grep loose enough to match Synth.is would block every
# install after the first, so the host list is explicit and ours is excluded.
#
# ── Why this also solves the stale-binary problem ───────────────────────────
#
# An AU host loads a plugin binary ONCE per process lifetime. `processor_
# constructed` appearing again when the plugin window is reopened does not mean
# fresh code, and believing otherwise cost an hour (C0). Because this script
# refuses to install while a host is running, a host started AFTERWARDS is
# guaranteed to load the binary just installed. The gate buys freshness for
# free — which is a reason to keep it even on a day when nothing is at risk.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${KROMOSYNTH_IOS_BUILD_DIR:-$REPO_ROOT/build/ios-auv3}"
CONFIG="${KROMOSYNTH_IOS_CONFIG:-Release}"
DEVICE="${KROMOSYNTH_IOS_DEVICE:-00008110-000120D9228A801E}"   # iPhone bthj, 13 Pro Max
QUERY_TIMEOUT="${KROMOSYNTH_IOS_QUERY_TIMEOUT:-25}"

APP_REL="src/KromosynthPlugin_artefacts/$CONFIG/Standalone/Synth.is.app"
# KROMOSYNTH_IOS_APP lets a MODIFIED copy be installed through this same gate
# rather than around it -- see ios-icon-experiment.sh, which strips a plist key
# and re-signs into a scratch directory. A throwaway experiment is exactly the
# case where the temptation to skip the gate is highest, so it gets to reuse it.
APP="${KROMOSYNTH_IOS_APP:-$BUILD_DIR/$APP_REL}"
APPEX="$APP/PlugIns/Synth.is.appex"

# Explicit, and ours is not on it. Extend with KROMOSYNTH_IOS_AU_HOSTS.
AU_HOSTS="${KROMOSYNTH_IOS_AU_HOSTS:-AUM\.app|GarageBand|Loopy|Cubasis|Drambo|BeatMaker|NanoStudio|Audiobus|apeMatrix|AudioKit}"

DRY_RUN=0
WAIT=0
WAIT_TIMEOUT="${KROMOSYNTH_IOS_WAIT_TIMEOUT:-600}"

for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    --wait)    WAIT=1 ;;
  esac
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say()  { printf '%s\n' "$*"; }
fail() { printf '\nFAIL  %s\n' "$*" >&2; exit 1; }
ok()   { printf 'ok    %s\n' "$*"; }

# ── A bounded runner, because a hang must not present as a pass ─────────────
#
# No timeout(1) on macOS — BSD userland, and coreutils is not a dependency this
# repo carries. Backgrounding with a poll is portable AND lets a hang be
# reported as a hang rather than as an empty result, which is the entire point.
#
# Callers MUST capture the status as `rc=0; run_bounded ... || rc=$?`. Writing
# `if ! run_bounded ...; then rc=$?` reads the status of the NEGATION, which is
# always 0 — so the timeout branch never fires and the gate below silently
# degrades to the one-liner it was written to replace. Found in this file
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

# ── 1. The artefact ─────────────────────────────────────────────────────────

[ -d "$APP" ] || fail "nothing built at $APP — run ./scripts/ios-build.sh first."
[ -d "$APPEX" ] || fail "the app has no embedded appex at PlugIns/ —
      installing it would deliver no AUv3."

built_at="$(date -r "$(stat -f '%m' "$APPEX/Synth.is")" '+%Y-%m-%d %H:%M:%S')"
ok "app built $built_at"

# Loud rather than fatal: an old binary is sometimes exactly what you want to
# install, when bisecting.
if [ -n "$(find "$REPO_ROOT/src" -newer "$APPEX/Synth.is" -name '*.cpp' -o \
           -newer "$APPEX/Synth.is" -name '*.h' 2>/dev/null | head -1)" ]; then
  say "WARN  sources in src/ are NEWER than this binary."
  say "      You are about to install a build that predates your edits, which"
  say "      is the exact shape of C0. Run ios-build.sh unless this is"
  say "      deliberate."
fi

# ── 2. Is the device actually answering? ────────────────────────────────────

say ""
say "Device $DEVICE"

rc=0
run_bounded "$QUERY_TIMEOUT" "$TMP/lock" \
  xcrun devicectl device info lockState -d "$DEVICE" || rc=$?

if [ "$rc" = 124 ]; then
  fail "the device did not answer within ${QUERY_TIMEOUT}s.

      This is almost always a LOCKED phone. Unlock it and rerun.

      It matters more than it sounds: the same hang makes the AU-host check
      below return nothing, and nothing looks identical to \"no hosts are
      running\". That is why this stops here instead of carrying on."
elif [ "$rc" != 0 ]; then
  fail "devicectl could not query the device (rc=$rc). Output:
$(sed 's/^/      /' "$TMP/lock")"
fi
say "ok    device is answering"

# ── 3. THE GATE (C15) ───────────────────────────────────────────────────────

# ── 3a. WAIT for the host to go away, if asked ──────────────────────────────
#
# The gate below needs the host closed; checking the result needs it open. So
# every test cycle cost a round trip: refuse, ask, be told, retry. Four times
# in one afternoon.
#
# --wait polls until no host process is resident, then falls through to the
# real gate. Force-quit whenever you like and the install happens on its own.
#
# This does NOT relax the gate. It is a loop in front of it, and section 3
# still runs and still refuses -- so a host that appears between the last poll
# and the install is caught exactly as before.
if [ "$WAIT" = 1 ]; then
  waited=0
  while [ "$waited" -lt "$WAIT_TIMEOUT" ]; do
    rc=0
    run_bounded "$QUERY_TIMEOUT" "$TMP/waitprocs" \
      xcrun devicectl device info processes -d "$DEVICE" || rc=$?

    if [ "$rc" = 0 ] && [ "$(grep -c . "$TMP/waitprocs")" -ge 5 ]; then
      still="$(grep -v '/PlugIns/' "$TMP/waitprocs" | grep -iE "$AU_HOSTS" || true)"
      [ -z "$still" ] && { say "ok    host closed after ${waited}s"; break; }
      say "      waiting for the host to close (${waited}s)..."
    else
      say "      device not answering, retrying (${waited}s)..."
    fi

    sleep 2
    waited=$(( waited + 2 ))
  done
fi

rc=0
run_bounded "$QUERY_TIMEOUT" "$TMP/procs" \
  xcrun devicectl device info processes -d "$DEVICE" || rc=$?

if [ "$rc" = 124 ]; then
  fail "the process list timed out after ${QUERY_TIMEOUT}s.
      Refusing to install: an unanswered check is not a passed check."
elif [ "$rc" != 0 ]; then
  fail "could not read the process list (rc=$rc). Refusing to install."
fi

# No real device has an empty process list. If this is empty, the query failed
# in some way that still exited zero — and reading that as "no hosts running"
# is precisely the mistake this file exists to prevent.
proc_lines="$(grep -c . "$TMP/procs" || true)"
[ "${proc_lines:-0}" -ge 5 ] \
  || fail "the process list came back with $proc_lines lines, which cannot be
      a real answer. Treating it as UNKNOWN, not as safe. Unlock the phone,
      confirm with:

        xcrun devicectl device info processes -d $DEVICE | head

      and rerun."

# EXTENSIONS ARE NOT HOSTS, and the host list matches a substring of a path.
#
# MobileGarageBandThumbnailExtension renders GarageBand project thumbnails and
# hosts nothing, but its path contains "GarageBand" and so tripped the gate --
# refusing an install for a process that could not possibly be at risk. Seen
# 2026-09-10 as pid 9346, immediately after GarageBand itself had correctly
# been cleared, which made a working gate look broken.
#
# A process running out of a host's PlugIns directory is a plugin or an
# extension by construction. AU hosts run as apps. So they are dropped before
# the host names are matched at all -- which also excludes OUR OWN appex,
# though the host list never named us anyway.
host_candidates="$(grep -v '/PlugIns/' "$TMP/procs" || true)"
running_hosts="$(printf '%s\n' "$host_candidates" | grep -iE "$AU_HOSTS" || true)"

if [ -n "$running_hosts" ]; then
  say ""
  say "$(printf '%s\n' "$running_hosts" | sed 's/^/      /')"
  fail "a known AU host has a LIVE PROCESS. Refusing to install (C15 — this
      took AUM down).

      It may be SUSPENDED rather than active, and it may not show in the app
      switcher: iOS keeps an app's process alive after you leave it. Seen
      2026-09-10, when MobileGarageBand held pid 9177 across fifteen minutes
      while the phone appeared to have nothing running.

      Suspended is still a refusal, deliberately. C15's crash was inside the
      host's requestIcon during a plugin rescan, which replacing a bundle can
      trigger in a host that is merely resident — so 'our plugin is not loaded
      in it' is NOT the same as 'it is not at risk'. A narrower test keyed on
      our own appex would be weaker, not smarter.

      To clear it: OPEN the host, then swipe it away in the app switcher.
      Opening it first is what makes the force-quit actually end the process.
      A reboot also works.

      Force-quit rather than background either way: a host that stays alive
      keeps the OLD plugin binary loaded for its whole process lifetime, so
      even a successful install would not reach it."
fi
ok "no AU host process ($proc_lines processes seen)"

# ── 4. Install ──────────────────────────────────────────────────────────────

if [ "$DRY_RUN" = 1 ]; then
  say ""
  say "--dry-run: gates passed, nothing installed."
  exit 0
fi

say ""
say "Installing $APP_REL"
say ""

xcrun devicectl device install app -d "$DEVICE" "$APP"

say ""
ok "installed"
say ""
say "Open AUM now — it was not running during the install, so it will load"
say "this binary rather than a cached one."
say ""
say "For the C22 measurement: load an instrument, then HOLD several notes"
say "while reading the footer. The load figure clears on read, so polling"
say "between notes reports 0% — which is what made C22 uninterpretable."
say ""
say "Then  ./scripts/ios-log.sh"
