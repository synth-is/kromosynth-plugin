#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// wclap-merged-host.mjs — synthis-aa4.42: a minimal single-threaded CLAP host in Node 22 that drives the
// GUEST half of a wasm-merge'd Surge XT WCLAP (docs/WCLAP_GUEST_HOSTING_ROUTE.md). NOT part of the gate
// (needs the artefacts below). Struct offsets are wasm32, ported from juce_webclap's test-host/clap-host-worklet.js.
//
//   node scripts/wclap-merged-host.mjs [merged.wasm]     (default: builds one in $WORK, /tmp/aa442)
//   WCLAP_SRC  Surge XT .wclap dir; WASM_MERGE  binaryen's wasm-merge
//
// Exits 0 only if: the guest plays a note with RMS > 0, a saved state restores (a parameter changed after
// the save returns to its saved value and a re-saved state is byte-identical), and the host half is untouched.
import fs from "node:fs"
import {execFileSync} from "node:child_process"
import os from "node:os"
import path from "node:path"

const WORK = process.env.WORK || "/tmp/aa442"
const SRC = process.env.WCLAP_SRC || path.join(os.homedir(), "wclap-native/juce-webclap-plugins/build/surge-xt/web/surge-xt.wclap")
const MERGE = process.env.WASM_MERGE || path.join(os.homedir(), "wclap-native/emsdk/upstream/bin/wasm-merge")
const SR = 48000, Q = 128, SLOT = 48, SCRATCH = 1 << 20
let merged = process.argv[2]
if (!merged) {
  fs.mkdirSync(WORK, {recursive: true})
  merged = path.join(WORK, "merged.wasm")
  const m = path.join(SRC, "module.wasm")
  execFileSync(MERGE, [m, "host", m, "guest", "-o", merged, "--all-features", "--rename-export-conflicts"], {stdio: "inherit"})
}

// ---- instantiate. The two modules share one WASI import namespace, so a shim cannot tell whose pointer it
// gets: `active` names the memory of whichever half we are calling into (always the guest after init).
const mod = await WebAssembly.compile(fs.readFileSync(merged))
let active = null
const view = () => new DataView(active.buffer)
const wasi = {
  args_get: () => 0, environ_get: () => 0,
  args_sizes_get: (a, b) => (view().setUint32(a, 0, true), view().setUint32(b, 0, true), 0),
  environ_sizes_get: (a, b) => (view().setUint32(a, 0, true), view().setUint32(b, 0, true), 0),
  clock_time_get: (_i, _p, out) => (view().setBigUint64(out, BigInt(Date.now()) * 1000000n, true), 0),
  fd_write: (_fd, iovs, n, written) => {
    let total = 0
    for (let i = 0; i < n; i++) total += view().getUint32(iovs + i * 8 + 4, true)
    view().setUint32(written, total, true); return 0
  },
  fd_fdstat_get: () => 8,
  proc_exit: code => { throw new Error(`plugin exited with ${code}`) },
  random_get: (ptr, len) => { new Uint8Array(active.buffer, ptr, len).forEach((_, i, a) => { a[i] = Math.random() * 256 }); return 0 },
  sched_yield: () => 0,
}
const imports = {wasi_snapshot_preview1: {}}
for (const e of WebAssembly.Module.imports(mod)) imports[e.module][e.name] = wasi[e.name] || (() => 52)
const ex = (await WebAssembly.instantiate(mod, imports)).exports
const G = {memory: ex.memory_11, table: ex.__indirect_function_table_12, malloc: ex.malloc_14, entry: ex.clap_entry_15}
for (const k in G) if (!G[k]) throw new Error(`merged module lacks guest export ${k}`)
const H = {memory: ex.memory, entry: ex.clap_entry}
if (G.memory === H.memory) throw new Error("guest and host share a memory: not a two-memory merge")
active = H.memory; ex._initialize()          // host half initialises on its own memory...
const hostBefore = Buffer.from(H.memory.buffer).subarray(0, 1 << 16).toString("base64")
active = G.memory; ex._initialize_17()       // ...guest on its own

// ---- helpers over the guest
const mem = G.memory
const dv = () => new DataView(mem.buffer)
const u8 = () => new Uint8Array(mem.buffer)
const u32 = p => dv().getUint32(p, true)
const fn = p => G.table.get(u32(p))
const cstr = p => { const b = u8(); let e = p; while (b[e]) e++; return Buffer.from(b.subarray(p, e)).toString() }
const str = s => { const b = Buffer.from(s + "\0"), p = G.malloc(b.length); u8().set(b, p); return p }
const VT = {i32: 0x7f, i64: 0x7e}
function trampoline(params, results, f) {
  const sec = (id, b) => [id, b.length, ...b], nm = s => [s.length, ...Buffer.from(s)]
  const bytes = new Uint8Array([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0,
    ...sec(1, [1, 0x60, params.length, ...params.map(t => VT[t]), results.length, ...results.map(t => VT[t])]),
    ...sec(2, [1, ...nm("e"), ...nm("f"), 0, 0]), ...sec(7, [1, ...nm("f"), 0, 0])])
  return new WebAssembly.Instance(new WebAssembly.Module(bytes), {e: {f}}).exports.f
}
const install = (ptr, params, results, f) => {
  const i = G.table.grow(1); G.table.set(i, trampoline(params, results, f)); dv().setUint32(ptr, i, true)
}

