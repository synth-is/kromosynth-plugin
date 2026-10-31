// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// synthis-aa4.90: headless check of a Six Sines WCLAP through kromosynth-burst packages/wclap-host-node.
// BURST_HOST overrides the host path. See docs/OPEN_WEBCLAP_SIX_SINES_2026-10-20.md.
// usage: node six-verify.mjs <bundle> [mode]   mode: fresh | snapshot | warm
const { WclapHost, loadBundle, fingerprint } = await import(process.env.BURST_HOST || (process.env.HOME + '/Developer/apps/synth.is/kromosynth-burst/packages/wclap-host-node/src/index.mjs'));
const [bundle, mode = 'fresh'] = process.argv.slice(2);
const host = await WclapHost.load(loadBundle(bundle).wasm);
if (mode === 'snapshot') host.snapshot();
const out = { mode, plugins: host.plugins };
function mk() {
  if (mode === 'snapshot') host.restore(); else if (mode === 'fresh') host.resetDeterminism();
  const p = host.createPlugin(); p.activate({ sampleRate: 48000, blockSize: 512 }); return p;
}
let p = mk();
out.paramCount = p.params.length;
out.paramSample = p.params.slice(0, 4).map(x => `${x.id}:${x.name}[${x.min},${x.max}]def=${x.def}`);
const PRIME = process.env.PRIME !== '0';
const render = pl => { if (PRIME) pl.render(512, []); return pl.renderNote({ key: 60, velocity: 0.8, noteFrames: 48000, tailFrames: 24000 }); };
const peak = ch => Math.max(...ch.map(c => c.reduce((m, v) => Math.max(m, Math.abs(v)), 0)));
const base = render(p); out.peak = peak(base); out.baseFp = fingerprint(base);
const st = p.saveState?.(); out.stateBytes = st ? st.length : null;
// state round trip: change a param, save, load in a fresh plugin, render == render with same state
const fps = [];
for (let i = 0; i < 5; i++) { if (mode !== 'warm' || i === 0) p = mk(); fps.push(fingerprint(render(p))); if (mode !== 'warm') p.destroy(); }
out.fps5 = fps; out.identical5 = new Set(fps).size === 1;
// round trip: param event -> audible change -> saveState -> fresh plugin loadState -> same audio + getParam
const cand = p => p.params.filter(x => /^(Main Level|Main Pan|Main Tuning|Main Attack Shape)/i.test(x.name));
p = mk(); const names = cand(p).map(x => x.name); out.candidates = names; p.destroy();
const results = [];
for (const name of ['Main Level', 'Main Pan', 'Main Attack Shape']) {
  p = mk(); const mp = p.param(name); const target = mp.def === mp.min ? (mp.min + mp.max) / 2 : (mp.def === mp.max ? mp.min : mp.max);
  const withParam = render2(p, mp, target); const fa = fingerprint(withParam);
  const sv = p.saveState(); const got = p.getParam(name); p.destroy();
  p = mk(); p.loadState(sv); const afterLoad = p.getParam(name); const fb = fingerprint(render(p)); const sv2 = p.saveState(); p.destroy();
  p = mk(); const fdef = fingerprint(render(p)); p.destroy();
  results.push({ name, def: mp.def, target, changesAudio: fa !== fdef, getParamAfterEvent: got, getParamAfterLoad: afterLoad, loadedRendersLikeEvent: fb === fa, loadedRendersLikeDefault: fb === fdef, stateBytes: sv.length, resaveIdentical: Buffer.compare(Buffer.from(sv), Buffer.from(sv2)) === 0 });
}
out.roundTrip = results;
out.roundTripAllOk = results.every(r => r.loadedRendersLikeEvent && r.resaveIdentical && r.changesAudio);
console.log(JSON.stringify(out));
function render2(pl, mp, v) { if (PRIME) pl.render(512, [{ frame: 0, type: 'param', id: mp.id, value: v }]); return pl.render(72000, [{ frame: 0, type: 'noteOn', key: 60, velocity: 0.8, noteId: 1 }, { frame: 48000, type: 'noteOff', key: 60, velocity: 0, noteId: 1 }]); }
