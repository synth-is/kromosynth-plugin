#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# ios-icon-experiment.sh — C24's one throwaway run, made repeatable.
#
# THE QUESTION
#
# The app icon works: it is on the home screen. The AU icon does not: AUM draws
# its no-icon placeholder. So the icon PIPELINE is fine and the AU's icon
# RESOLUTION is not.
#
# The surviving hypothesis (C24, flagged in 58629e9 and wrongly retired in C7)
# is `AudioComponentBundle` in the appex's NSExtensionAttributes. JUCE writes it
# unguarded on Darwin — juce_PlistOptions.cpp:377, JUCEUtils.cmake:1459-1471 —
# naming a bundle it only ever creates for macOS.
#
# CONFIRMED AGAINST THE BUILT BUNDLE, 2026-09-10, which is stronger than C24
# had it. The key is present and reads `is.synth.plugin.internal`, and a search
# of the entire shipped app finds no bundle by that name and in fact no .bundle
# at all. So the appex tells a host to look for its icon in a bundle that does
# not exist. C7 retired this because the AU instantiated; instantiation and icon
# lookup are different code paths, and the C15 crash was inside requestIcon.
#
# Still a hypothesis. Mechanically consistent is not the same as demonstrated,
# and this script is what demonstrates or kills it.
#
# ── Why a script for a throwaway ────────────────────────────────────────────
#
# Because it has now failed to happen twice. It is not a keyword on
# juce_add_plugin, so it needs a post-build plist edit plus a re-sign, which is
# fiddly enough at the end of a session to keep being deferred. Two runs of
# this and the answer exists.
#
# ── What it does not do ─────────────────────────────────────────────────────
#
# It does not touch the build output. The modified app is a COPY in
# build/icon-experiment, so the A/B stays intact and reverting is reinstalling
# the original — no rebuild, no undo step to forget.
#
# It does not install. It prints the command, which routes through
# ios-install.sh so the C15 gate still applies. An experiment is exactly where
# the temptation to skip that is highest.
#
# It cannot tell you the answer. THE VERDICT NEEDS YOUR EYES on AUM's plugin
# strip. That is the one step nothing here can automate.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${KROMOSYNTH_IOS_BUILD_DIR:-$REPO_ROOT/build/ios-auv3}"
CONFIG="${KROMOSYNTH_IOS_CONFIG:-Release}"
IDENTITY="${KROMOSYNTH_IOS_IDENTITY:-Apple Development: Bjorn Thor Jonsson (A63TE5AF96)}"

SRC_APP="$BUILD_DIR/src/KromosynthPlugin_artefacts/$CONFIG/Standalone/Synth.is.app"
WORK="$REPO_ROOT/build/icon-experiment"
OUT_APP="$WORK/Synth.is.app"
OUT_APPEX="$OUT_APP/PlugIns/Synth.is.appex"

KEY=":NSExtension:NSExtensionAttributes:AudioComponentBundle"

say()  { printf '%s\n' "$*"; }
fail() { printf '\nFAIL  %s\n' "$*" >&2; exit 1; }

[ -d "$SRC_APP" ] || fail "nothing built — run ./scripts/ios-build.sh first."

# ── which experiment ────────────────────────────────────────────────────────
#
# C28 STRIPPED the key and the icon stayed absent. That eliminated "the key
# points somewhere dead, so remove it" and left the more interesting version
# untried: JUCE sets AudioComponentBundle to <bundleID>.internal, a helper
# bundle it creates on macOS ONLY (JUCEUtils.cmake:1459-1471). On iOS nothing
# by that name is ever built, so the appex tells a host to look for its icon in
# a bundle that does not exist -- and the containing app, two directories up,
# is the bundle that actually holds one.
#
#   --strip   remove the key entirely (C28, done, no effect)
#   --point   set it to the CONTAINING APP's bundle id (untried)
#
# Default is --point, because the strip has already been run.
MODE="${1:---point}"

if [ "$MODE" = "--revert" ]; then
  say "Revert is just reinstalling the untouched build:"
  say ""
  say "  ./scripts/ios-install.sh"
  say ""
  say "Nothing to undo — the experiment never wrote into build/ios-auv3."
  exit 0
