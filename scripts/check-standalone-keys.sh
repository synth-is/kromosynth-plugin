#!/usr/bin/env bash
# check-standalone-keys.sh — grep-level acceptance checks for synthis-o2a.5
# (standalone computer-keyboard play + Z/X octave shift).
#
# The gate (scripts/gate.sh) cannot see the editor, so these checks encode the
# task's acceptance criteria directly against src/. Run by hand:
#
#   bash scripts/check-standalone-keys.sh
#
# Exit 0 = every criterion is present in the sources; exit 1 = at least one is
# missing. This script is NOT part of gate.sh and changes nothing the gate
# relies on.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }

# ── 1. the keyEvent native function exists and is registered only under the
#       Standalone wrapper guard (before the browser is constructed from the
#       options, after a `wrapperType_Standalone` guard line).
awk '
  /wrapperType == juce::AudioProcessor::wrapperType_Standalone/ { guard = NR }
  /withNativeFunction \("keyEvent"/ && key == 0 { key = NR; g = guard }
  /browser_ = std::make_unique<InstrumentedBrowser>/ && br == 0 { br = NR }
  END { exit !(key > 0 && g > 0 && key > g && (br == 0 || key < br)) }
' src/PluginEditor.cpp \
  && ok "keyEvent native function registered under the Standalone guard" \
  || bad "keyEvent native function registered under the Standalone guard"

# ── 2. the on-screen keyboard itself is still constructed only under Standalone.
awk '
  /wrapperType == juce::AudioProcessor::wrapperType_Standalone/ { guard = NR }
  /keyboard_ = std::make_unique<juce::MidiKeyboardComponent>/ && kb == 0 { kb = NR; g = guard }
  END { exit !(kb > 0 && g > 0 && kb > g) }
' src/PluginEditor.cpp \
  && ok "on-screen keyboard constructed only under the Standalone guard" \
  || bad "on-screen keyboard constructed only under the Standalone guard"

# ── 3. the octave label is constructed under the same guard.
awk '
  /wrapperType == juce::AudioProcessor::wrapperType_Standalone/ { guard = NR }
  /octaveLabel_ = std::make_unique/ && lb == 0 { lb = NR; g = guard }
  END { exit !(lb > 0 && g > 0 && lb > g) }
' src/PluginEditor.cpp \
  && ok "octave label constructed only under the Standalone guard" \
  || bad "octave label constructed only under the Standalone guard"

# ── 4. the key handler itself refuses to run outside the Standalone wrapper
#       (defence in depth for AU / VST3 / AUv3, where the host owns MIDI input).
grep -q 'wrapperType != juce::AudioProcessor::wrapperType_Standalone' src/PluginEditor.cpp \
  && ok "key handler no-ops unless wrapperType is Standalone" \
  || bad "key handler no-ops unless wrapperType is Standalone"

# ── 5. Z lowers and X raises the octave, clamped to a playable range.
grep -q '== "z"' src/PluginEditor.cpp && grep -q '== "x"' src/PluginEditor.cpp \
  && ok "Z and X are handled as octave down / up" \
  || bad "Z and X are handled as octave down / up"
grep -q 'jlimit (0, 9' src/PluginEditor.cpp \
  && ok "octave clamped to the playable range 0..9" \
  || bad "octave clamped to the playable range 0..9"
grep -q 'setKeyPressBaseOctave' src/PluginEditor.cpp \
  && ok "the keyboard component's own key mapping follows the octave (setKeyPressBaseOctave)" \
  || bad "the keyboard component's own key mapping follows the octave (setKeyPressBaseOctave)"

# ── 6. the current octave is visible on screen.
grep -q 'octaveLabel_' src/PluginEditor.h \
  && ok "octave label declared in the editor" \
  || bad "octave label declared in the editor"
grep -q '"Octave "' src/PluginEditor.cpp \
  && ok "octave label text shows the current octave" \
  || bad "octave label text shows the current octave"

# ── 7. key-up sends note off; key repeat does not retrigger; focus loss
#       releases every held key.
grep -q 'noteOff' src/PluginEditor.cpp \
  && ok "key release sends a note off" \
  || bad "key release sends a note off"
grep -q '! *repeat' src/PluginEditor.cpp \
  && ok "key repeat is guarded against retriggering" \
  || bad "key repeat is guarded against retriggering"
grep -q 'releaseHeldKeys' src/PluginEditor.cpp \
  && ok "focus loss releases every held key (releaseHeldKeys)" \
  || bad "focus loss releases every held key (releaseHeldKeys)"

# ── 8. the shell page forwards keydown / keyup / blur to native.
grep -q "addEventListener('keydown'" src/ShellPage.h \
  && ok "shell page forwards keydown" \
  || bad "shell page forwards keydown"
grep -q "addEventListener('keyup'" src/ShellPage.h \
  && ok "shell page forwards keyup" \
  || bad "shell page forwards keyup"
grep -q "addEventListener('blur'" src/ShellPage.h \
  && ok "shell page reports window blur (focus loss)" \
  || bad "shell page reports window blur (focus loss)"
grep -q "call('keyEvent'" src/ShellPage.h \
  && ok "shell page calls the keyEvent native function" \
  || bad "shell page calls the keyEvent native function"

# ── 9. keys typed in a text field in the page do not play notes.
grep -q 'activeElement' src/ShellPage.h \
  && ok "page checks document.activeElement before forwarding" \
  || bad "page checks document.activeElement before forwarding"
grep -q "TEXTAREA" src/ShellPage.h && grep -q "'INPUT'" src/ShellPage.h \
  && ok "text inputs / textareas are excluded from key capture" \
  || bad "text inputs / textareas are excluded from key capture"

# ── 10. AU / VST3 / AUv3 are unchanged: the page only forwards when the
#        keyEvent function is actually registered (Standalone-only).
grep -q '__juce__functions' src/ShellPage.h \
  && ok "page checks __juce__functions so non-standalone wrappers never call keyEvent" \
  || bad "page checks __juce__functions so non-standalone wrappers never call keyEvent"

# ── 11. a key release sends the note-off for the note that key actually
#        started — remembered per physical key at key-down — not a note
#        recomputed from the octave current at key-up time. Shifting octaves
#        with Z/X between key-down and key-up must not strand a sounding note.
grep -q 'heldKeyNotesByKey_' src/PluginEditor.h \
  && ok "per-key sounding-note map declared in the editor" \
  || bad "per-key sounding-note map declared in the editor"
awk '
  /void KromosynthAudioProcessorEditor::handleKeyEvent/ { infn = 1; next }
  infn && /^}/ { exit !(upMap && !upRecompute) }
  infn {
    if ($0 ~ /else/) up = 1
    if (up && /heldKeyNotesByKey_/) upMap = 1
    if (up && /12 \* keyOctave_/) upRecompute = 1
  }
' src/PluginEditor.cpp \
  && ok "key-up note-off uses the note remembered at key-down, not the current octave" \
  || bad "key-up note-off uses the note remembered at key-down, not the current octave"
awk '
  /void KromosynthAudioProcessorEditor::releaseHeldKeys/ { infn = 1; next }
  infn && /^}/ { exit !saw }
  infn && /heldKeyNotesByKey_/ { saw = 1 }
' src/PluginEditor.cpp \
  && ok "focus loss releases every held key through the per-key map" \
  || bad "focus loss releases every held key through the per-key map"

echo
if (( fail == 0 )); then
  echo "check-standalone-keys: all criteria present"
  exit 0
else
  echo "check-standalone-keys: criteria missing (see FAIL lines)"
  exit 1
fi
