#!/usr/bin/env python3
"""
Generate two Reaper projects that play identical MIDI through the AU and the
VST3 build of the same plugin, so the two renders can be diffed.

DEVELOPER TOOL, never shipped. Same category as tools/sfz-probe.

Why generated rather than committed as a fixture: a .rpp carries absolute paths
and a host-specific plugin identifier, so a committed one would work on exactly
one machine and could silently load the wrong thing on any other. Generating it
puts the machine-specific parts in one visible place.

Why two projects rather than one with two tracks: Reaper's stem-render
semantics are another thing that could be wrong, and a wrong render mode looks
exactly like a plugin that does not play. Two projects each rendering a plain
master mix removes that variable.

Plugin identifiers are read out of Reaper's own scan caches rather than
guessed, so if Reaper has not scanned the plugin this fails loudly instead of
producing a project with a silently unresolved FX slot.

Usage:
    python3 make_projects.py <output-dir>
"""

import base64
import os
import re
import sys

REAPER_CFG = os.path.expanduser("~/Library/Application Support/REAPER")
VST_CACHE = os.path.join(REAPER_CFG, "reaper-vstplugins_arm64.ini")
AU_CACHE = os.path.join(REAPER_CFG, "reaper-auplugins_arm64.ini")

PLUGIN_NAME = "Synth.is"

# 960 ticks per quarter note at 120 BPM, so 1920 ticks per second.
TPQN = 960
TICKS_PER_SEC = 1920


def read_vst3_ident():
    """Pull the VST3 identifiers out of Reaper's scan cache.

    Line shape:
        Synth.is.vst3=<hash>,<int-id>{<uid>,Synth.is (Synth.is)!!!VSTi
    """
    with open(VST_CACHE, "r", errors="replace") as handle:
        for line in handle:
            if not line.startswith(PLUGIN_NAME + ".vst3="):
                continue
            match = re.search(r"=([0-9A-F]+),(\d+)\{([0-9A-F]+),(.+?)!!!", line.strip())
            if match:
                return match.group(2), match.group(3), match.group(4)
    raise SystemExit(
        PLUGIN_NAME + ".vst3 not found in " + VST_CACHE + "\n"
        "Reaper has not scanned it. Open Reaper once, or check the VST3 is "
        "installed in ~/Library/Audio/Plug-Ins/VST3."
    )


def check_au_present():
    with open(AU_CACHE, "r", errors="replace") as handle:
        if PLUGIN_NAME + ": " + PLUGIN_NAME + "=" in handle.read():
            return
    raise SystemExit("AU not found in " + AU_CACHE + ". Reaper has not scanned it.")


def midi_events():
    """Four single notes at rising velocity, then a four-note chord.

    Chosen to exercise the three things this comparison should be sensitive to:

      - Velocity layers. The RR artefact's bands are 1-42, 43-84, 85-127
        (measured, section G6), so 20 / 60 / 100 / 127 land in v1 / v2 / v3 / v3
        and the first three must sound like different samples rather than one
        sample at three gains.
      - Polyphony. Four notes is 20 voices on an XY instrument at section G4's
        five-per-note.
      - Release tails. Every note-off is followed by enough silence for
        ampeg_release (0.3 s at CC73 zero) to run out inside the render.

    Returns a list of (absolute_tick, status, data1, data2).
    """
    events = []
    note = 60
    chord = [60, 64, 67, 72]

    tick = 0
    for velocity in (20, 60, 100, 127):
        events.append((tick, 0x90, note, velocity))
        events.append((tick + TICKS_PER_SEC // 2, 0x80, note, 0))
        tick += TICKS_PER_SEC

    for pitch in chord:
        events.append((tick, 0x90, pitch, 100))
    for pitch in chord:
        events.append((tick + TICKS_PER_SEC, 0x80, pitch, 0))

    return sorted(events, key=lambda e: e[0])


def midi_source_block(indent):
    lines = [indent + "<SOURCE MIDI", indent + "  HASDATA 1 " + str(TPQN) + " QN"]

    previous = 0
    for tick, status, data1, data2 in midi_events():
        lines.append(
            "%s  E %d %02x %02x %02x" % (indent, tick - previous, status, data1, data2)
        )
        previous = tick

    lines.append("%s  E %d b0 7b 00" % (indent, TICKS_PER_SEC))
    lines.append(indent + ">")
    return "\n".join(lines)


def fx_block(kind, vst3_int_id, vst3_uid, vst3_label):
    """Reaper's FX chain entry.

    The trailing base64 is the plugin state Reaper would have saved. Ours is
    genuinely stateless -- getStateInformation writes nothing -- so an empty
    blob is correct rather than merely tolerated. If the plugin gains state
    this needs regenerating from a project Reaper saved itself.
    """
    if kind == "vst3":
        header = (
            '<VST "VST3i: %s" %s.vst3 0 "" %s{%s} ""'
            % (vst3_label, PLUGIN_NAME, vst3_int_id, vst3_uid)
        )
    else:
        header = '<AU "AUi: %s: %s" "" 0 "" 0 ""' % (PLUGIN_NAME, PLUGIN_NAME)

    state = base64.b64encode(b"\x00" * 8).decode()

    return "\n".join(
        [
            "    <FXCHAIN",
            "      SHOW 0",
            "      LASTSEL 0",
            "      DOCKED 0",
            "      " + header,
            "        " + state,
            "      >",
            "    >",
        ]
    )


PROJECT = """<REAPER_PROJECT 0.1 "7.79/macOS-arm64" 1
  TEMPO 120 4 4
  SAMPLERATE 44100 0 0
  RENDER_FILE ""
  RENDER_PATTERN ""
  RENDER_FMT 0 2 0
  RENDER_1X 0
  RENDER_RANGE 1 0 0 18 1000
  RENDER_RESAMPLE 3 0 1
  RENDER_ADDTOPROJ 0
  RENDER_STEMS 0
  RENDER_DITHER 0
  RENDER_CHANNELS 2
  RENDER_SRATE 48000
  <RENDER_CFG
    {render_cfg}
  >
  <TRACK
    NAME "{name}"
    TRACKHEIGHT 0 0
    VOLPAN 1 0 -1 -1 1
    MUTESOLO 0 0 0
    IPHASE 0
    ISBUS 0 0
    MAINSEND 1 0
    <ITEM
      POSITION 0
      LENGTH {length}
      LOOP 0
      NAME "test"
{midi}
    >
{fx}
  >
>
"""

# "evaw" + int32 bit depth. 24 bit is 144 dB of headroom, far more than a
# format comparison needs, and it avoids guessing a float-flag encoding.
RENDER_CFG_WAV24 = base64.b64encode(b"evaw" + (24).to_bytes(4, "little")).decode()


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)

    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)

    vst3_int_id, vst3_uid, vst3_label = read_vst3_ident()
    check_au_present()

    last_tick = max(e[0] for e in midi_events())
    length = last_tick / TICKS_PER_SEC + 4.0

    for kind in ("au", "vst3"):
        text = PROJECT.format(
            name=kind.upper(),
            length="%.6f" % length,
            midi=midi_source_block("      "),
            fx=fx_block(kind, vst3_int_id, vst3_uid, vst3_label),
            render_cfg=RENDER_CFG_WAV24,
        )
        path = os.path.join(out_dir, kind + ".rpp")
        with open(path, "w") as handle:
            handle.write(text)
        print("wrote " + path)

    print("vst3 uid   " + vst3_uid)
    print("vst3 id    " + vst3_int_id)
    print("length     %.3f s" % length)


if __name__ == "__main__":
    main()
