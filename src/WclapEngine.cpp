// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// See WclapEngine.h. synthis-et4.3 spike.

#include "WclapEngine.h"

#include <juce_events/juce_events.h>

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>
#include <wclap-bridge.h>

#ifdef KROMOSYNTH_WCLAP_NATIVE_REFERENCE
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
// ── the bridge's process-wide state ─────────────────────────────────────────
// wclap_global_init configures ONE Wasmtime engine for the process, and wclap_global_deinit ABORTS if a
// WCLAP is still open. Several plugin instances can live in one host, so: count users, init on the
// first, deinit on the last (which also joins the epoch thread before a host can unload this dylib).
std::mutex gBridgeMutex;
int gBridgeUsers = 0;

bool bridgeAcquire (juce::String& why)
{
    std::lock_guard<std::mutex> lock (gBridgeMutex);

    if (gBridgeUsers == 0 && ! wclap_global_init (WclapEngine::timeLimitMs()))
    {
        why = "wclap_global_init failed";
        return false;
    }

    ++gBridgeUsers;
    return true;
}

void bridgeRelease()
{
    std::lock_guard<std::mutex> lock (gBridgeMutex);

    if (--gBridgeUsers == 0)
        wclap_global_deinit();
}


struct Mem
{
    juce::MemoryBlock* out = nullptr;
    const char* in = nullptr;
    size_t size = 0, rd = 0;
};

constexpr int kSettleFrames = 512;
constexpr size_t kMaxEvents = 2048;

struct EventSlot
{
    union
    {
        clap_event_header_t header;
        clap_event_note_t note;
        clap_event_midi_t midi;
        clap_event_param_value_t param;
    };
    EventSlot() { std::memset (this, 0, sizeof (*this)); }
};
}

struct WclapEngine::Impl
{
    // One clap_host_t per engine: host_data finds us again in the callbacks. request_callback is what
    // makes the guest's page updates timely: without it the guest falls back to sending from inside
    // process(), on the audio thread.
    clap_host_t host = { CLAP_VERSION, this, "Synth.is", "Synth.is", "https://synth.is", "0",
        [] (const clap_host_t* h, const char* id) -> const void*
        {
            if (id != nullptr && std::strcmp (id, CLAP_EXT_WEBVIEW) == 0)
            {
                static const clap_host_webview_t ext {
                    [] (const clap_host_t* hh, const void* buf, uint32_t size) -> bool
                    {
                        auto* self = static_cast<Impl*> (hh->host_data);
                        const juce::SpinLock::ScopedLockType l (self->sinkLock);
                        if (! self->toPage) return false;   // the extension: fail while the page is not open
                        self->toPage (buf, size);
                        return true;
                    } };
                return &ext;
            }
            (void) h;
            return nullptr;
        },
        [] (const clap_host_t*) {}, [] (const clap_host_t*) {},
        [] (const clap_host_t* h)
        {
            auto* self = static_cast<Impl*> (h->host_data);
            if (self->mainThreadWanted.exchange (true)) return;   // one callback in flight is enough
            auto alive = self->alive;
            juce::MessageManager::callAsync ([self, alive]
            {
                if (*alive) self->runMainThreadCallback();
            });
        } };

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // message thread only
    std::atomic<bool> mainThreadWanted { false };
    juce::SpinLock sinkLock;
    PageSink toPage;
    const clap_plugin_gui_t* gui = nullptr;
    const clap_plugin_webview_t* webview = nullptr;
    bool guiCreated = false;

    void runMainThreadCallback()
    {
        mainThreadWanted = false;
        if (plugin != nullptr) plugin->on_main_thread (plugin);
    }

    void closeGui()
    {
        {
            const juce::SpinLock::ScopedLockType l (sinkLock);
            toPage = nullptr;
        }
        if (guiCreated && gui != nullptr && plugin != nullptr) gui->destroy (plugin);
        guiCreated = false;
    }

