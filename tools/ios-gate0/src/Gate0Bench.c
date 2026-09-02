// Gate0Bench.c — EXPERIMENT_01 §3.3 on real iOS hardware.
//
// ── The one question this answers ───────────────────────────────────────────
//
// EXPERIMENT_01_DSP_EXECUTION's decision rule turns on a single measurement:
//
//   "If B sustains 8 voices at RTF >= 4x on the slowest decile on iOS hardware
//    -> B everywhere."
//
// Path B is WASM interpreted. iOS forbids JIT, so wasmtime/Cranelift — the
// desktop engine, a median 5.56x faster — cannot ship there. wasm3 can.
//
// §4 is explicit that **an iOS simulator result does not count**: it runs on
// the host CPU and would measure this Mac wearing a costume. Build and run this
// on a device. The simulator is useful only to check the app works.
//
// ── Why this is not the plugin ──────────────────────────────────────────────
//
// An AUv3 target is a host app plus an extension plus provisioning plus an AUv3
// host on the phone, and every bit of it is wasted if the gate fails. The gate
// is about interpreter throughput and needs no audio, no plugin and no host.
//
// It is also a port of probe/gate0/host_wasm3.c rather than of FaustEngine,
// deliberately: the desktop wasm3 numbers were taken with that harness, so this
// produces figures directly comparable to them. Measuring a different thing
// more accurately would be worth less than measuring the same thing.
//
// ── What it reports ─────────────────────────────────────────────────────────
//
//   §3.3  RTF at 1 and 8 voices, per module, against the >= 4x bar
//   §3.5  peak resident memory — on iOS a hard wall, not a slowdown
//   §3.6  cold vs warm, on the first module, because a warm factory cache has
//         moved a load-time number by 25x before now
//
// §4.2's guard applies as ever: peak is reported, and a run whose peak is zero
// measured nothing however fast it was.

// Must precede every wasm3 header. m3_core.c defines this before including
// wasm3.h so that the error-string constants are emitted rather than declared
// extern -- but in a unity build an earlier file has already pulled wasm3.h in,
// its include guard blocks the second inclusion, and the constants are never
// defined. Setting it here, before anything, makes the single inclusion the
// defining one.
#define M3_IMPLEMENT_ERROR_STRINGS

#include "Gate0Data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

// wasm3, compiled as one translation unit. The WASI and tracer sources are
// left out on purpose: uvwasi is an external dependency this does not have and
// does not need.
#include "wasm3/m3_bind.c"
#include "wasm3/m3_code.c"
#include "wasm3/m3_compile.c"
#include "wasm3/m3_core.c"
#include "wasm3/m3_env.c"
#include "wasm3/m3_exec.c"
#include "wasm3/m3_function.c"
#include "wasm3/m3_info.c"
#include "wasm3/m3_module.c"
#include "wasm3/m3_parse.c"
#include "wasm3/m3_validate.c"
#include "wasm3/m3_api_libc.c"

// The embedded modules, included rather than compiled separately so that the
// Xcode target has exactly ONE source file to know about. Everything else --
// wasm3's twelve sources and 1.5 MB of module bytes -- arrives through this
// file, which means no Resources build phase and no project surgery.
#include "Gate0Data.c"

#define BLOCK 128
#define SR 48000
#define MAX_VOICES 8
#define STACK_BYTES (256 * 1024)

// ─────────────────────────────────────────────────────────────────────────────
// The env import table, in DOUBLE.
//
// faustwasm binds these to JavaScript Math.*, which is double and narrows to
// f32 only on return. Calling the float libm computes the whole expression in
// float and lands on a different sample — measured at -50.5 dB on a ten-import
// elite. Wrong here would make every number below describe a different program
// from the one the platform ships.
// ─────────────────────────────────────────────────────────────────────────────

#define UNARY(NAME, EXPR)                                                     \
    static m3ApiRawFunction(NAME) {                                           \
        m3ApiReturnType(float); m3ApiGetArg(float, x);                        \
        m3ApiReturn((float)(EXPR)); }
