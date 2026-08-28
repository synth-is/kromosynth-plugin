#!/usr/bin/env bash
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

for bundle in "$AU" "$VST3"; do
    [[ -d "$bundle" ]] || fail "not built: $bundle
Run: cmake --build --preset sampler-release"

    if ! codesign -dvvv "$bundle" 2>&1 | grep -q '^Authority=Developer ID Application'; then
        actual=$(codesign -dvvv "$bundle" 2>&1 | grep -E '^(Signature|Authority)=' | head -1)
        fail "$bundle is not Developer ID signed.
Found: ${actual:-no signature}
Notarisation will reject it. Reconfigure with -DKROMOSYNTH_SIGN_IDENTITY=..."
    fi

    if ! codesign -dvvv "$bundle" 2>&1 | grep -q 'flags=.*runtime'; then
        fail "$bundle lacks the hardened runtime.
Apple requires it for notarisation. Reconfigure with
-DKROMOSYNTH_HARDENED_RUNTIME=ON"
    fi

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
for bundle in "$AU" "$VST3"; do
    printf '%-34s ' "$(basename "$bundle")"
    if spctl -a -vvv -t install "$bundle" 2>&1 | grep -q 'source=Notarized Developer ID'; then
        echo "accepted, Notarized Developer ID"
    else
        echo "NOT ACCEPTED"
        spctl -a -vvv -t install "$bundle" 2>&1 | sed 's/^/    /'
        exit 1
    fi
done

echo
echo "Done. Reinstall so the hosts pick up the stapled bundles:"
echo "  cmake --build --preset sampler-release"
echo
echo "Then re-test on a machine that has NOT been granted a security exception."
echo "This one has: A2 records GarageBand being permanently excepted here, so a"
echo "clean load on this Mac does not prove a clean load anywhere else."
