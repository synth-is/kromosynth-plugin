#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// synthis-aa4.95. Render the same genomes (Init patch + N seeded random) through a reference bundle and candidate
// bundles with kromosynth-burst's adapters (which implement the repeatable-render recipe: resetDeterminism /
// snapshot-restore, fresh plugin per render, two notes) and report fingerprint equality or the residual in dB.
//
//   BURST=~/Developer/apps/synth.is/kromosynth-burst node compare.mjs surge|obxf --ref <bundle> [--n 6] <candidate>...
//
// Residual dB = 20*log10(max|a-b| / max|a|) over both channels of both notes (-Infinity = bit-identical). The
// pre-registered rule accepts bit-identical, or <= -90 dB across engines. Read-only on every bundle.
import os from 'node:os';
const BURST = (process.env.BURST || `${os.homedir()}/Developer/apps/synth.is/kromosynth-burst`).replace(/^~/, os.homedir());
const kind = process.argv[2];
const args = process.argv.slice(3);
const take = (k, d) => { const i = args.indexOf(`--${k}`); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
const ref = take('ref'); const N = +take('n', 6); const out = take('out');
const cands = args;
const { createSurgeEngine } = await import(`${BURST}/src/surge-adapter.mjs`);
const { createObxfEngine } = await import(`${BURST}/src/obxf-adapter.mjs`);
const { fingerprint } = await import(`${BURST}/packages/wclap-host-node/src/index.mjs`);
const { mulberry32 } = await import(`${BURST}/tools/surge/rng.mjs`);
const make = kind === 'obxf' ? createObxfEngine : createSurgeEngine;

async function renders(bundle) {
  const t0 = performance.now();
  const e = await make({ bundle });
  const rng = mulberry32(11);
  const genomes = [e.seed(), ...Array.from({ length: N }, () => e.random(rng))];
  const r = genomes.map((g) => e.render(g));
  return { e, genomes, r, ms: performance.now() - t0 };
}
const fp = (r) => fingerprint([r.left, r.right]);
function resid(a, b) {
  let mx = 0, pk = 0;
  for (const ch of ['left', 'right']) for (let i = 0; i < a[ch].length; i++) { mx = Math.max(mx, Math.abs(a[ch][i] - b[ch][i])); pk = Math.max(pk, Math.abs(a[ch][i])); }
  return { maxDiff: mx, peak: pk, db: mx === 0 ? -Infinity : 20 * Math.log10(mx / (pk || 1)) };
}
const R = await renders(ref);
const report = { kind, ref, params: R.e.catalogue.length, genomes: R.genomes.length, refFingerprints: R.r.map(fp), candidates: [] };
console.error(`ref ${ref}: ${report.params} genes, peaks ${R.r.map((x) => x.left.reduce((m, v) => Math.max(m, Math.abs(v)), 0).toFixed(3)).join(' ')}`);
for (const c of cands) {
  const C = await renders(c);
  const rows = C.r.map((x, i) => ({ fp: fp(x), identical: fp(x) === report.refFingerprints[i], ...resid(R.r[i], x) }));
  const worst = Math.max(...rows.map((x) => x.db));
  const cand = { bundle: c, genes: C.e.catalogue.length, sameCatalogue: JSON.stringify(C.e.catalogue.map((x) => [x.id, x.name])) === JSON.stringify(R.e.catalogue.map((x) => [x.id, x.name])),
    allIdentical: rows.every((x) => x.identical), worstDb: worst, rows };
  report.candidates.push(cand);
  console.error(`cand ${c}\n  genes=${cand.genes} sameCatalogue=${cand.sameCatalogue} allIdentical=${cand.allIdentical} worst=${worst} dB\n  ` + rows.map((x) => (x.identical ? '=' : x.db.toFixed(1))).join(' '));
}
console.log(JSON.stringify(report, (k, v) => (v === -Infinity ? '-Infinity' : v), 1));
if (out) (await import('node:fs')).writeFileSync(out, JSON.stringify(report, (k, v) => (v === -Infinity ? '-Infinity' : v), 1));
