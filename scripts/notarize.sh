#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
#
# Notarise and staple the signed plugin bundles.
#
# KEEPABLE. Separate from the build on purpose: notarisation is a network round
# trip to Apple that takes minutes, and wiring it into every build would make
# the edit/build/host-test loop unusable.
#
# ── Prerequisites ────────────────────────────────────────────────────────────
#
# 1. A Developer ID Application certificate in the login keychain. Note this is
#    NOT the same certificate as the "Apple Distribution" one used to ship an
#    iOS app to the App Store, even though both come with the same Apple
#    Developer Program membership.
#
#    Xcode > Settings > Apple Accounts > (pick the Apple ID) > (pick the Team)
#        > Manage Certificates... > + > Developer ID Application
#
#    The pane is "Apple Accounts", not "Accounts" -- renamed in Xcode 26.
#
#    Apple's documentation says Developer ID certificates are restricted to the
#    team's Account Holder. OBSERVED OTHERWISE: this certificate was created by
#    an account showing the "Admin" role in Xcode's team list. Either Apple has
#    relaxed it or the Account Holder had delegated the right. Recorded because
#    the documented restriction was stated here as fact and the screen said
#    otherwise.
#
#    If the + menu does not offer it, use the web path instead:
#
#      a. Keychain Access > Certificate Assistant > Request a Certificate From
#         a Certificate Authority. Save to disk. This creates the private key
#         in the login keychain and a .certSigningRequest file.
#      b. developer.apple.com/account > Certificates, IDs & Profiles >
#         Certificates > + > Developer ID Application. Upload the CSR.
#      c. Download the .cer and double-click to install.
#
#    Either way, confirm it landed:
#
#        security find-identity -v -p codesigning
#
# 2. The build configured with that identity:
#
#        cmake --preset sampler-release \
#              -DKROMOSYNTH_SIGN_IDENTITY="Developer ID Application: NAME (TEAMID)"
#
# 3. A stored notarytool credential profile. The password is an app-specific
#    password generated at the WEBSITE appleid.apple.com -- not entered in
#    Xcode, and not the Apple ID password itself:
#
#        appleid.apple.com > Sign-In and Security > App-Specific Passwords > +
#
#    It is displayed once. The Apple ID is the address shown under your name in
#    Xcode > Settings > Apple Accounts.
#
#        xcrun notarytool store-credentials "kromosynth" \
#            --apple-id "you@example.com" \
#            --team-id "TEAMID" \
#            --password "abcd-efgh-ijkl-mnop"
#
#    This script never handles the password itself; notarytool reads it from
#    the keychain by profile name.
#
# ── Why a zip, and why stapling matters ──────────────────────────────────────
#
# notarytool will not accept a bare .component or .vst3 -- plugins must arrive
# in a zip, dmg or pkg. `ditto -c -k --keepParent` is the required form; the
# `zip` command mangles bundle symlinks and resource forks.
#
# Stapling is the step people skip. Notarisation registers the bundle with
# Apple, and without stapling every load requires a successful network check
# against Apple's servers. A stapled bundle carries its own ticket and works
# offline. For a plugin loaded during a DAW's startup scan, an unstapled bundle
# on a flaky network is a plugin that intermittently fails to appear.
#
# ── Usage ────────────────────────────────────────────────────────────────────
#
#     scripts/notarize.sh [build-dir] [keychain-profile]
#
# Defaults: build/sampler-release, profile "kromosynth".

set -euo pipefail

BUILD_DIR="${1:-build/sampler-release}"
PROFILE="${2:-kromosynth}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

ARTEFACTS="$BUILD_DIR/src/KromosynthPlugin_artefacts/Release"
AU="$ARTEFACTS/AU/Synth.is.component"
VST3="$ARTEFACTS/VST3/Synth.is.vst3"

STAGING="$BUILD_DIR/notarize"
ZIP="$STAGING/Synth.is-plugins.zip"

fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ── Pre-flight ───────────────────────────────────────────────────────────────
#
# Every one of these checks exists because the corresponding failure is
# expensive: notarisation is a multi-minute round trip, and an ad-hoc signature
# or a missing timestamp is rejected only after the upload completes.
#
# NO PIPELINES HERE, deliberately. The first version used
#
#     if ! codesign -dvvv "$bundle" 2>&1 | grep -q '^Authority=...'; then
#
# which rejected a CORRECTLY signed bundle while printing the very Authority
# line it claimed was missing. `grep -q` exits at its first match and closes the
# pipe; codesign then dies of SIGPIPE and returns 141; `set -o pipefail` above
# promotes that to a pipeline failure. Verified: the identical command returns
# 0 without pipefail and 141 with it.
#
# So codesign runs ONCE per bundle into a variable, matched with `case`.
# Cheaper, and it cannot fail this way.

for bundle in "$AU" "$VST3"; do
    [[ -d "$bundle" ]] || fail "not built: $bundle
Run: cmake --build --preset sampler-release"

    info=$(codesign -dvvv "$bundle" 2>&1) || fail "codesign could not read $bundle"

    case "$info" in
        *"Authority=Developer ID Application"*) ;;
        *) fail "$bundle is not Developer ID signed.
Notarisation will reject it. Reconfigure with -DKROMOSYNTH_SIGN_IDENTITY=..." ;;
    esac

    case "$info" in
        *runtime*) ;;
        *) fail "$bundle lacks the hardened runtime.
