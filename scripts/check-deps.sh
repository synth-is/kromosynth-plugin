#!/usr/bin/env bash
# Stage 0 preflight — verify the native toolchain before spending time on a build.
#
# KEEPABLE.
#
# Written because the environment could not be inspected remotely: the only
# evidence on disk was four CMakeCache.txt files from December 2023 (cmake
# 3.27.9 from /opt/homebrew/Cellar, Xcode at /Applications/Xcode.app). That is
# 2.5 years stale and proves nothing about today. This script checks instead of
# assuming.
#
# Exit codes:  0 = everything required is present
#              1 = at least one REQUIRED item is missing
#
# Nothing here installs anything or modifies the system. Read-only.

set -uo pipefail

FAIL=0
WARN=0

# Colours only when attached to a terminal.
if [[ -t 1 ]]; then
  R=$'\e[31m'; G=$'\e[32m'; Y=$'\e[33m'; B=$'\e[1m'; N=$'\e[0m'
else
  R=""; G=""; Y=""; B=""; N=""
fi

ok()   { printf '  %sOK%s    %s\n'   "$G" "$N" "$*"; }
bad()  { printf '  %sMISS%s  %s\n'   "$R" "$N" "$*"; FAIL=$((FAIL+1)); }
warn() { printf '  %sWARN%s  %s\n'   "$Y" "$N" "$*"; WARN=$((WARN+1)); }
info() { printf '        %s\n' "$*"; }
head_() { printf '\n%s%s%s\n' "$B" "$*" "$N"; }

# Compare dotted versions: version_ge "3.27.9" "3.24" -> 0 if >=
version_ge() {
  [[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" == "$2" ]]
}

printf '%s=== kromosynth-plugin Stage 0 preflight ===%s\n' "$B" "$N"

# ── Platform ─────────────────────────────────────────────────────────────────
head_ "Platform"

if [[ "$(uname -s)" != "Darwin" ]]; then
  bad "Not macOS. Stage 0 is macOS-only (AU + VST3); Windows/Linux are Stage 3."
else
  ok "macOS $(sw_vers -productVersion) ($(uname -m))"
fi

# ── Xcode / toolchain ────────────────────────────────────────────────────────
head_ "Compiler toolchain  [REQUIRED]"

if ! xcode-select -p >/dev/null 2>&1; then
  bad "No developer directory. Run: xcode-select --install"
else
  DEVDIR="$(xcode-select -p)"
  ok "Developer dir: $DEVDIR"
  case "$DEVDIR" in
    *Xcode.app*) info "Full Xcode. Needed only for the 'stage0-xcode' preset." ;;
    *)           info "Command Line Tools only — sufficient for AU + VST3." ;;
  esac
fi

if ! command -v clang >/dev/null 2>&1; then
  bad "clang not on PATH"
else
  ok "$(clang --version | head -n1)"
fi

# JUCE 8 + this project require C++20. Apple clang 14 (Xcode 14) is the
# realistic floor; older will fail deep inside JUCE with unhelpful errors.
if command -v clang >/dev/null 2>&1; then
  CLANG_VER="$(clang --version | head -n1 | sed -n 's/.*version \([0-9][0-9]*\.[0-9]*\).*/\1/p')"
  if [[ -n "$CLANG_VER" ]] && ! version_ge "$CLANG_VER" "14.0"; then
    warn "Apple clang $CLANG_VER — C++20 support may be incomplete. 14.0+ recommended."
  fi
fi

if ! xcrun --show-sdk-path >/dev/null 2>&1; then
  bad "No macOS SDK found (xcrun --show-sdk-path failed)"
else
  ok "macOS SDK $(xcrun --show-sdk-version) at $(xcrun --show-sdk-path)"
fi

# AudioUnit headers live in the SDK. If these are absent the AU target will
# fail late, after JUCE has already compiled — which is an expensive way to
# find out.
SDKP="$(xcrun --show-sdk-path 2>/dev/null || true)"
if [[ -n "$SDKP" && -d "$SDKP/System/Library/Frameworks/AudioUnit.framework" ]]; then
  ok "AudioUnit.framework present in SDK"
elif [[ -n "$SDKP" && -d "$SDKP/System/Library/Frameworks/AudioToolbox.framework" ]]; then
  ok "AudioToolbox.framework present (AudioUnit headers live here on recent SDKs)"
else
  bad "Neither AudioUnit nor AudioToolbox framework found in the SDK — AU target will fail"
fi

# ── Build tools ──────────────────────────────────────────────────────────────
head_ "Build tools  [REQUIRED]"