    void* module = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_params_t* params = nullptr;
    juce::SpinLock paramLock;
    std::vector<std::pair<juce::uint32, double>> pendingParams;   // reserved at load
    bool bridgeHeld = false;
   #ifdef KROMOSYNTH_WCLAP_NATIVE_REFERENCE
    // MEASUREMENT ONLY (tools/wclap-host-probe): with KROMOSYNTH_WCLAP_NATIVE_CLAP=<x.clap> the same
    // engine, event translation and process() loop drive the NATIVE build of the plugin instead of the
    // WebAssembly one, so "CPU per voice against native, in the plugin" differs by the guest and nothing
    // else. Never defined for the shipping targets.
    void* dl = nullptr;
    const clap_plugin_entry_t* nativeEntry = nullptr;
   #endif

    juce::File wclapDir, dataDir;
    juce::String pluginId;
    double rate = 48000.0;
    int maxBlock = 512;

    juce::CriticalSection lock;
    std::vector<EventSlot> events;
    std::vector<int> evTime;   // sample offset of each stored event
    size_t numEvents = 0, nextIn = 0;
    std::vector<float> scratchL, scratchR;
    juce::int64 steady = 0;

    std::string lastError() const
    {
        char err[512] = {};
        if (module != nullptr && wclap_get_error (module, err, sizeof err))
            return err;
        return {};
    }

    void destroyPlugin()
    {
        closeGui();
        gui = nullptr;
        webview = nullptr;
        if (plugin != nullptr)
        {
            plugin->stop_processing (plugin);
            plugin->deactivate (plugin);
            plugin->destroy (plugin);
        }
        plugin = nullptr;
        state = nullptr;
        params = nullptr;
    }

    void closeAll()
    {
        destroyPlugin();
        factory = nullptr;
        if (module != nullptr) { wclap_close (module); module = nullptr; }
       #ifdef KROMOSYNTH_WCLAP_NATIVE_REFERENCE
        if (nativeEntry != nullptr) { nativeEntry->deinit(); nativeEntry = nullptr; }
        if (dl != nullptr) { dlclose (dl); dl = nullptr; }
       #endif
        if (bridgeHeld) { bridgeRelease(); bridgeHeld = false; }
    }

    // Runs `frames` frames of silence-in-events through the plugin, output discarded.
    void renderBlocks (int frames)
    {
        numEvents = nextIn = 0;
        for (int done = 0; done < frames; done += maxBlock)
            runChunk (std::min (maxBlock, frames - done));
    }

    // Feeds events[nextIn..) whose time falls inside [0, frames) (already offset to this chunk).
    void runChunk (int frames)
    {
        float* ch[2] = { scratchL.data(), scratchR.data() };
        clap_audio_buffer_t out {};
        out.channel_count = 2;
        out.data32 = ch;

        clap_input_events_t in { this,
            [] (const clap_input_events_t* x) -> uint32_t
            { auto* s = (Impl*) x->ctx; return (uint32_t) (s->numEvents - s->nextIn); },
            [] (const clap_input_events_t* x, uint32_t i) -> const clap_event_header_t*
            { auto* s = (Impl*) x->ctx; return &s->events[s->nextIn + i].header; } };
        clap_output_events_t outEv { nullptr,
            [] (const clap_output_events_t*, const clap_event_header_t*) { return true; } };

        clap_process_t pr {};
        pr.steady_time = steady;
        pr.frames_count = (uint32_t) frames;
        pr.audio_outputs = &out;
        pr.audio_outputs_count = 1;
        pr.in_events = &in;
        pr.out_events = &outEv;
        plugin->process (plugin, &pr);
        steady += frames;
    }