Apple requires it for notarisation. Reconfigure with
-DKROMOSYNTH_HARDENED_RUNTIME=ON" ;;
    esac

    case "$info" in
        *"Timestamp="*) ;;
        # Without a secure timestamp the signature stops verifying the day the
        # certificate expires, which for this team is 1 Feb 2027 rather than
        # five years out. See L5.
        *) fail "$bundle has no secure timestamp. Sign with --timestamp." ;;
    esac

    codesign --verify --deep --strict --verbose=2 "$bundle" \
        || fail "$bundle fails signature verification"
done

xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null 2>&1 \
    || fail "no notarytool credential profile named '$PROFILE'.
Create one with:
  xcrun notarytool store-credentials \"$PROFILE\" \\
      --apple-id \"you@example.com\" --team-id \"TEAMID\" \\
      --password \"app-specific-password\""

# ── Submit ───────────────────────────────────────────────────────────────────

rm -rf "$STAGING"
mkdir -p "$STAGING/Synth.is"
cp -R "$AU" "$STAGING/Synth.is/"
cp -R "$VST3" "$STAGING/Synth.is/"

# keepParent so the bundles arrive as bundles rather than as loose contents.
ditto -c -k --keepParent "$STAGING/Synth.is" "$ZIP"

echo "Submitting $(du -h "$ZIP" | cut -f1) to Apple. This usually takes 1-5 minutes."

if ! xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait; then
    echo
    echo "Rejected. The summary above is not the detail -- fetch the full log:"
    echo "  xcrun notarytool log <submission-id> --keychain-profile $PROFILE"
    exit 1
fi

# ── Staple ───────────────────────────────────────────────────────────────────
#
# Stapled onto the ORIGINAL bundles, not the copies in the staging directory.
# The ticket is per-bundle, so both need it.

for bundle in "$AU" "$VST3"; do
    xcrun stapler staple "$bundle" || fail "stapling failed for $bundle"
done

# ── Verify, because "notarised" and "Gatekeeper accepts it" are not the same ──
#
# A2's whole point is that the host's assessment is what matters. spctl is that
# assessment. Check it rather than trusting that submission success implies it.

echo
echo "Installing the stapled bundles."

# The script does this itself rather than telling you to rebuild, because
# "rebuild to reinstall" is wrong in both directions:
#
#   - If nothing changed, CMake does not re-link, so the POST_BUILD copy never
#     runs and the INSTALLED bundles stay at their pre-staple state. Observed:
#     spctl said "Notarized Developer ID" (it checked with Apple over the
#     network) while `stapler validate` said no ticket was attached. The online
#     check masked the missing staple exactly as long as the network was up.
#   - If something did change, the rebuild re-signs, which invalidates the
#     ticket and requires notarising again.
#
# ditto rather than cp, to preserve the bundle structure and extended
# attributes the signature is computed over.

install_bundle() {
    local src="$1" dir="$2"
    mkdir -p "$dir"
    rm -rf "${dir:?}/$(basename "$src")"
    ditto "$src" "$dir/$(basename "$src")"
}

install_bundle "$AU"   "$HOME/Library/Audio/Plug-Ins/Components"
install_bundle "$VST3" "$HOME/Library/Audio/Plug-Ins/VST3"

# ── Verify, because "notarised" and "Gatekeeper accepts it" are not the same ──
#
# A2's whole point is that the host's assessment is what matters. spctl is that
# assessment. Check it rather than trusting that submission success implies it.
#
# And check the STAPLE separately. spctl alone will happily report a bundle as
# notarised by asking Apple over the network, which says nothing about whether
# the ticket travels with the bundle. A DAW scanning plugins at startup on a
# flaky connection is precisely where that difference shows up.

echo
for bundle in "$HOME/Library/Audio/Plug-Ins/Components/$(basename "$AU")" \
              "$HOME/Library/Audio/Plug-Ins/VST3/$(basename "$VST3")"; do
    printf '%-34s ' "$(basename "$bundle")"

    # Same no-pipeline rule as the pre-flight: `| grep -q` under pipefail
    # reports failure via SIGPIPE even on a match.
    assessment=$(spctl -a -vvv -t install "$bundle" 2>&1 || true)
    stapled=$(xcrun stapler validate "$bundle" 2>&1 || true)

    case "$assessment" in
        *"source=Notarized Developer ID"*) ;;
        *)
            echo "NOT ACCEPTED"
            printf '%s\n' "$assessment" | sed 's/^/    /'
            exit 1
            ;;
    esac

    case "$stapled" in
        *"validate action worked"*)
            echo "accepted, Notarized Developer ID, ticket stapled"
            ;;
        *)
            echo "notarised but NOT STAPLED"
            printf '%s\n' "$stapled" | sed 's/^/    /'
            exit 1
            ;;
    esac
done

echo
echo "Installed and stapled. Do NOT rebuild before testing: a rebuild re-signs,"
echo "which invalidates the ticket and requires notarising again."
echo
echo "Now load Synth.is in GarageBand. Per L6 the security exception on this"
echo "machine is scoped to three superseded ad-hoc builds, so this build is not"
echo "pre-approved and the test is valid here: loading with no dialog means"
echo "library validation passed."
