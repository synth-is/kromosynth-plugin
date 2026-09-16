#!/usr/bin/env bash
#
# Build the plugin, either to check it compiles or to install it for host testing.
#
# KEEPABLE. Sibling of notarize.sh, and deliberately separate from it for the
# same reason: notarisation is a network round trip to Apple that takes minutes,
# and the edit/build/host-test loop has to stay fast.
#
# ── Why this exists ──────────────────────────────────────────────────────────
#
# Two things about building this plugin are easy to get wrong and expensive when
# you do.
#
# A plugin build copies itself over ~/Library/Audio/Plug-Ins by default. If
# KROMOSYNTH_SIGN_IDENTITY is empty it also signs ad-hoc — so "let me check this
# compiles" silently replaces a Developer ID signed, notarised bundle with an
# ad-hoc one, and the host starts demanding security exceptions. That is what
# `verify` mode is for: it compiles and installs nothing.
#
# And the signing identity is a long exact string. Typing it wrong does not
# fail; it falls back to ad-hoc, prints one line among hundreds, and you find
# out later. This reads it from the keychain instead.
#
# ── Usage ────────────────────────────────────────────────────────────────────
#
#   scripts/build.sh                 # verify: compile only, installs nothing
#   scripts/build.sh install         # sign with Developer ID and install
#   scripts/build.sh install --backup   # copy the current bundle aside first
#   scripts/build.sh install --adhoc    # install unsigned (expect gatekeeper)
#
# After `install`, restart the host. AU caches component scans aggressively and
# will happily keep using the previous binary.
#
# Notarisation is NOT done here. A rebuild discards the stapled ticket, so an
# installed build is signed but not notarised until scripts/notarize.sh runs.
# For local host testing that is fine: hosts care about the Developer ID
# signature, not the ticket.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

MODE="${1:-verify}"
shift || true

BACKUP=0
ADHOC=0

for arg in "$@"; do
  case "$arg" in
    --backup) BACKUP=1 ;;
    --adhoc)  ADHOC=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

# The Faust engine is ON by default because a build that omits it routes every
# Faust instrument in the library to the "no engine" path, which renders
# silence and looks exactly like a broken instrument rather than a missing
# feature. KROMOSYNTH_BUILD_FAUST=OFF in the environment turns it off
# deliberately.
FAUST="${KROMOSYNTH_BUILD_FAUST:-ON}"

AU_DEST="$HOME/Library/Audio/Plug-Ins/Components/Synth.is.component"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

case "$MODE" in
  verify)  BUILD_DIR="build/verify-nocopy"; COPY=OFF ;;
  install) BUILD_DIR="build/stage1";        COPY=ON  ;;
  *) echo "usage: scripts/build.sh [verify|install] [--backup] [--adhoc]" >&2; exit 2 ;;
esac

# ── Signing identity ─────────────────────────────────────────────────────────
#
# Read from the keychain rather than hard-coded. A team name changes, a
# certificate is reissued, and a stale literal here would fall back to ad-hoc
# without failing.
IDENTITY=""

if [[ "$MODE" == "install" && "$ADHOC" -eq 0 ]]; then
  IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null \
              | grep -o '"Developer ID Application: [^"]*"' \
              | head -1 | tr -d '"')"

  if [[ -z "$IDENTITY" ]]; then
    echo "No Developer ID Application identity in the keychain." >&2
    echo "Create one (see scripts/notarize.sh) or pass --adhoc to install unsigned." >&2
    exit 1
  fi

  echo "Signing identity: $IDENTITY"
fi

# ── Backup ───────────────────────────────────────────────────────────────────
if [[ "$BACKUP" -eq 1 && -d "$AU_DEST" ]]; then
  STAMP="$(date +%Y%m%d-%H%M%S)"
  BACKUP_DIR="$HOME/Desktop/Synth.is.component.$STAMP"
  echo "Backing up current bundle -> $BACKUP_DIR"
  ditto "$AU_DEST" "$BACKUP_DIR"

  if xcrun stapler validate "$AU_DEST" >/dev/null 2>&1; then
    echo "  (that copy is notarised; this build will not be)"
  fi
fi

# Recorded before the build so the check at the end can tell whether anything
# actually landed, rather than assuming it did.
BEFORE=""
[[ -f "$AU_DEST/Contents/MacOS/Synth.is" ]] && \
  BEFORE="$(shasum "$AU_DEST/Contents/MacOS/Synth.is" | cut -d' ' -f1)"

echo "Configuring $BUILD_DIR (copy after build: $COPY)"

cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DKROMOSYNTH_BUILD_SAMPLER=ON \
      -DKROMOSYNTH_BUILD_FAUST="$FAUST" \
      -DKROMOSYNTH_COPY_AFTER_BUILD="$COPY" \
      -DKROMOSYNTH_SIGN_IDENTITY="$IDENTITY" \
      >/dev/null

echo "Building AU and VST3 with $JOBS jobs..."
cmake --build "$BUILD_DIR" --target KromosynthPlugin_AU KromosynthPlugin_VST3 -j "$JOBS"

# ── Did it do what it said? ──────────────────────────────────────────────────
#
# Both directions are checked. A verify build that quietly installed would be
# the destructive case; an install build that quietly did not would send you
# into a host to test a binary that never changed. Both have happened.
echo
AFTER=""
[[ -f "$AU_DEST/Contents/MacOS/Synth.is" ]] && \
  AFTER="$(shasum "$AU_DEST/Contents/MacOS/Synth.is" | cut -d' ' -f1)"

if [[ "$MODE" == "verify" ]]; then
  if [[ "$BEFORE" != "$AFTER" ]]; then
    echo "WARNING: the installed bundle changed during a verify build." >&2
    echo "That should be impossible with COPY_AFTER_BUILD=OFF." >&2
    exit 1
  fi
  echo "Compiled. Installed bundle untouched."
  exit 0
fi

if [[ -z "$AFTER" ]]; then
  echo "WARNING: nothing is installed at $AU_DEST" >&2
  exit 1
fi

if [[ "$BEFORE" == "$AFTER" ]]; then
  echo "WARNING: the installed bundle did not change." >&2
  echo "The build may have been up to date, or the copy step did not run." >&2
fi

echo "Installed: $(ls -l "$AU_DEST/Contents/MacOS/Synth.is" | awk '{print $6, $7, $8}')"
codesign -dv --verbose=2 "$AU_DEST" 2>&1 | grep -E "^Authority=Developer ID Application|flags=" || true

if ! codesign --verify --deep --strict "$AU_DEST" 2>/dev/null; then
  echo "WARNING: signature does not verify." >&2
fi

echo

# A WARNING NOBODY READS IS NOT A WARNING.
#
# "Restart the host before testing" has been printed here since Stage 0 and was
# ignored three times in one day, each time costing a round of debugging a build
# that was never loaded -- once including a diagnosis of a WKWebView caching
# problem that does not exist.
#
# The reason it is easy to ignore: reopening the plugin WINDOW looks like a
# restart. It is not. GarageBand hosts AU plugins out of process, that process
# loads the binary ONCE, and a new plugin instance inside it runs the old image.
# `processor_constructed` appears in the log every time and looks like proof of
# fresh code.
#
# So this no longer advises. It checks, and says which image is running.
# `|| true` IS LOAD-BEARING, and its absence made this whole block dead code.
#
# With `set -euo pipefail` at the top, a pipeline whose grep matches nothing
# exits non-zero, and a failing command substitution in an assignment aborts
# the script. So when NO AU host was running -- the normal case, and the case
# this advisory is most useful in -- `scripts/build.sh install` printed the
# codesign block and then exited 1, reporting failure for an install that had
# just succeeded, without ever reaching the check below.
#
# Which is the same shape as the thing this block was written to catch: a check
# that looks present and does not run. Found 2026-09-16.
HOST_LINE=$(ps -eo lstart=,comm= | grep -i AUHostingServiceXPC | grep -v grep | head -1 || true)

if [ -n "$HOST_LINE" ]; then
  HOST_EPOCH=$(date -j -f "%a %b %e %T %Y" "$(echo "$HOST_LINE" | sed 's/ *\/System.*//')" +%s 2>/dev/null)
  BIN_EPOCH=$(stat -f %m "$AU_DEST/Contents/MacOS/$(basename "${AU_DEST%.component}")" 2>/dev/null)

  if [ -n "$HOST_EPOCH" ] && [ -n "$BIN_EPOCH" ] && [ "$HOST_EPOCH" -lt "$BIN_EPOCH" ]; then
    echo "  *** THE RUNNING AU HOST PREDATES THIS BUILD ***"
    echo "      host started: $(date -r $HOST_EPOCH '+%H:%M:%S')   this build: $(date -r $BIN_EPOCH '+%H:%M:%S')"
    echo
    echo "      Quit the DAW entirely. Closing the plugin window is NOT enough:"
    echo "      the host process outlives it and keeps the old binary loaded."
  else
    echo "  No AU host is holding an older image."
  fi
else
  echo "  No AU host running — the next launch will load this build."
fi
