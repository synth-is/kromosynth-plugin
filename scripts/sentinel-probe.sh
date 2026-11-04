#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
#
# Runs the sentinel rate probe on the Mac Standalone and prints what it found.
#
# KEEPABLE. synthis-7o3.1 (docs/RENDER_PROGRESS_DESIGN.md, section 5).
#
# ── What it answers ──────────────────────────────────────────────────────────
#
# The site will set `window.location.href` to a `synthis://progress?...` URL
# every few seconds. Does that disturb the page it is set from: focus, scroll,
# frame cadence, a reload? And what does a hidden site view do to the page's
# own timers, which decides how long the plugin can wait for a heartbeat before
# calling a job lost?
#
# The probe itself is in src/PluginEditor.cpp (KROMOSYNTH_SENTINEL_PROBE, inert
# unless that variable is set, Standalone only).
#
# ── Run it WITH THE SCREEN UNLOCKED AND THE WINDOW IN FRONT ──────────────────
#
# A locked screen makes the site's document hidden: no frames, no focus, nothing
# to scroll, and timers throttled to about one a minute. Every counter the probe
# keeps for those things then reads zero, which proves nothing. The delivery
# counts are sound either way; the rest need a visible window.
#
# ── Usage ────────────────────────────────────────────────────────────────────
#
#   scripts/sentinel-probe.sh            # shown, hidden, flash: about 4 minutes
#   scripts/sentinel-probe.sh shown      # site in front, 1 sentinel per second
#   scripts/sentinel-probe.sh hidden     # site put behind the shell after 14 s
#   scripts/sentinel-probe.sh flash      # 10 per second for 20 s: WATCH THE WINDOW
#
# Needs the Standalone built first (scripts/build.sh verify builds it into
# build/verify-nocopy). Opens a window; leave the mouse alone during `shown` and
# `hidden`, and scroll and type in the site during `flash`.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$REPO_ROOT/build/verify-nocopy/src/KromosynthPlugin_artefacts/Release/Standalone/Synth.is.app/Contents/MacOS/Synth.is"
LOG="$HOME/Library/Logs/Synth.is/stage0-Synth-Standalone.log"

if [[ ! -x "$APP" ]]; then
  echo "No Standalone at $APP" >&2
  echo "Build it first: scripts/build.sh verify" >&2
  exit 1
fi

if ioreg -n Root -d1 -a 2>/dev/null | grep -A1 CGSSessionScreenIsLocked | grep -q "<true/>"; then
  echo "The screen is locked, so the site's document would be hidden and most counters would read zero." >&2
  echo "Unlock it and run this again." >&2
  exit 1
fi

# run_one <label> <spec> <seconds> [hide]
run_one() {
  local label="$1" spec="$2" secs="$3" hide="${4:-0}"
  local mark pid
  mark="$(date +%Y-%m-%dT%H:%M:%S)"

  echo
  echo "== $label  (KROMOSYNTH_SENTINEL_PROBE=$spec, hide=$hide)"

  KROMOSYNTH_SENTINEL_PROBE="$spec" KROMOSYNTH_SENTINEL_PROBE_HIDE="$hide" "$APP" >/dev/null 2>&1 &
  pid=$!

  # Load, probe delay, the window itself, the read-back, and some slack.
  sleep $(( secs + 22 ))

  kill "$pid" 2>/dev/null || true
  sleep 1
  kill -9 "$pid" 2>/dev/null || true

  awk -v m="$mark" '$1 >= m' "$LOG" | grep "pid=$pid" > "${TMPDIR:-/tmp}/sentinel-probe-$label.log" || true

  python3 -I - "${TMPDIR:-/tmp}/sentinel-probe-$label.log" <<'PY'
import json, re, sys
lines = open(sys.argv[1]).read().splitlines()
received = sum(1 for l in lines if "acquisition_sentinel_malformed" in l)
loads = sum(1 for l in lines if "page_finished_loading" in l)
res = next((l for l in lines if "SENTINEL_PROBE {" in l), None)
if not res:
    print("  no result line (the site may not have loaded, or the app was closed early)")
    sys.exit(0)
d = json.loads(res.split("SENTINEL_PROBE ", 1)[1])
sent = d.get("sentFinal", d.get("sent"))
print(f"  sentinels sent {sent}, reached pageAboutToLoad {received}, page loads {loads} (2 = shell + site, no reloads)")
print(f"  document hidden at start: {d['hidden0']}   page could scroll: {d['scrollable']} px   field held focus: {d['field']}")
print(f"  focus events {d['focusEv']}, blur {d['blurEv']}, scroll {d['scrollEv']}, visibility {d['visEv']}, pagehide {d['pagehide']}, beforeunload {d['beforeunload']}")
print(f"  scrollY {d['scrollY0']} -> {d['scrollY1']}   active element {d['active0']} -> {d['active1']}   href unchanged: {d['href0'] == d['href1']}")
print(f"  frames {d['rafFrames']}, over 50 ms {d['rafOver50']}, over 100 ms {d['rafOver100']}, longest {d['rafMaxMs']} ms")
print(f"  1 s timer: ticks per 10 s {d.get('tickBuckets')}, longest gap {d.get('maxTickGapMs')} ms, ticks while hidden {d.get('hiddenTicks')}")
PY
}

what="${1:-all}"

case "$what" in
  shown|all)  run_one shown  "1:45:1:page" 45 0 ;;
esac
case "$what" in
  hidden|all) run_one hidden "1:45:1:page" 45 1 ;;
esac
case "$what" in
  flash|all)
    echo
    echo ">>> flash: for the next 40 s the site gets 10 sentinels a second."
    echo ">>> Watch the window for any flash or jump; scroll the page and click a search field."
    run_one flash "10:20:1:page" 20 0
    ;;
esac

echo
echo "Paste this output back, and say whether the window flashed, jumped or lost focus during flash."