if ! command -v cmake >/dev/null 2>&1; then
  bad "cmake not found. Install: brew install cmake"
else
  CMAKE_VER="$(cmake --version | head -n1 | awk '{print $3}')"
  if version_ge "$CMAKE_VER" "3.24"; then
    ok "cmake $CMAKE_VER (need >= 3.24)  [$(command -v cmake)]"
  else
    bad "cmake $CMAKE_VER is too old — need >= 3.24. Upgrade: brew upgrade cmake"
  fi
fi

if ! command -v make >/dev/null 2>&1; then
  bad "make not found (ships with the Command Line Tools)"
else
  ok "$(make --version | head -n1)"
fi

if ! command -v git >/dev/null 2>&1; then
  bad "git not found — required to fetch the JUCE submodule"
else
  ok "$(git --version)"
fi

# ── Optional ─────────────────────────────────────────────────────────────────
head_ "Optional"

if command -v ninja >/dev/null 2>&1; then
  ok "ninja $(ninja --version) — the 'stage0-ninja' preset is available"
else
  info "ninja absent. Fine: the default 'stage0' preset uses Unix Makefiles."
  info "For faster incremental builds: brew install ninja"
fi

if [[ -x /usr/bin/auval ]]; then
  ok "auval present — AU validation available"
else
  warn "auval not at /usr/bin/auval (unexpected on macOS)"
fi

if command -v pluginval >/dev/null 2>&1; then
  ok "pluginval $(pluginval --version 2>/dev/null | head -n1)"
elif [[ -d /Applications/pluginval.app ]]; then
  ok "pluginval.app in /Applications"
else
  info "pluginval absent. Not required; useful later."
  info "  https://github.com/Tracktion/pluginval/releases"
fi

# ── Disk space ───────────────────────────────────────────────────────────────
head_ "Disk"

AVAIL_KB="$(df -k "$HOME" | awk 'NR==2 {print $4}')"
AVAIL_GB=$((AVAIL_KB / 1024 / 1024))
# JUCE clone ~1.5 GB; a debug build tree with JUCE objects ~2-4 GB.
if (( AVAIL_GB >= 10 )); then
  ok "${AVAIL_GB} GB free (JUCE clone + build tree needs roughly 6 GB)"
elif (( AVAIL_GB >= 6 )); then
  warn "${AVAIL_GB} GB free — tight. JUCE clone + build tree needs roughly 6 GB."
else
  bad "${AVAIL_GB} GB free — not enough for a JUCE clone plus build tree"
fi

# ── Plugin install destinations ──────────────────────────────────────────────
head_ "Plugin install destinations  (COPY_PLUGIN_AFTER_BUILD writes here)"

for d in "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"; do
  if [[ -d "$d" && -w "$d" ]]; then
    ok "writable: $d"
  elif [[ -d "$d" ]]; then
    bad "exists but NOT writable: $d"
  else
    info "does not exist yet (CMake will create it): $d"
  fi
done

# Stale copies from a previous session cause confusing host behaviour.
for d in "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"; do
  for stale in "$d"/Synth.is.* "$d"/KromosynthPlugin.*; do
    [[ -e "$stale" ]] && warn "pre-existing build present: $stale"
  done
done

# ── Hosts for the Stage 0 matrix ─────────────────────────────────────────────
head_ "Hosts for the Stage 0 test matrix"

# Report EVERY match, not just the first. An earlier version returned on the
# first hit, so a machine with both Live 11 and Live 12 installed reported
# only "Ableton Live 11 Suite" (alphabetically first) and the Live 12 install
# was invisible. For a matrix whose whole point is per-host variance, silently
# hiding a host version is exactly the wrong failure.
check_host() {
  local label="$1"; shift
  local found=0
  for p in "$@"; do
    if [[ -e "$p" ]]; then ok "$label — $p"; found=1; fi
  done
  (( found )) || warn "$label not found — that row of the matrix cannot be filled"
}

check_host "Logic Pro"  "/Applications/Logic Pro.app" "/Applications/Logic Pro X.app"
check_host "Ableton Live" /Applications/Ableton\ Live*.app
check_host "Reaper"     "/Applications/REAPER.app" "/Applications/Reaper.app"
check_host "Bitwig"     "/Applications/Bitwig Studio.app"

# ── Network reachability ─────────────────────────────────────────────────────
head_ "Web app origin"

