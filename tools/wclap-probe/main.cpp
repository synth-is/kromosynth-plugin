// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// wclap-probe — synthis-aa4.23 spike. Developer tool, never shipped, not in the gate.
//
// Drives ONE CLAP plugin headlessly through the same host code whether it is
//   * a WCLAP directory (module.wasm), via the wclap-bridge C API (Wasmtime), or
//   * a native .clap bundle (dlopen),
// so "plays", "state round-trips" and "CPU vs native" use identical measurement code.
//
//   wclap-probe <dir.wclap | x.clap> [--plugin-id ID] [--seconds N] [--block N] [--sr HZ]
//
// Prints KEY=VALUE lines; exit 0 only if every self-check passed.
#include <clap/clap.h>
#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "wclap-bridge.h"

namespace {

const clap_host_t kHost = {CLAP_VERSION, nullptr, "wclap-probe", "synthis", "", "0",
    [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};

struct Mem { std::vector<char> b; size_t rd = 0; };

clap_ostream_t mkOut(Mem* m) {
    return {m, [](const clap_ostream_t* s, const void* p, uint64_t n) -> int64_t {
        auto* m = (Mem*)s->ctx;
        m->b.insert(m->b.end(), (const char*)p, (const char*)p + n);
        return (int64_t)n; }};
}
clap_istream_t mkIn(Mem* m) {
    return {m, [](const clap_istream_t* s, void* p, uint64_t n) -> int64_t {
        auto* m = (Mem*)s->ctx;
        size_t k = std::min<size_t>(n, m->b.size() - m->rd);
        std::memcpy(p, m->b.data() + m->rd, k);
        m->rd += k;
        return (int64_t)k; }};
}

// One note-on per voice, all in the first block (synthis-et4.3: --voices N; 0 = idle plugin, for the
// per-voice CPU cost = (rt_fraction(N) - rt_fraction(0)) / N).
struct Events { std::vector<clap_event_note_t> on; bool sent = false; };

struct Run { std::vector<float> L; double seconds = 0; };

// One note-on in the first block, then silence of events.
Run render(const clap_plugin_t* p, double sr, uint32_t block, double secs, int key, int voices = 1) {
    (void)key;  // superseded by the chord table below
    Run r;
    const uint32_t total = (uint32_t)(sr * secs);
    std::vector<float> l(block), rr(block);
    float* ch[2] = {l.data(), rr.data()};
    clap_audio_buffer_t out{};
    out.channel_count = 2;
    out.data32 = ch;
    Events ev;
    // The same chords as tools/wclap-host-probe, so the in-plugin and native numbers play the same notes:
    // N=1 {60}, N=4 {60,64,67,72}, N=8 {48..76}, N=16 {36..96}. Velocity 100/127 as in that probe.
    static const int kChord[16] = {36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96};
    const int first = voices >= 16 ? 0 : voices >= 8 ? 3 : voices >= 4 ? 6 : 6;
    for (int v = 0; v < voices; ++v) {
        clap_event_note_t n{};
        n.header = {sizeof(clap_event_note_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0};
        n.note_id = v + 1;
        n.key = (int16_t)kChord[(first + v) % 16];
        n.velocity = 100.0 / 127.0;
        ev.on.push_back(n);
    }
    clap_input_events_t in{&ev,
        [](const clap_input_events_t* x) -> uint32_t { auto* e = (Events*)x->ctx; return e->sent ? 0 : (uint32_t)e->on.size(); },
        [](const clap_input_events_t* x, uint32_t i) -> const clap_event_header_t* {
            return &((Events*)x->ctx)->on[i].header; }};
    clap_output_events_t outEv{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
    auto t0 = std::chrono::steady_clock::now();
    for (uint32_t done = 0; done < total; done += block) {
        clap_process_t pr{};
        pr.frames_count = block;
        pr.audio_outputs = &out;
        pr.audio_outputs_count = 1;
        pr.in_events = &in;
        pr.out_events = &outEv;
        pr.steady_time = done;
        p->process(p, &pr);
        ev.sent = true;
        r.L.insert(r.L.end(), l.begin(), l.end());
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

double rms(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += (double)x * x;
    return v.empty() ? 0 : std::sqrt(s / v.size());
}
double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, (double)std::fabs(a[i] - b[i]));
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // output must survive a hang/kill (aa4.104)
    if (argc < 2) {
        std::fprintf(stderr, "usage: wclap-probe <dir.wclap|x.clap> [--plugin-id ID] [--seconds N] [--block N] [--sr HZ]\n");
        return 2;
    }
    std::string path = argv[1], wantId;
    double secs = 4, sr = 48000;
    uint32_t block = 256;
    int voices = 1;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        if (k == "--plugin-id") wantId = argv[i + 1];
        else if (k == "--seconds") secs = atof(argv[i + 1]);
        else if (k == "--block") block = (uint32_t)atoi(argv[i + 1]);
        else if (k == "--sr") sr = atof(argv[i + 1]);
        else if (k == "--voices") voices = atoi(argv[i + 1]);
    }
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    const bool isWclap = path.size() < 5 || path.substr(path.size() - 5) != ".clap";

    const clap_plugin_factory_t* fac = nullptr;
    void* wh = nullptr;
    void* dl = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    if (isWclap) {
        if (!wclap_global_init(getenv("WCLAP_PROBE_TIME_LIMIT_MS") ? (unsigned)atoi(getenv("WCLAP_PROBE_TIME_LIMIT_MS")) : 2000)) { std::puts("FAIL=global_init"); return 1; }
        const char* sd = std::getenv("WCLAP_PROBE_DATA");  // optional: dir holding presets/ cache/ var/
        std::string pre = sd ? std::string(sd) + "/presets" : "", ca = sd ? std::string(sd) + "/cache" : "",
                    va = sd ? std::string(sd) + "/var" : "";
        wh = sd ? wclap_open_with_dirs(path.c_str(), pre.c_str(), ca.c_str(), va.c_str()) : wclap_open(path.c_str());
        char err[512] = {0};
        if (!wh || wclap_get_error(wh, err, sizeof err)) { std::printf("FAIL=open %s\n", err); return 1; }
        fac = (const clap_plugin_factory_t*)wclap_get_factory(wh, CLAP_PLUGIN_FACTORY_ID);
    } else {
        std::string base = path.substr(path.rfind('/') + 1);
        base = base.substr(0, base.size() - 5);
        std::string bin = path + "/Contents/MacOS/" + base;
        dl = dlopen(bin.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!dl) { std::printf("FAIL=dlopen %s\n", dlerror()); return 1; }
        entry = (const clap_plugin_entry_t*)dlsym(dl, "clap_entry");
        if (!entry || !entry->init(path.c_str())) { std::puts("FAIL=clap_entry"); return 1; }
        fac = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    }
    if (!fac) { std::puts("FAIL=no_factory"); return 1; }

    const clap_plugin_descriptor_t* d = nullptr;
    for (uint32_t i = 0; i < fac->get_plugin_count(fac); ++i) {
        auto* x = fac->get_plugin_descriptor(fac, i);
        std::printf("PLUGIN[%u]=%s | %s\n", i, x->id, x->name);
        if (!d && (wantId.empty() || wantId == x->id)) d = x;
    }
    if (!d) { std::puts("FAIL=plugin_id_not_found"); return 1; }
    std::printf("MODE=%s\nPLUGIN_ID=%s\n", isWclap ? "wclap" : "native", d->id);

    auto make = [&]() -> const clap_plugin_t* {
        auto* p = fac->create_plugin(fac, &kHost, d->id);
        if (!p) {
            std::puts("STEP_FAILED=create_plugin");
            char err[1024] = {0};
            if (wh && wclap_get_error(wh, err, sizeof err)) std::printf("WCLAP_ERROR=%s\n", err);
            return nullptr;
        }
        if (!p->init(p)) { std::puts("STEP_FAILED=init"); return nullptr; }
        if (!p->activate(p, sr, 32, block)) { std::puts("STEP_FAILED=activate"); return nullptr; }
        p->start_processing(p);
        return p;
    };
    const clap_plugin_t* a = make();
    if (!a) { std::puts("FAIL=create_activate"); return 1; }

    auto* params = (const clap_plugin_params_t*)a->get_extension(a, CLAP_EXT_PARAMS);
    auto* state = (const clap_plugin_state_t*)a->get_extension(a, CLAP_EXT_STATE);
    auto* gui = (const clap_plugin_gui_t*)a->get_extension(a, CLAP_EXT_GUI);
    std::printf("EXT_PARAMS=%d\nEXT_STATE=%d\nEXT_GUI=%d\nPARAM_COUNT=%u\n", params != nullptr, state != nullptr,
                gui != nullptr, params ? params->count(a) : 0);

    int rc = 0;
    render(a, sr, block, 0.5, 60, voices);  // warm-up (JIT/caches), discarded
    Run r1 = render(a, sr, block, secs, 60, voices);
    double lvl = rms(r1.L);
    std::printf("VOICES=%d\nRMS=%g\nCPU_RT_FRACTION=%g\n", voices, lvl, r1.seconds / secs);
    if (voices == 0) std::puts("CHECK_PLAYS=SKIPPED(idle)");
    else if (lvl > 1e-5) std::puts("CHECK_PLAYS=PASS"); else { std::puts("CHECK_PLAYS=FAIL"); rc = 1; }

    if (state) {
        // Stage 1, one instance: save, load the same bytes back into it, save again.
        Mem m1;
        auto os1 = mkOut(&m1);
        bool saved = state->save(a, &os1);
        std::printf("STATE_BYTES=%zu\n", m1.b.size());
        Mem back = m1;
        auto ib = mkIn(&back);
        bool loaded = saved && state->load(a, &ib);
        Mem m2;
        auto os2 = mkOut(&m2);
        bool resaved = loaded && state->save(a, &os2);
        bool same1 = resaved && m2.b == m1.b;
        std::printf("STATE_SAME_INSTANCE saved=%d loaded=%d resaved_equal=%d\n", saved, loaded, same1);
        std::printf("CHECK_STATE_ROUNDTRIP_SAME_INSTANCE=%s\n", same1 ? "PASS" : "FAIL");
        if (!same1) rc = 1;
        std::fflush(stdout);

        // Stage 2, a fresh instance (what a DAW does on session reload): load the bytes, save, compare,
        // and check it still makes sound. Audio is not compared sample for sample: the native Surge XT
        // does not render two fresh instances identically either (random oscillator phase).
        const clap_plugin_t* b = make();
        // CLAP: extensions are per plugin instance. wclap-bridge keeps the guest's extension pointers
        // in the Plugin object and fills them in on get_extension(), so using `a`'s `state` struct on
        // `b` without asking `b` calls guest function-table slot 0 and Wasmtime panics
        // ("object used with the wrong store": a null funcref has no store). See
        // docs/PLUGIN_AS_HOST_SPIKE.md finding 5.
        auto* stateB = b ? (const clap_plugin_state_t*)b->get_extension(b, CLAP_EXT_STATE) : nullptr;
        Mem mb = m1;
        auto ibb = mkIn(&mb);
        bool l2 = stateB && stateB->load(b, &ibb);
        Mem m3;
        auto os3 = mkOut(&m3);
        bool s3 = l2 && stateB->save(b, &os3);
        Run rb;
        if (l2) rb = render(b, sr, block, 1.0, 64);
        bool same2 = s3 && m3.b == m1.b && rms(rb.L) > 1e-5;
        std::printf("STATE_FRESH_INSTANCE loaded=%d resaved_equal=%d rms=%g\n", l2, s3 && m3.b == m1.b, rms(rb.L));
        std::printf("CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE=%s\n", same2 ? "PASS" : "FAIL");
        if (!same2) rc = 1;
        if (b) { b->stop_processing(b); b->deactivate(b); b->destroy(b); }
    } else {
        std::puts("CHECK_STATE_ROUNDTRIP_SAME_INSTANCE=FAIL(no state extension)");
        rc = 1;
    }

    a->stop_processing(a);
    a->deactivate(a);
    a->destroy(a);
    if (isWclap) { wclap_close(wh); wclap_global_deinit(); } else { entry->deinit(); dlclose(dl); }
    return rc;
}
