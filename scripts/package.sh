#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
#
# Build a signed, notarised .pkg installer for the AU and VST3.
#
# ── What this is for ─────────────────────────────────────────────────────────
#
# scripts/build.sh install copies the bundles into the DEVELOPER'S own
# ~/Library/Audio/Plug-Ins. That is right for the edit/build/host-test loop and
# is not a thing you can give to anyone. This produces the artefact you put on
# a download page.
#
# ── The two certificates, which are not the same certificate ────────────────
#
#   Developer ID Application   signs the .component and the .vst3
#   Developer ID Installer     signs the .pkg
#
# Both come with the same Apple Developer Program membership and the same team,
# and having one does not give you the other. Missing the Installer one is the
# kind of thing that blocks at the very end of a release, so this script checks
# for BOTH before it builds anything.
#
# To create it: Xcode > Settings > Accounts > (Apple ID) > (Team)
#               > Manage Certificates... > + > Developer ID Installer
#
# ── Where it installs, and why there ────────────────────────────────────────
#
# /Library/Audio/Plug-Ins, not ~/Library. It is what every commercial plugin
# does, it is what hosts scan by default, and it means one install serves every
# user on the machine. The cost is an admin password at install time, which is
# what people expect from a .pkg.
#
# ── Usage ────────────────────────────────────────────────────────────────────
#
#   scripts/package.sh                 # build, sign, package. No notarisation.
#   scripts/package.sh --notarize      # ... then notarise and staple the pkg
#
# Notarisation needs the same keychain profile scripts/notarize.sh documents:
#
#   xcrun notarytool store-credentials "kromosynth" \
#       --apple-id <id> --team-id ATA8MQ4L7K --password <app-specific-password>
#
# NOTARISING THE PKG IS NOT THE SAME as notarising the bundles. A stapled pkg
# carries its own ticket; the bundles inside it are covered by the pkg's
# submission, so scripts/notarize.sh is not also needed for a release built
# this way.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build/package"
STAGING="$BUILD_DIR/pkg"
PROFILE="kromosynth"
NOTARIZE=0

for arg in "$@"; do
  case "$arg" in
    --notarize) NOTARIZE=1 ;;
    --profile=*) PROFILE="${arg#*=}" ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

say()  { printf '%s\n' "$*"; }
ok()   { printf 'ok    %s\n' "$*"; }
fail() { printf 'FAIL  %s\n' "$*" >&2; exit 1; }

# ── Both certificates, before anything is built ──────────────────────────────
#
# Checked up front on purpose. Discovering a missing Installer certificate
# after a fifteen-minute build, at the end of a release, is exactly the failure
# this ordering removes. `|| true` because grep exits non-zero on no match and
# set -e would turn a missing certificate into an unexplained abort.

APP_CERT="$(security find-identity -v -p codesigning 2>/dev/null \
            | grep -o 'Developer ID Application: [^"]*' | head -1 || true)"
INST_CERT="$(security find-identity -v 2>/dev/null \
             | grep -o 'Developer ID Installer: [^"]*' | head -1 || true)"

[ -n "$APP_CERT" ] || fail "no Developer ID Application certificate in the keychain.
      This signs the .component and .vst3. See scripts/notarize.sh."

[ -n "$INST_CERT" ] || fail "no Developer ID Installer certificate in the keychain.

      This is a DIFFERENT certificate from Developer ID Application, which you
      do have. Same membership, same team; one does not imply the other.

      Xcode > Settings > Accounts > (Apple ID) > (Team)
            > Manage Certificates... > + > Developer ID Installer

      A pkg signed with the Application certificate is rejected by Gatekeeper
      and refused by notarisation, so there is no point building one."

ok "Developer ID Application: ${APP_CERT#Developer ID Application: }"
ok "Developer ID Installer:   ${INST_CERT#Developer ID Installer: }"

VERSION="$(grep -m1 -oE 'project\(KromosynthPlugin VERSION [0-9.]+' CMakeLists.txt \
           | grep -oE '[0-9.]+$')"
[ -n "$VERSION" ] || fail "could not read VERSION from CMakeLists.txt"

# 0.0.1 is the CMake placeholder. Refusing is deliberate: a download page
# advertising 0.0.1 is a mistake nobody notices until a customer reports it.
[ "$VERSION" != "0.0.1" ] || fail "VERSION is still 0.0.1, the CMake placeholder.
      Set a real version in CMakeLists.txt before building a release."

ok "version $VERSION"