#define BINARY(NAME, EXPR)                                                    \
    static m3ApiRawFunction(NAME) {                                           \
        m3ApiReturnType(float); m3ApiGetArg(float, x); m3ApiGetArg(float, y); \
        m3ApiReturn((float)(EXPR)); }

UNARY (g0_abs,   fabs((double)x))       UNARY (g0_acos,  acos((double)x))
UNARY (g0_asin,  asin((double)x))       UNARY (g0_atan,  atan((double)x))
UNARY (g0_ceil,  ceil((double)x))       UNARY (g0_cos,   cos((double)x))
UNARY (g0_exp,   exp((double)x))        UNARY (g0_floor, floor((double)x))
UNARY (g0_log,   log((double)x))        UNARY (g0_log10, log10((double)x))
UNARY (g0_round, round((double)x))      UNARY (g0_sin,   sin((double)x))
UNARY (g0_sqrt,  sqrt((double)x))       UNARY (g0_tan,   tan((double)x))
UNARY (g0_acosh, acosh((double)x))      UNARY (g0_asinh, asinh((double)x))
UNARY (g0_atanh, atanh((double)x))      UNARY (g0_cosh,  cosh((double)x))
UNARY (g0_sinh,  sinh((double)x))       UNARY (g0_tanh,  tanh((double)x))
UNARY (g0_isnan, isnan(x) ? 1.0f : 0.0f)
UNARY (g0_isinf, isinf(x) ? 1.0f : 0.0f)

BINARY (g0_atan2, atan2((double)x, (double)y))
BINARY (g0_fmod,  fmod((double)x, (double)y))
BINARY (g0_pow,   pow((double)x, (double)y))
// NOT remainder(): the JS table is x - Math.round(x/y)*y, and Math.round rounds
// halves upward where remainder() rounds half to even.
BINARY (g0_remainder, (double)x - round((double)x / (double)y) * (double)y)
BINARY (g0_max,      (x > y ? x : y))
BINARY (g0_min,      (x < y ? x : y))
BINARY (g0_copysign, copysignf(x, y))

typedef struct { const char *name, *sig; M3RawCall fn; } Link;

// The FULL table, not the measured union. §4.1 found 11 imports across the
// archive, but the set is program-dependent and a name this build lacks is a
// hard failure at load, on a device, for a genome nobody tested.
static const Link kMath[] = {
    {"_abs","f(f)",g0_abs},        {"_acosf","f(f)",g0_acos},
    {"_asinf","f(f)",g0_asin},     {"_atanf","f(f)",g0_atan},
    {"_ceilf","f(f)",g0_ceil},     {"_cosf","f(f)",g0_cos},
    {"_expf","f(f)",g0_exp},       {"_floorf","f(f)",g0_floor},
    {"_logf","f(f)",g0_log},       {"_log10f","f(f)",g0_log10},
    {"_roundf","f(f)",g0_round},   {"_sinf","f(f)",g0_sin},
    {"_sqrtf","f(f)",g0_sqrt},     {"_tanf","f(f)",g0_tan},
    {"_acoshf","f(f)",g0_acosh},   {"_asinhf","f(f)",g0_asinh},
    {"_atanhf","f(f)",g0_atanh},   {"_coshf","f(f)",g0_cosh},
    {"_sinhf","f(f)",g0_sinh},     {"_tanhf","f(f)",g0_tanh},
    {"_isnanf","f(f)",g0_isnan},   {"_isinff","f(f)",g0_isinf},
    {"_atan2f","f(ff)",g0_atan2},  {"_fmodf","f(ff)",g0_fmod},
    {"_powf","f(ff)",g0_pow},      {"_remainderf","f(ff)",g0_remainder},
    {"_max_f","f(ff)",g0_max},     {"_min_f","f(ff)",g0_min},
    {"_copysignf","f(ff)",g0_copysign},
};

typedef struct {
    IM3Runtime rt; IM3Module mod;
    IM3Function fInit, fCompute, fSetParam;
    uint8_t *mem;
    uint32_t dsp, inArr, outArr, inBuf, outBuf;
} Voice;

static double nowSeconds(void) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1.0e9;
}

