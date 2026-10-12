// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// Plain-C++ test for src/SubstrateRouting.h. Exit 0 = pass.
#include <cstdio>
#include "../../src/SubstrateRouting.h"
using namespace kromosynth;
static int fails = 0;
#define EXPECT(files, want) do { if (substrateForFiles files != (want)) { \
    std::printf("FAIL line %d: got %s want %s\n", __LINE__, substrateName(substrateForFiles files), substrateName(want)); ++fails; } } while (0)
int main() {
    EXPECT(({}), Substrate::none);
    EXPECT(({"readme.txt"}), Substrate::none);
    EXPECT(({"instrument.sfz", "samples/a.wav"}), Substrate::sampler);
    EXPECT(({"instrument.wasm", "instrument.json"}), Substrate::faust);
    EXPECT(({"instrument.wclap/module.wasm", "instrument.wclap/ui/index.html", "state.bin"}), Substrate::wclap);
    EXPECT(({"instrument.sfz", "instrument.wclap/module.wasm"}), Substrate::wclap);   // live engine beats preview
    EXPECT(({"instrument.sfz", "instrument.wasm"}), Substrate::faust);
    EXPECT(({"instrument.wclap/ui/index.html"}), Substrate::none);                      // no module = not playable
    std::puts(fails ? "routing_test: FAIL" : "routing_test: PASS");
    return fails ? 1 : 0;
}