# ── Build ────────────────────────────────────────────────────────────────────
#
# COPY_AFTER_BUILD=OFF, so a release build never touches the developer's own
# plugin folders. build.sh install deliberately does the opposite; mixing the
# two would mean the machine that cuts the release is also the machine running
# an unreleased build, which is how "works here" happens.

say ""
say "Configuring $BUILD_DIR"

cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DKROMOSYNTH_BUILD_SAMPLER=ON \
      -DKROMOSYNTH_BUILD_FAUST=ON \
      -DKROMOSYNTH_COPY_AFTER_BUILD=OFF \
      -DKROMOSYNTH_SIGN_IDENTITY="$APP_CERT" \
      >/dev/null

say "Building AU and VST3..."
cmake --build "$BUILD_DIR" --target KromosynthPlugin_AU KromosynthPlugin_VST3 \
      -j "$(sysctl -n hw.ncpu)"

ARTEFACTS="$BUILD_DIR/src/KromosynthPlugin_artefacts/Release"
AU="$ARTEFACTS/AU/Synth.is.component"
VST3="$ARTEFACTS/VST3/Synth.is.vst3"

[ -d "$AU" ]   || fail "no AU at $AU"
[ -d "$VST3" ] || fail "no VST3 at $VST3"

# The build signs via KROMOSYNTH_SIGN_IDENTITY, but verify rather than assume:
# an unsigned bundle inside a signed pkg passes pkgbuild and fails at the user.
for b in "$AU" "$VST3"; do
  codesign --verify --strict "$b" 2>/dev/null \
    || fail "$(basename "$b") is not validly signed"
done
ok "both bundles signed and verified"

# ── Component packages ───────────────────────────────────────────────────────
#
# One per format, each rooted at the directory it installs into, so the
# distribution can offer them separately. --install-location is where the
# CONTENTS of the root go.

rm -rf "$STAGING"
mkdir -p "$STAGING/au/Synth.is.component" "$STAGING/vst3/Synth.is.vst3" \
         "$STAGING/licences/Licences/third-party" "$STAGING/out"

ditto "$AU"   "$STAGING/au/Synth.is.component"
ditto "$VST3" "$STAGING/vst3/Synth.is.vst3"

pkgbuild --root "$STAGING/au" \
         --identifier "is.synth.plugin.au" \
         --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/Components" \
         "$STAGING/out/au.pkg" >/dev/null

pkgbuild --root "$STAGING/vst3" \
         --identifier "is.synth.plugin.vst3" \
         --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         "$STAGING/out/vst3.pkg" >/dev/null

# ── Licences ────────────────────────────────────────────────────────────────
#
# AGPL-3.0-or-later (LICENSE), the third-party notices, and the licence texts
# those notices point at, so the BSD/MIT/Apache "reproduce this notice in
# binary form" terms are met by the installer itself. Outside the signed
# bundles on purpose: adding files inside them after signing would break the
# signature.

LIC="$STAGING/licences/Licences"
[ -f LICENSE ] || fail "no LICENSE at the repo root"
[ -f THIRD_PARTY_NOTICES.md ] || fail "no THIRD_PARTY_NOTICES.md at the repo root"
cp -X LICENSE THIRD_PARTY_NOTICES.md "$LIC/"

# AGPL source offer. The public repo URL is not decided yet, so SOURCE_URL is
# empty until it is: set it (env or edit) and the offer names it. Until then
# the offer points at the contact address only.
SOURCE_URL="${SOURCE_URL:-}"
{
  echo "Synth.is plugin $VERSION - source offer"
  echo
  echo "This software is licensed AGPL-3.0-or-later (see LICENSE). The complete"
  echo "corresponding source for this exact version is available to anyone who"
  echo "received these binaries."
  echo
  if [ -n "$SOURCE_URL" ]; then
    echo "Source: $SOURCE_URL (tag v$VERSION)"
  else
    echo "Source: on request, see README.md in the source repository."
  fi
} > "$LIC/SOURCE_OFFER.txt"

WT_LICENSE="$(ls "$BUILD_DIR"/_wasmtime/*/LICENSE 2>/dev/null | head -1 || true)"
SF=third_party/sfizz
DR=$SF/external/st_audiofile/thirdparty
while IFS='|' read -r name path; do
  [ -f "$path" ] || fail "licence text for $name is missing: $path"
  cp -X "$path" "$LIC/third-party/$name"