// ---- clap_entry -> plugin factory -> plugin
const entry = G.entry.value
if (!fn(entry + 12)(str("/"))) throw new Error("clap_entry.init failed")
const factory = fn(entry + 20)(str("clap.plugin-factory"))
if (!factory) throw new Error("no plugin factory")
const desc = fn(factory + 4)(factory, 0)
const id = cstr(u32(desc + 12)), name = cstr(u32(desc + 16))
console.log(`guest plugin: ${name} (${id})`)

const host = G.malloc(48), exts = new Map()
const ext = (n, size) => { const p = G.malloc(size); exts.set(n, p); return p }
const hs = ["wclap-merged-host", "synthis", "", "0.1"].map(str)
{ const v = dv(); v.setUint32(host, 1, true); v.setUint32(host + 4, 2, true); v.setUint32(host + 8, 0, true)
  v.setUint32(host + 12, 0, true); hs.forEach((p, i) => v.setUint32(host + 16 + i * 4, p, true)) }
install(host + 32, ["i32", "i32"], ["i32"], (_h, p) => exts.get(cstr(p)) || 0)
install(host + 36, ["i32"], [], () => {}); install(host + 40, ["i32"], [], () => {}); install(host + 44, ["i32"], [], () => {})
const hp = ext("clap.params", 12)
install(hp, ["i32", "i32"], [], () => {}); install(hp + 4, ["i32", "i32", "i32"], [], () => {}); install(hp + 8, ["i32"], [], () => {})
install(ext("clap.state", 4), ["i32"], [], () => {})
const hl = ext("clap.log", 4); install(hl, ["i32", "i32", "i32"], [], () => {})
const tc = ext("clap.thread-check", 8); install(tc, ["i32"], ["i32"], () => 1); install(tc + 4, ["i32"], ["i32"], () => 0)

const plugin = fn(factory + 8)(factory, host, str(id))
if (!plugin) throw new Error("create_plugin failed")
if (!fn(plugin + 8)(plugin)) throw new Error("plugin.init failed")
const pext = n => fn(plugin + 40)(plugin, str(n))
const params = pext("clap.params"), state = pext("clap.state")
if (!params || !state) throw new Error("guest lacks clap.params or clap.state")

// process data (see createProcess in the worklet)
const proc = G.malloc(40), outs = [G.malloc(Q * 4), G.malloc(Q * 4)], ins = [G.malloc(Q * 4), G.malloc(Q * 4)]
const outData = G.malloc(8), inData = G.malloc(8), audioOut = G.malloc(24), audioIn = G.malloc(24)
const slots = G.malloc(SLOT * 16), inEv = G.malloc(12), outEv = G.malloc(8)
const scratch = G.malloc(SCRATCH), infoPtr = G.malloc(1320), valPtr = G.malloc(8), textPtr = G.malloc(256)
let queue = []
{ const v = dv()
  for (const [b, d, ch] of [[audioOut, outData, outs], [audioIn, inData, ins]]) {
    v.setUint32(d, ch[0], true); v.setUint32(d + 4, ch[1], true); v.setUint32(b, d, true)
    v.setUint32(b + 4, 0, true); v.setUint32(b + 8, 2, true); v.setUint32(b + 12, 0, true); v.setBigUint64(b + 16, 0n, true)
  }
  v.setBigUint64(proc, 0n, true); v.setUint32(proc + 12, 0, true); v.setUint32(proc + 16, audioIn, true)
  v.setUint32(proc + 20, audioOut, true); v.setUint32(proc + 32, inEv, true); v.setUint32(proc + 36, outEv, true) }
install(inEv + 4, ["i32"], ["i32"], () => queue.length)
install(inEv + 8, ["i32", "i32"], ["i32"], (_l, i) => i < queue.length ? slots + i * SLOT : 0)
install(outEv + 4, ["i32", "i32"], ["i32"], () => 1)
const ostream = G.malloc(8), istream = G.malloc(8)
let chunks = [], src = null
install(ostream + 4, ["i32", "i32", "i64"], ["i64"], (_s, p, n) => { chunks.push(u8().slice(p, p + Number(n))); return n })
install(istream + 4, ["i32", "i32", "i64"], ["i64"], (_s, p, n) => {
  const k = Math.min(Number(n), src.b.length - src.c); u8().set(src.b.subarray(src.c, src.c + k), p); src.c += k; return BigInt(k)
})

if (!fn(plugin + 16)(plugin, SR, 1, Q)) throw new Error("plugin.activate failed")
fn(plugin + 24)(plugin)