    // create -> init -> activate -> start_processing -> one empty block.
    bool createPlugin (juce::String& why)
    {
        plugin = factory->create_plugin (factory, &host, pluginId.toRawUTF8());
        if (plugin == nullptr) { why = "create_plugin returned null " + juce::String (lastError()); return false; }
        if (! plugin->init (plugin)) { why = "plugin init failed " + juce::String (lastError()); destroyPlugin(); return false; }
        if (! plugin->activate (plugin, rate, 1, (uint32_t) maxBlock))
        { why = "activate failed " + juce::String (lastError()); destroyPlugin(); return false; }
        plugin->start_processing (plugin);
        // CLAP: extensions are per plugin INSTANCE (finding 5); ask this instance.
        state = (const clap_plugin_state_t*) plugin->get_extension (plugin, CLAP_EXT_STATE);
        params = (const clap_plugin_params_t*) plugin->get_extension (plugin, CLAP_EXT_PARAMS);
        gui = (const clap_plugin_gui_t*) plugin->get_extension (plugin, CLAP_EXT_GUI);
        webview = (const clap_plugin_webview_t*) plugin->get_extension (plugin, CLAP_EXT_WEBVIEW);
        steady = 0;
        renderBlocks (kSettleFrames);
        return true;
    }

    bool save (juce::MemoryBlock& dest)
    {
        if (state == nullptr) return false;
        Mem m; m.out = &dest;
        dest.reset();
        clap_ostream_t os { &m, [] (const clap_ostream_t* s, const void* p, uint64_t n) -> int64_t
            { ((Mem*) s->ctx)->out->append (p, (size_t) n); return (int64_t) n; } };
        return state->save (plugin, &os);
    }

    bool load (const void* data, size_t size)
    {
        if (state == nullptr) return false;
        Mem m; m.in = (const char*) data; m.size = size;
        clap_istream_t is { &m, [] (const clap_istream_t* s, void* p, uint64_t n) -> int64_t
            { auto* m = (Mem*) s->ctx;
              const auto k = std::min<size_t> ((size_t) n, m->size - m->rd);
              std::memcpy (p, m->in + m->rd, k); m->rd += k; return (int64_t) k; } };
        if (! state->load (plugin, &is)) return false;
        renderBlocks (kSettleFrames);
        return true;
    }
};

WclapEngine::WclapEngine() : impl_ (std::make_unique<Impl>()) {}
WclapEngine::~WclapEngine() { unload(); *impl_->alive = false; }

unsigned WclapEngine::timeLimitMs()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_WCLAP_TIME_LIMIT_MS", {});
    if (env.isNotEmpty()) return (unsigned) env.getIntValue();
    return 60000;   // PINNED (steering 2026-10-26, synthis-et4.11): a hung guest taking down the DAW is
                    // worse than the ~30 % CPU the limit costs — docs/PLUGIN_WCLAP_HOST_SPIKE.md, CPU
                    // section. The bridge default is 2000 ms, which kills Surge XT's init (finding 2).
}