done <<LIST
JUCE.md|third_party/JUCE/LICENSE.md
JUCE-VST3_SDK.txt|third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt
sfizz.txt|$SF/LICENSE
wasmtime.txt|$WT_LICENSE
wasm3.txt|tools/ios-gate0/wasm3/LICENSE
dr_libs.txt|$DR/dr_libs/LICENSE
abseil-cpp.txt|$SF/external/abseil-cpp/LICENSE
atomic_queue.txt|$SF/external/atomic_queue/LICENSE
filesystem.txt|$SF/external/filesystem/LICENSE
fmidi.md|$SF/external/fmidi/LICENSE.md
jsl.md|$SF/external/jsl/LICENSE.md
simde.txt|$SF/external/simde/COPYING
st_audiofile.md|$SF/external/st_audiofile/LICENSE.md
stb_vorbis.txt|$DR/stb_vorbis/LICENSE
libaiff.txt|$DR/libaiff/LICENSE
wavpack.txt|$DR/wavpack/license.txt
cephes.txt|$SF/external/cephes/LICENSE.txt
cpuid.rst|$SF/src/external/cpuid/LICENSE.rst
hiir.txt|$SF/src/external/hiir/license.txt
kiss_fft.txt|$SF/src/external/kiss_fft/COPYING
pugixml.md|$SF/src/external/pugixml/LICENSE.md
spline.txt|$SF/src/external/spline/LICENSE
tunings.md|$SF/src/external/tunings/LICENSE.md
LIST

pkgbuild --root "$STAGING/licences" \
         --identifier "is.synth.plugin.licences" \
         --version "$VERSION" \
         --install-location "/Library/Application Support/Synth.is" \
         "$STAGING/out/licences.pkg" >/dev/null

ok "component packages built"

# ── Distribution ─────────────────────────────────────────────────────────────
#
# Hand-written rather than `productbuild --synthesize`, because the synthesised
# one offers no choices and we want the user able to install VST3 without AU.
# hostArchitectures covers Apple silicon and Intel; the bundles are universal.

cat > "$STAGING/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Synth.is</title>
    <organization>is.synth</organization>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>

    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
        <line choice="licences"/>
    </choices-outline>

    <choice id="au" title="Audio Unit (AU)"
            description="For Logic Pro, GarageBand, and other AU hosts. Installs to /Library/Audio/Plug-Ins/Components.">
        <pkg-ref id="is.synth.plugin.au"/>
    </choice>

    <choice id="vst3" title="VST3"
            description="For Reaper, Ableton Live, Cubase, and other VST3 hosts. Installs to /Library/Audio/Plug-Ins/VST3.">
        <pkg-ref id="is.synth.plugin.vst3"/>
    </choice>

    <choice id="licences" title="Licences" visible="false" start_selected="true"
            description="AGPL-3.0-or-later and third-party notices. Installs to /Library/Application Support/Synth.is/Licences.">
        <pkg-ref id="is.synth.plugin.licences"/>
    </choice>

    <pkg-ref id="is.synth.plugin.au" version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
    <pkg-ref id="is.synth.plugin.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="is.synth.plugin.licences" version="$VERSION" onConclusion="none">licences.pkg</pkg-ref>
</installer-gui-script>
XML

PKG="$BUILD_DIR/Synth.is-$VERSION.pkg"

productbuild --distribution "$STAGING/distribution.xml" \
             --package-path "$STAGING/out" \
             --sign "$INST_CERT" \
             "$PKG" >/dev/null

ok "signed installer: $PKG"

pkgutil --check-signature "$PKG" >/dev/null \
  || fail "the pkg signature does not verify"
ok "pkg signature verifies"

# ── Notarisation ─────────────────────────────────────────────────────────────

if [ "$NOTARIZE" -eq 0 ]; then
  say ""
  say "Not notarised. The pkg is signed and will install on THIS machine, and"
  say "will be refused by Gatekeeper on anyone else's. Re-run with --notarize"
  say "before putting it on a download page."
  exit 0
fi

say ""
say "Notarising (a network round trip; minutes, not seconds)..."

xcrun notarytool submit "$PKG" --keychain-profile "$PROFILE" --wait \
  || fail "notarisation failed. 'xcrun notarytool log <submission-id> \\
      --keychain-profile $PROFILE' says why."

xcrun stapler staple "$PKG" || fail "stapling failed"

# The check that matters, and the one people skip: spctl assesses the pkg the
# way Gatekeeper will on a machine that has never seen it. A pkg that notarised
# but did not staple still passes here while online and fails on a laptop in a
# studio with no network.
spctl --assess --type install -vv "$PKG" 2>&1 | grep -q "accepted" \
  || fail "spctl rejects the stapled pkg"

ok "notarised, stapled, and accepted by spctl"
say ""
say "Ship  $PKG"
