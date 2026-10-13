// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
#pragma once
// synthis-aa4.23 spike: which substrate opens an archive entry. PROVISIONAL.
//
// Pure functions, no JUCE, so the decision is testable without a host
// (tools/wclap-probe/routing_test.cpp). Nothing in the plugin calls this yet:
// InstrumentLibrary::Kind still has only { none, sampler, faust }. The wclap
// layout below (`instrument.wclap/module.wasm` + `state.bin`) is a PROPOSAL, not
// something any server produces. See docs/PLUGIN_AS_HOST_SPIKE.md.
#include <string>
#include <vector>

namespace kromosynth
{
enum class Substrate { none, sampler, faust, wclap };

/** @param files paths relative to the unpacked artefact root, '/' separated.
    A hosted guest wins over a bare Faust or SFZ file: a WCLAP entry may ship
    an `instrument.sfz` preview beside it, and the live engine is the point. */
inline Substrate substrateForFiles (const std::vector<std::string>& files)
{
    bool sfz = false, faustWasm = false, wclapModule = false;
    for (const auto& f : files)
    {
        if (f == "instrument.sfz") sfz = true;
        else if (f == "instrument.wasm") faustWasm = true;
        else if (f == "instrument.wclap/module.wasm") wclapModule = true;
    }
    if (wclapModule) return Substrate::wclap;
    if (faustWasm) return Substrate::faust;
    if (sfz) return Substrate::sampler;
    return Substrate::none;
}

inline const char* substrateName (Substrate s)
{
    switch (s)
    {
        case Substrate::sampler: return "sampler";
        case Substrate::faust:   return "faust";
        case Substrate::wclap:   return "wclap";
        default:                 return "none";
    }
}
}