static size_t peakResidentBytes(void) {
    struct mach_task_basic_info info;
    mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS)
        return 0;
    return (size_t)info.resident_size_max;
}

static void voiceFree(Voice *v) { if (v->rt) m3_FreeRuntime(v->rt); memset(v, 0, sizeof(*v)); }

static const char *voiceLoad(Voice *v, IM3Environment env, const Gate0Module *m) {
    memset(v, 0, sizeof(*v));
    v->rt = m3_NewRuntime(env, STACK_BYTES, NULL);
    if (!v->rt) return "m3_NewRuntime";

    M3Result r = m3_ParseModule(env, &v->mod, m->wasm, (uint32_t)m->wasmLen);
    if (r) return r;
    r = m3_LoadModule(v->rt, v->mod);
    if (r) return r;

    for (size_t i = 0; i < sizeof(kMath)/sizeof(kMath[0]); i++) {
        M3Result e = m3_LinkRawFunction(v->mod, "env", kMath[i].name, kMath[i].sig, kMath[i].fn);
        // Not an error: the module simply does not import that name.
        if (e && e != m3Err_functionLookupFailed) return e;
    }

    if ((r = m3_FindFunctionIn(&v->fInit, v->mod, "init"))) return r;
    if ((r = m3_FindFunctionIn(&v->fCompute, v->mod, "compute"))) return r;
    if ((r = m3_FindFunctionIn(&v->fSetParam, v->mod, "setParamValue"))) return r;

    size_t bytes = 0;
    v->mem = m3_GetMemory(v->mod, &bytes, 0);
    if (!v->mem) return "no memory export";

    // layoutMono(), identical to host_wasm3.c and to
    // FaustMonoWebAudioDsp.initMemory.
    const uint32_t ss = 4, ps = 4;
    v->dsp    = 0;
    v->inArr  = (uint32_t)m->structBytes;
    v->outArr = v->inArr + (uint32_t)m->inputs * ps;
    v->inBuf  = v->outArr + (uint32_t)m->outputs * ps;
    v->outBuf = v->inBuf + (uint32_t)m->inputs * BLOCK * ss;
    if (v->outBuf + (uint32_t)m->outputs * BLOCK * ss > bytes) return "layout exceeds memory";

    uint32_t *h32 = (uint32_t *)v->mem;
    for (int c = 0; c < m->inputs; c++)  h32[(v->inArr >> 2) + c]  = v->inBuf + BLOCK * ss * (uint32_t)c;
    for (int c = 0; c < m->outputs; c++) h32[(v->outArr >> 2) + c] = v->outBuf + BLOCK * ss * (uint32_t)c;

    uint32_t a0 = v->dsp, a1 = SR;
    const void *args[2] = { &a0, &a1 };
    return m3_Call(v->fInit, 2, args);
}

static void setParam(Voice *v, int off, float value) {
    if (off < 0) return;
    uint32_t a0 = v->dsp, a1 = (uint32_t)off;
    const void *args[3] = { &a0, &a1, &value };
    m3_Call(v->fSetParam, 3, args);
}

// Renders `seconds` of audio through `nv` voices and returns wall clock.
// Voices are detuned by a semitone each, as host_wasm3.c does, so eight voices
// are eight different programs' worth of work rather than the same one eight
// times.
static double renderVoices(IM3Environment env, const Gate0Module *m, int nv,
                           double seconds, float *peakOut, const char **err) {
    Voice V[MAX_VOICES];
    memset(V, 0, sizeof(V));

    for (int i = 0; i < nv; i++) {
        const char *e = voiceLoad(&V[i], env, m);
        if (e) { *err = e; for (int j = 0; j <= i; j++) voiceFree(&V[j]); return -1.0; }
        setParam(&V[i], m->idxFreq, 220.0f * powf(2.0f, i / 12.0f));
        setParam(&V[i], m->idxGain, 0.8f);
        setParam(&V[i], m->idxGate, 1.0f);
    }

    const int total = (int)(seconds * SR);
    float peak = 0.0f;
    const double t0 = nowSeconds();

    for (int written = 0; written < total; written += BLOCK) {
        for (int i = 0; i < nv; i++) {
            uint32_t a0 = V[i].dsp, a1 = BLOCK, a2 = V[i].inArr, a3 = V[i].outArr;
            const void *args[4] = { &a0, &a1, &a2, &a3 };
            M3Result r = m3_Call(V[i].fCompute, 4, args);
            if (r) { *err = r; for (int j = 0; j < nv; j++) voiceFree(&V[j]); return -1.0; }

            const uint32_t base = ((uint32_t *)V[i].mem)[V[i].outArr >> 2];
            const float *src = (const float *)(V[i].mem + base);
            for (int s = 0; s < BLOCK; s++) {
                const float a = fabsf(src[s]);
                if (a > peak) peak = a;
            }
        }
    }

    const double elapsed = nowSeconds() - t0;
    for (int i = 0; i < nv; i++) voiceFree(&V[i]);
    *peakOut = peak;
    return elapsed;
}

