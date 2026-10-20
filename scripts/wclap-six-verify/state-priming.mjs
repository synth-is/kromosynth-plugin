// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// synthis-aa4.90: headless check of a Six Sines WCLAP through kromosynth-burst packages/wclap-host-node.
// BURST_HOST overrides the host path. See docs/OPEN_WEBCLAP_SIX_SINES_2026-10-20.md.
const { WclapHost, loadBundle, fingerprint } = await import(process.env.BURST_HOST || (process.env.HOME + '/Developer/apps/synth.is/kromosynth-burst/packages/wclap-host-node/src/index.mjs'));
const host = await WclapHost.load(loadBundle(process.argv[2]).wasm);
const mk=()=>{host.resetDeterminism();const p=host.createPlugin();p.activate({sampleRate:48000,blockSize:512});return p;};
const pk=o=>Math.max(...o.map(c=>c.reduce((m,v)=>Math.max(m,Math.abs(v)),0)));
const R=(p,evs=[])=>p.render(72000,[...evs,{frame:0,type:'noteOn',key:60,velocity:.8,noteId:1},{frame:48000,type:'noteOff',key:60,velocity:0,noteId:1}]);
let p=mk(); const id=p.param('Main Pan').id;
let a=R(p,[{frame:0,type:'param',id,value:1}]); console.log('event',fingerprint(a),pk(a),a.map(c=>c.reduce((s,v)=>s+Math.abs(v),0)));
const sv=p.saveState(); p.destroy();
p=mk(); let d=R(p); console.log('default',fingerprint(d),pk(d),d.map(c=>c.reduce((s,v)=>s+Math.abs(v),0))); p.destroy();
p=mk(); p.loadState(sv); let b=R(p); console.log('loaded',fingerprint(b),pk(b),b.map(c=>c.reduce((s,v)=>s+Math.abs(v),0)));p.destroy();
p=mk(); p.loadState(sv); p.render(512,[]); let c=R(p); console.log('loaded+warmup',fingerprint(c),pk(c));p.destroy();
p=mk(); p.loadState(sv); let e=R(p); console.log('loaded again',fingerprint(e));p.destroy();
console.log('--- with 1-block warmup both paths');
p=mk(); p.render(512,[{frame:0,type:'param',id,value:1}]); const w1=R(p); console.log('event+warm',fingerprint(w1),pk(w1)); const sv2=p.saveState(); p.destroy();
p=mk(); p.render(512,[]); const w0=R(p); console.log('default+warm',fingerprint(w0),pk(w0)); p.destroy();
p=mk(); p.loadState(sv2); p.render(512,[]); const w2=R(p); console.log('loaded+warm',fingerprint(w2),pk(w2)); p.destroy();
p=mk(); p.loadState(sv2); p.render(512,[]); const w3=R(p); console.log('loaded+warm again',fingerprint(w3)); p.destroy();