WclapEngine::LoadResult WclapEngine::load (const juce::File& wclapDir, const juce::File& dataDir,
                                           double sampleRate, int maxBlock)
{
    unload();
    LoadResult r;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto& s = *impl_;
    const juce::ScopedLock sl (s.lock);

    s.wclapDir = wclapDir;
    s.dataDir = dataDir;
    s.rate = sampleRate;
    s.maxBlock = std::max (maxBlock, 1);
    s.events.assign (kMaxEvents, EventSlot());
    s.evTime.reserve (kMaxEvents);
    s.pendingParams.reserve (64);
    s.scratchL.assign ((size_t) s.maxBlock, 0.0f);
    s.scratchR.assign ((size_t) s.maxBlock, 0.0f);

    for (auto* sub : { "presets", "cache", "var" })
        dataDir.getChildFile (sub).createDirectory();

   #ifdef KROMOSYNTH_WCLAP_NATIVE_REFERENCE
    if (const auto nat = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_WCLAP_NATIVE_CLAP", {}); nat.isNotEmpty())
    {
        const auto bin = juce::File (nat).getChildFile ("Contents/MacOS").getChildFile (juce::File (nat).getFileNameWithoutExtension());
        s.dl = dlopen (bin.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
        s.nativeEntry = s.dl != nullptr ? (const clap_plugin_entry_t*) dlsym (s.dl, "clap_entry") : nullptr;
        if (s.nativeEntry == nullptr || ! s.nativeEntry->init (nat.toRawUTF8())) { r.detail = "native clap_entry failed"; s.nativeEntry = nullptr; s.closeAll(); return r; }
        s.factory = (const clap_plugin_factory_t*) s.nativeEntry->get_factory (CLAP_PLUGIN_FACTORY_ID);
        if (s.factory == nullptr) { r.detail = "native: no factory"; s.closeAll(); return r; }
    }
    else
   #endif
    {
    if (! bridgeAcquire (r.detail)) return r;
    s.bridgeHeld = true;

    s.module = wclap_open_with_dirs (wclapDir.getFullPathName().toRawUTF8(),
                                     dataDir.getChildFile ("presets").getFullPathName().toRawUTF8(),
                                     dataDir.getChildFile ("cache").getFullPathName().toRawUTF8(),
                                     dataDir.getChildFile ("var").getFullPathName().toRawUTF8());
    if (s.module == nullptr) { r.detail = "wclap_open failed"; s.closeAll(); return r; }
    if (const auto e = s.lastError(); ! e.empty()) { r.detail = "wclap_open: " + juce::String (e); s.closeAll(); return r; }

    s.factory = (const clap_plugin_factory_t*) wclap_get_factory (s.module, CLAP_PLUGIN_FACTORY_ID);
    if (s.factory == nullptr) { r.detail = "no plugin factory"; s.closeAll(); return r; }
    }

    // The first descriptor that says "instrument" (the Surge XT bundle also lists an effects plugin),
    // else the first descriptor at all.
    const clap_plugin_descriptor_t* chosen = nullptr;
    const auto count = s.factory->get_plugin_count (s.factory);
    for (uint32_t i = 0; i < count && chosen == nullptr; ++i)
    {
        const auto* d = s.factory->get_plugin_descriptor (s.factory, i);
        for (const char* const* f = d != nullptr ? d->features : nullptr; f != nullptr && *f != nullptr; ++f)
            if (juce::String (*f) == CLAP_PLUGIN_FEATURE_INSTRUMENT) { chosen = d; break; }
    }
    if (chosen == nullptr && count > 0) chosen = s.factory->get_plugin_descriptor (s.factory, 0);
    if (chosen == nullptr) { r.detail = "bundle lists no plugins"; s.closeAll(); return r; }

    s.pluginId = chosen->id;
    r.pluginId = chosen->id;
    r.pluginName = chosen->name;
    name_ = chosen->name;

    juce::String why;
    if (! s.createPlugin (why)) { r.detail = why; s.closeAll(); return r; }

    r.ok = true;
    r.detail = r.pluginId;
    r.loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
    loaded_ = true;
    ++generation_;
    return r;
}

bool WclapEngine::reactivate (double sampleRate, int maxBlock)
{
    auto& s = *impl_;
    const juce::ScopedLock sl (s.lock);
    if (! loaded_) return false;

    juce::MemoryBlock saved;
    const bool haveState = s.save (saved);

    guiOpen_ = false;
    s.destroyPlugin();
    s.rate = sampleRate;
    s.maxBlock = std::max (maxBlock, 1);
    s.scratchL.assign ((size_t) s.maxBlock, 0.0f);
    s.scratchR.assign ((size_t) s.maxBlock, 0.0f);

    juce::String why;
    if (! s.createPlugin (why)) { loaded_ = false; return false; }
    if (haveState) s.load (saved.getData(), saved.getSize());
    ++generation_;
    return true;
}

void WclapEngine::unload()
{
    auto& s = *impl_;
    const juce::ScopedLock sl (s.lock);
    loaded_ = false;
    guiOpen_ = false;
    s.closeAll();
}

int WclapEngine::numParams()
{
    const juce::ScopedLock sl (impl_->lock);
    return loaded_ && impl_->params != nullptr ? (int) impl_->params->count (impl_->plugin) : 0;
}

bool WclapEngine::paramInfo (int index, ParamInfo& out)
{
    const juce::ScopedLock sl (impl_->lock);
    if (! loaded_ || impl_->params == nullptr) return false;
    clap_param_info_t i {};
    if (! impl_->params->get_info (impl_->plugin, (uint32_t) index, &i)) return false;
    out.id = i.id;
    out.name = juce::String::fromUTF8 (i.name);
    out.minValue = i.min_value;
    out.maxValue = i.max_value;
    out.defaultValue = i.default_value;
    out.hidden = (i.flags & CLAP_PARAM_IS_HIDDEN) != 0;
    out.readOnly = (i.flags & CLAP_PARAM_IS_READONLY) != 0;
    out.stepped = (i.flags & CLAP_PARAM_IS_STEPPED) != 0;
    return true;
}

void WclapEngine::queueParam (juce::uint32 id, double value)
{
    const juce::SpinLock::ScopedLockType pl (impl_->paramLock);
    impl_->pendingParams.emplace_back (id, value);
}

bool WclapEngine::saveState (juce::MemoryBlock& dest)
{
    const juce::ScopedLock sl (impl_->lock);
    return loaded_ && impl_->save (dest);
}

bool WclapEngine::loadState (const void* data, size_t size)
{
    const juce::ScopedLock sl (impl_->lock);
    return loaded_ && impl_->load (data, size);
}

void WclapEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();

    const juce::ScopedTryLock tl (impl_->lock);
    if (! tl.isLocked() || ! loaded_) return;

    auto& s = *impl_;
    const int total = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // MIDI -> CLAP events, all up front; each chunk then consumes the ones inside it.
    s.numEvents = 0;
    s.nextIn = 0;
    auto& evTime = s.evTime;
    evTime.clear();

    {
        const juce::SpinLock::ScopedTryLockType pl (s.paramLock);
        if (pl.isLocked())
        {
            for (const auto& [id, value] : s.pendingParams)
            {
                auto& e = s.events[s.numEvents];
                std::memset (&e, 0, sizeof (e));
                e.header.size = sizeof (clap_event_param_value_t);
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.header.type = CLAP_EVENT_PARAM_VALUE;
                e.param.param_id = id;
                e.param.note_id = -1;
                e.param.port_index = e.param.channel = e.param.key = -1;
                e.param.value = value;
                evTime.push_back (0);
                ++s.numEvents;
            }
            s.pendingParams.clear();
        }
    }

    for (const auto meta : midi)
    {
        if (s.numEvents >= kMaxEvents) break;
        const auto m = meta.getMessage();
        auto& e = s.events[s.numEvents];
        std::memset (&e, 0, sizeof (e));
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.flags = 0;

        if (m.isNoteOn() || m.isNoteOff())
        {
            e.header.size = sizeof (clap_event_note_t);
            e.header.type = m.isNoteOn() ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
            e.note.note_id = -1;
            e.note.port_index = 0;
            e.note.channel = (int16_t) (m.getChannel() - 1);
            e.note.key = (int16_t) m.getNoteNumber();
            e.note.velocity = m.isNoteOn() ? m.getFloatVelocity() : 0.5;
        }
        else
        {
            // Everything else (CC incl. all-notes-off, pitch bend, aftertouch) as a raw MIDI 1.0 event.
            const int n = m.getRawDataSize();
            if (n < 1 || n > 3) continue;
            e.header.size = sizeof (clap_event_midi_t);
            e.header.type = CLAP_EVENT_MIDI;
            e.midi.port_index = 0;
            std::memcpy (e.midi.data, m.getRawData(), (size_t) n);
        }

        evTime.push_back (meta.samplePosition);
        ++s.numEvents;
    }

    float peak = 0.0f;
    size_t consumed = 0;

    for (int done = 0; done < total; done += s.maxBlock)
    {
        const int n = std::min (s.maxBlock, total - done);

        // Events in [done, done+n): the stored list is time-ordered, so they are contiguous from `consumed`.
        size_t end = consumed;
        while (end < s.numEvents && evTime[end] < done + n)
        {
            s.events[end].header.time = (uint32_t) std::max (0, evTime[end] - done);
            ++end;
        }

        // runChunk reads events[nextIn .. numEvents); window the view to this chunk.
        const auto savedNum = s.numEvents;
        s.nextIn = consumed;
        s.numEvents = end;
        s.runChunk (n);
        s.numEvents = savedNum;
        consumed = end;

        for (int i = 0; i < n; ++i)
        {
            const float l = s.scratchL[(size_t) i], r = s.scratchR[(size_t) i];
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
            if (numCh >= 2)
            {
                buffer.getWritePointer (0)[done + i] = l;
                buffer.getWritePointer (1)[done + i] = r;
            }
            else if (numCh == 1)
                buffer.getWritePointer (0)[done + i] = 0.5f * (l + r);
        }
    }

    lastPeak_.store (peak, std::memory_order_relaxed);
    blocks_.fetch_add (1, std::memory_order_relaxed);
}

bool WclapEngine::paramValue (juce::uint32 id, double& out)
{
    const juce::ScopedLock sl (impl_->lock);
    return loaded_ && impl_->params != nullptr && impl_->params->get_value (impl_->plugin, id, &out);
}

juce::File WclapEngine::bundleDir() const { return impl_->wclapDir; }

WclapEngine::GuiOpenResult WclapEngine::guiOpen (PageSink toPage)
{
    GuiOpenResult r;
    auto& s = *impl_;
    if (! loaded_) { r.detail = "no engine loaded"; return r; }
    if (s.gui == nullptr || s.webview == nullptr) { r.detail = "the guest has no clap.gui / clap.webview"; return r; }
    if (guiOpen_) guiClose();

    // get_uri before anything else: clap.webview says the host must call it before messages flow.
    char uri[1024] = {};
    const auto n = s.webview->get_uri (s.plugin, uri, (uint32_t) sizeof (uri));
    if (n <= 0 || n > (int32_t) sizeof (uri)) { r.detail = "get_uri failed (" + juce::String (n) + ")"; return r; }
    r.uri = juce::String::fromUTF8 (uri);

    if (! s.gui->is_api_supported (s.plugin, CLAP_WINDOW_API_WEBVIEW, false))
    { r.detail = "gui api \"webview\" not supported"; return r; }

    {
        const juce::SpinLock::ScopedLockType l (s.sinkLock);
        s.toPage = std::move (toPage);
    }

    if (! s.gui->create (s.plugin, CLAP_WINDOW_API_WEBVIEW, false))
    {
        const juce::SpinLock::ScopedLockType l (s.sinkLock);
        s.toPage = nullptr;
        r.detail = "gui create(webview) failed " + juce::String (s.lastError());
        return r;
    }

    s.guiCreated = true;
    uint32_t w = 0, h = 0;
    if (s.gui->get_size (s.plugin, &w, &h)) { r.width = w; r.height = h; }

    guiOpen_ = true;
    r.ok = true;
    return r;
}

void WclapEngine::guiClose()
{
    guiOpen_ = false;
    impl_->closeGui();
}

bool WclapEngine::guiReceive (const void* data, size_t size)
{
    auto& s = *impl_;
    if (! guiOpen_ || ! loaded_ || s.webview == nullptr || s.plugin == nullptr || size == 0) return false;
    return s.webview->receive (s.plugin, data, (uint32_t) size);
}

void WclapEngine::guiService()
{
    if (impl_->mainThreadWanted.load()) impl_->runMainThreadCallback();
}