fi

# ── The key, before ─────────────────────────────────────────────────────────

current="$(/usr/libexec/PlistBuddy -c "Print $KEY" \
             "$SRC_APP/PlugIns/Synth.is.appex/Info.plist" 2>/dev/null || true)"

[ -n "$current" ] || fail "AudioComponentBundle is already absent from the built
      appex. Either JUCE changed, or someone already patched this. Either way
      the experiment's premise is gone — check what changed before proceeding,
      because an unchanged icon would then mean something different."

say "AudioComponentBundle = $current"

# The mechanism, restated as a check rather than as a belief. If a bundle by
# that name DOES turn up, the hypothesis is wrong and this run is pointless.
if find "$SRC_APP" -name "*.bundle" 2>/dev/null | grep -q .; then
  say ""
  say "WARN  the app contains a .bundle after all:"
  find "$SRC_APP" -name "*.bundle" | sed 's/^/        /'
  say "      The hypothesis assumed none exists. Reconsider before running."
fi

# ── Copy, strip, re-sign ────────────────────────────────────────────────────

rm -rf "$WORK"
mkdir -p "$WORK"
cp -R "$SRC_APP" "$OUT_APP"

if [ "$MODE" = "--strip" ]; then
  /usr/libexec/PlistBuddy -c "Delete $KEY" "$OUT_APPEX/Info.plist"
  say "stripped the key from the copy (C28's experiment)"
else
  # The containing app's bundle id, read from the app rather than hardcoded so
  # a rename cannot silently point this at the wrong thing.
  APP_ID="$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" \
              "$OUT_APP/Info.plist" 2>/dev/null)"

  [ -n "$APP_ID" ] || fail "could not read the containing app's bundle id."

  /usr/libexec/PlistBuddy -c "Set $KEY $APP_ID" "$OUT_APPEX/Info.plist"
  say "pointed the key at the containing app: $APP_ID"
fi

# Entitlements must survive the re-sign. Dropping them silently is how you get
# a plugin that installs, launches, and quietly cannot see the App Group — an
# empty instrument list, indistinguishable from a user who owns nothing.
codesign -d --entitlements :- "$SRC_APP/PlugIns/Synth.is.appex" \
  > "$WORK/appex.entitlements" 2>/dev/null \
  || fail "could not read the appex's entitlements — refusing to re-sign
      without them. See the comment above: losing the App Group entitlement
      presents as an empty library, not as an error."
codesign -d --entitlements :- "$SRC_APP" > "$WORK/app.entitlements" 2>/dev/null \
  || fail "could not read the app's entitlements."

grep -q 'application-groups' "$WORK/appex.entitlements" \
  || fail "the appex entitlements do not mention application-groups. That is
      the one this build depends on; stopping rather than re-signing without it."

# INSIDE OUT. Signing the app first would be invalidated by then signing the
# appex nested inside it.
codesign -f -s "$IDENTITY" --entitlements "$WORK/appex.entitlements" \
  --timestamp=none "$OUT_APPEX" >/dev/null
codesign -f -s "$IDENTITY" --entitlements "$WORK/app.entitlements" \
  --timestamp=none "$OUT_APP" >/dev/null

codesign --verify --deep --strict "$OUT_APP" \
  || fail "the re-signed app does not verify. Do not install it."

say "re-signed appex then app, entitlements preserved, verifies"

# ── Hand over ───────────────────────────────────────────────────────────────

say ""
say "Install the experiment:"
say ""
say "  KROMOSYNTH_IOS_APP=$OUT_APP \\"
say "    ./scripts/ios-install.sh"
say ""
say "Then force-quit AUM, reopen it, and LOOK at the plugin strip."
say ""
say "  icon appears  -> AudioComponentBundle was it. Fix it properly, either as"
say "                   a post-build step in CMake or by guarding"
say "                   juce_PlistOptions.cpp:377 for non-macOS, and record"
say "                   which in the findings."
say "  icon absent   -> C24's surviving hypothesis is dead too. That is a"
say "                   RESULT, not a failed run, and it should be written down"
say "                   as one — the elimination is what is left."
say ""
say "Either way the verdict is yours to read. Nothing here can see the screen."