URL="${KROMOSYNTH_WEB_APP_URL:-https://synth.is}"
if command -v curl >/dev/null 2>&1; then
  CODE="$(curl -s -o /dev/null -w '%{http_code}' -m 10 "$URL" || echo "000")"
  if [[ "$CODE" == "200" ]]; then
    ok "$URL responds 200"
  elif [[ "$CODE" == "000" ]]; then
    bad "$URL unreachable — the webview will have nothing to load"
  else
    warn "$URL responded HTTP $CODE"
  fi

  # NOTE: `tr -d '\r'` is load-bearing. HTTP headers are CRLF-terminated and
  # xargs does not strip the CR, so COOP_VAL came out as "same-origin\r" and
  # the equality test below failed against an invisible byte — reporting
  # INCOMPLETE on an origin that was correctly configured. The ok lines above
  # still looked right because a trailing CR is invisible in terminal output.
  HDRS="$(curl -s -I -m 10 "$URL" 2>/dev/null | tr -d '\r' | tr 'A-Z' 'a-z')"

  COEP_VAL="$(grep 'cross-origin-embedder-policy' <<<"$HDRS" | head -n1 | cut -d: -f2- | cut -d';' -f1 | xargs || true)"
  COOP_VAL="$(grep 'cross-origin-opener-policy'   <<<"$HDRS" | head -n1 | cut -d: -f2- | cut -d';' -f1 | xargs || true)"

  if [[ -n "$COEP_VAL" ]]; then
    ok "COEP: $COEP_VAL"
  else
    warn "no Cross-Origin-Embedder-Policy header"
  fi

  if [[ -n "$COOP_VAL" ]]; then
    ok "COOP: $COOP_VAL"
  else
    warn "no Cross-Origin-Opener-Policy header"
  fi

  # EXACT match after stripping any `; report-to=...` parameters. Not a prefix
  # glob: `same-origin-allow-popups` starts with `same-origin` but does NOT
  # grant cross-origin isolation, and a prefix match reported it as valid.
  COEP_OK=0; COOP_OK=0
  [[ "$COEP_VAL" == "require-corp" || "$COEP_VAL" == "credentialless" ]] && COEP_OK=1
  [[ "$COOP_VAL" == "same-origin" ]] && COOP_OK=1

  if (( COEP_OK && COOP_OK )); then
    ok "both isolation headers valid — crossOriginIsolated SHOULD be true."
    info "So if the probe reports it false, that is a genuine WKWebView"
    info "finding, not a deployment gap. This is what makes it interpretable."
  else
    warn "isolation headers incomplete or invalid at the deployed origin."
    info "crossOriginIsolated needs COOP: same-origin AND COEP:"
    info "require-corp|credentialless. With either missing it is false in"
    info "every browser, so the probe says nothing about WKWebView."
  fi
else
  warn "curl not found — skipping reachability check"
fi

# ── Repository state ─────────────────────────────────────────────────────────
head_ "Repository"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
info "root: $REPO_ROOT"

if [[ -f "$REPO_ROOT/third_party/JUCE/CMakeLists.txt" ]]; then
  ok "third_party/JUCE present"
  JH="$REPO_ROOT/third_party/JUCE/modules/juce_core/juce_core.h"
  if [[ -f "$JH" ]]; then
    JV="$(sed -n 's/.*version:[[:space:]]*\([0-9.]*\).*/\1/p' "$JH" | head -n1)"
    JL="$(sed -n 's/.*license:[[:space:]]*\(.*\)/\1/p' "$JH" | head -n1)"
    info "JUCE version: ${JV:-unknown}   module declares license: ${JL:-unknown}"
    if grep -q 'AGPL' "$JH"; then
      ok "juce_core.h mentions AGPL"
    else
      warn "juce_core.h does NOT mention AGPL — the configure step will hard-fail."
      info "That is a FINDING for WP_SYNTHIS_PLUGIN.md §0, not a check to delete."
    fi
  fi
else
  info "third_party/JUCE empty — expected before the first submodule fetch."
  info "Next: git submodule update --init --recursive"
fi

if [[ -d "$REPO_ROOT/.git" ]]; then
  ok "git repository initialised"
else
  info "not a git repository yet — run: git init"
fi

# ── Verdict ──────────────────────────────────────────────────────────────────
head_ "Verdict"
if (( FAIL > 0 )); then
  printf '  %s%d required item(s) missing.%s %d warning(s).\n' "$R" "$FAIL" "$N" "$WARN"
  printf '  Fix the MISS lines above before building.\n\n'
  exit 1
fi
printf '  %sAll required tooling present.%s %d warning(s).\n' "$G" "$N" "$WARN"
printf '  Next: docs/BUILD_STEPS.md\n\n'
exit 0