let steady = 0n
function block(events = []) {
  queue = events
  const v = dv()
  events.forEach((e, i) => {
    const p = slots + i * SLOT
    u8().fill(0, p, p + SLOT)
    v.setUint16(p + 8, 0, true); v.setUint16(p + 10, e.type, true)
    if (e.type === 5) { // param value
      v.setUint32(p, 48, true); v.setUint32(p + 16, e.id, true); v.setInt32(p + 24, -1, true)
      v.setInt16(p + 28, -1, true); v.setInt16(p + 30, -1, true); v.setInt16(p + 32, -1, true); v.setFloat64(p + 40, e.value, true)
    } else { // note on/off
      v.setUint32(p, 40, true); v.setInt32(p + 16, -1, true)
      v.setInt16(p + 20, 0, true); v.setInt16(p + 22, 0, true); v.setInt16(p + 24, e.key, true); v.setFloat64(p + 32, e.vel, true)
    }
  })
  v.setBigUint64(proc, steady, true); v.setUint32(proc + 8, Q, true); v.setUint32(proc + 24, 0, true); v.setUint32(proc + 28, 1, true)
  fn(plugin + 36)(plugin, proc)
  steady += BigInt(Q); queue = []
  return [...new Float32Array(mem.buffer, outs[0], Q), ...new Float32Array(mem.buffer, outs[1], Q)]
}
function render(blocks, events0 = []) {
  let sum = 0, n = 0, peak = 0
  for (let b = 0; b < blocks; b++) for (const s of block(b ? [] : events0)) { sum += s * s; n++; peak = Math.max(peak, Math.abs(s)) }
  return {rms: Math.sqrt(sum / n), peak}
}
const saveState = () => {
  chunks = []; if (!fn(state)(plugin, ostream)) throw new Error("state.save failed")
  return Buffer.concat(chunks)
}
const loadState = b => { src = {b: new Uint8Array(b), c: 0}; if (!fn(state + 4)(plugin, istream)) throw new Error("state.load failed"); src = null }
const getParam = pid => { fn(params + 8)(plugin, pid, valPtr); return dv().getFloat64(valPtr, true) }

// pick a continuous, modifiable parameter to perturb (flag bits: STEPPED 1, HIDDEN 4, READONLY 8)
const count = fn(params)(plugin)
let target = null
for (let i = 0; i < count && !target; i++) {
  if (!fn(params + 4)(plugin, i, infoPtr)) continue
  const v = dv(), pid = v.getUint32(infoPtr, true), flags = v.getUint32(infoPtr + 4, true)
  const min = v.getFloat64(infoPtr + 1296, true), max = v.getFloat64(infoPtr + 1304, true)
  if (!(flags & (1 | 4 | 8)) && max > min) target = {pid, name: cstr(infoPtr + 12), min, max}
}
if (!target) throw new Error("no continuous parameter found")
console.log(`${count} params; perturbing #${target.pid} "${target.name}" [${target.min}, ${target.max}]`)

// ---- 1. silence, then a note
const silent = render(20)
const NOTE_ON = 0, NOTE_OFF = 1, PARAM = 5
const played = render(150, [{type: NOTE_ON, key: 60, vel: 1}])
console.log(`before note RMS ${silent.rms.toExponential(3)}; note RMS ${played.rms.toFixed(5)} peak ${played.peak.toFixed(4)}`)
render(10, [{type: NOTE_OFF, key: 60, vel: 0}])

// ---- 2. state: save, change a parameter, check it moved, load, check it came back
const base = getParam(target.pid)
const saved = saveState()
const changed = base + (base - target.min > target.max - base ? target.min - base : target.max - base) * 0.75
render(2, [{type: PARAM, id: target.pid, value: changed}])
const moved = getParam(target.pid)
loadState(saved)
const restored = getParam(target.pid)
const resaved = saveState()
console.log(`param ${base} -> ${moved} -> (load state) ${restored}; state ${saved.length} bytes, resave identical: ${Buffer.compare(saved, resaved) === 0}`)
const after = render(150, [{type: NOTE_ON, key: 64, vel: 1}])
console.log(`note after restore RMS ${after.rms.toFixed(5)}`)

active = H.memory
const hostAfter = Buffer.from(H.memory.buffer).subarray(0, 1 << 16).toString("base64")
const checks = {
  "silent before note": silent.rms < 1e-6,
  "RMS > 0 on note": played.rms > 1e-4 && Number.isFinite(played.rms),
  "param moved after save": Math.abs(moved - base) > 1e-9,
  "state restored the parameter": Math.abs(restored - base) < 1e-6,
  "re-saved state identical": Buffer.compare(saved, resaved) === 0,
  "still sounds after load": after.rms > 1e-4,
  "host memory untouched by guest": hostBefore === hostAfter,
}
for (const [k, ok] of Object.entries(checks)) console.log(`${ok ? "PASS" : "FAIL"}  ${k}`)
process.exit(Object.values(checks).every(Boolean) ? 0 : 1)