#define EMIT(...) do { int _n = snprintf(p, (size_t)(end - p), __VA_ARGS__); \
                       if (_n > 0) p += _n; if (p >= end) return; } while (0)

void gate0_run(char *out, size_t outLen) {
    char *p = out, *end = out + outLen;
    *p = 0;

    IM3Environment env = m3_NewEnvironment();
    if (!env) { snprintf(out, outLen, "m3_NewEnvironment failed\n"); return; }

    EMIT("EXPERIMENT_01 §3.3 — path B on this device\n");
    EMIT("wasm3 d012e2e · interpreted · %d modules, the slowest decile\n", gate0ModuleCount);
    EMIT("block %d @ %d Hz · bar: 8 voices at RTF >= 4x\n\n", BLOCK, SR);

    // §3.6. Cold means this process has never touched this module.
    {
        float pk = 0; const char *err = NULL;
        const double cold = renderVoices(env, &gate0Modules[0], 1, 1.0, &pk, &err);
        const double warm = renderVoices(env, &gate0Modules[0], 1, 1.0, &pk, &err);
        if (cold > 0 && warm > 0)
            EMIT("§3.6 cold %.1f ms · warm %.1f ms · ratio %.2fx\n\n",
                 cold * 1e3, warm * 1e3, cold / warm);
    }

    EMIT("%-34s %8s %8s %8s %6s\n", "module", "1 voice", "8 voice", "desktop", "8v");
    int pass = 0, measured = 0;

    for (int i = 0; i < gate0ModuleCount; i++) {
        const Gate0Module *m = &gate0Modules[i];
        float pk1 = 0, pk8 = 0;
        const char *err = NULL;

        const double t1 = renderVoices(env, m, 1, 2.0, &pk1, &err);
        if (t1 <= 0) { EMIT("%-34s  ERROR %s\n", m->name, err ? err : "?"); continue; }

        const double t8 = renderVoices(env, m, 8, 2.0, &pk8, &err);
        if (t8 <= 0) { EMIT("%-34s  ERROR(8v) %s\n", m->name, err ? err : "?"); continue; }

        const double rtf1 = 2.0 / t1, rtf8 = 2.0 / t8;

        // §4.2. A runtime that renders silence is infinitely fast.
        if (pk1 <= 0.0f || pk8 <= 0.0f) {
            EMIT("%-34s  SILENT — void\n", m->name);
            continue;
        }

        measured++;
        if (rtf8 >= 4.0) pass++;
        EMIT("%-34s %7.1fx %7.1fx %7.1fx %6s\n",
             m->name, rtf1, rtf8, m->desktopRtfWasm3, rtf8 >= 4.0 ? "PASS" : "fail");
    }

    EMIT("\n8 voices at >= 4x: %d of %d\n", pass, measured);
    EMIT("§3.5 peak resident: %.1f MB\n", peakResidentBytes() / (1024.0 * 1024.0));
    EMIT("\n%s\n", pass == measured && measured > 0
         ? "Rule 1: B sustains the slowest decile — B everywhere."
         : "Rule 2/3: B does not sustain the slowest decile here.");
    EMIT("A simulator result does not count (§4). Device only.\n");

    m3_FreeEnvironment(env);
}
