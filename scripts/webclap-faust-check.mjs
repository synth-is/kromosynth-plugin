// SPDX-License-Identifier: AGPL-3.0-or-later
// synthis-et4.17: load the Faust spike WCLAP in kromosynth-burst's wclap-host-node and
//   null    replay the artefact's plan (one voice, 128-frame blocks) and compare it sample for sample with the
//           platform references beside the artefact: <stem>.wasmtime-archive.f32 (the native null-test baseline)
//           and <stem>.wasm3-archive.f32
//   note    play one MIDI note through FaustEngine's voice path; time it
//   voices  hold N notes at once, 48 kHz / 512, N = 1..8, three fresh instances each; report the real-time factor
//   measure FaustEngine::measure() run inside the module (prints its single-voice RTF and peaks)
// Driven by scripts/webclap-faust.sh. NOT part of the gate.
//
//   node scripts/webclap-faust-check.mjs <host src dir> <bundle dir> <artefact stem> [null|note|voices|measure|all] [--out f.wav]
import fs from 'node:fs';

const [hostSrc, bundle, stem, mode = 'all', ...rest] = process.argv.slice(2);
const outWav = rest[0] === '--out' ? rest[1] : null;
const { WclapHost, loadBundle, fingerprint } = await import(`${hostSrc}/index.mjs`);
const { encodeWav } = await import(`${hostSrc}/wav.mjs`);
const wasm = loadBundle(bundle).wasm;

async function plugin({ sampleRate = 48000, blockSize, clock = 'virtual' }) {
	const host = await WclapHost.load(wasm, { clock });
	const p = host.createPlugin();
	p.activate({ sampleRate, blockSize });
	return { host, p };
}

function compare(ours, ref) {
	const n = Math.min(ours.length, ref.length);
	let maxAbs = 0, sumSq = 0, refSq = 0, first = -1, peakO = 0, peakR = 0, differing = 0;
	for (let i = 0; i < n; ++i) {
		const d = ours[i] - ref[i];
		if (d !== 0) { differing++; if (first < 0) first = i; }
		maxAbs = Math.max(maxAbs, Math.abs(d));
		sumSq += d * d; refSq += ref[i] * ref[i];
		peakO = Math.max(peakO, Math.abs(ours[i])); peakR = Math.max(peakR, Math.abs(ref[i]));
	}
	const dB = sumSq === 0 ? -Infinity : 10 * Math.log10(sumSq / refSq);
	return { n, maxAbs, dB, first, differing, peakO, peakR };
}

if (mode === 'null' || mode === 'all') {
	const { host, p } = await plugin({ blockSize: 128 });
	const frames = 10 * 48000;
	const t0 = performance.now();
	const [l] = p.render(frames, [
		{ frame: 0, type: 'param', id: 'Replay plan', value: 1 },
		{ frame: 0, type: 'noteOn', key: 57, velocity: 0.8, noteId: 1 },
	]);
	const ms = performance.now() - t0;
	console.log(host.stdout.trim());
	console.log(`null: rendered ${frames} frames (10 s) in ${ms.toFixed(0)} ms, ${(10 / (ms / 1000)).toFixed(2)}x real time, one voice, block 128`);
	for (const ext of ['wasmtime-archive.f32', 'wasm3-archive.f32']) {
		const f = `${stem}.${ext}`;
		if (!fs.existsSync(f)) { console.log(`null: no ${ext}`); continue; }
		const b = fs.readFileSync(f);
		const ref = new Float32Array(b.buffer, b.byteOffset, b.byteLength / 4);
		const c = compare(l, ref);
		console.log(`null vs ${ext}: compared ${c.n}, peak ours ${c.peakO.toFixed(6)} ref ${c.peakR.toFixed(6)}, ` +
			`maxAbsDiff ${c.maxAbs.toExponential(6)}, residual ${c.dB === -Infinity ? '-inf' : c.dB.toFixed(1)} dB, ` +
			`${c.differing} samples differ${c.first >= 0 ? `, first at ${c.first}` : ''}` +
			`${c.maxAbs === 0 ? '  IDENTICAL' : ''}`);
	}
	if (outWav) fs.writeFileSync(outWav.replace(/\.wav$/, '-replay.wav'), encodeWav([l], 48000));
}

if (mode === 'note' || mode === 'all') {
	const { host, p } = await plugin({ blockSize: 512 });
	const sr = 48000;
	const t0 = performance.now();
	const out = p.renderNote({ key: 60, velocity: 0.8, noteFrames: Math.round(1.5 * sr), tailFrames: Math.round(0.5 * sr) });
	const ms = performance.now() - t0;
	let peak = 0, sq = 0;
	for (const x of out[0]) { peak = Math.max(peak, Math.abs(x)); sq += x * x; }
	console.log(host.stdout.trim());
	console.log(`note: key 60, 1.5 s held + 0.5 s tail, ${out[0].length} frames in ${ms.toFixed(0)} ms ` +
		`(${(out[0].length / sr / (ms / 1000)).toFixed(2)}x real time, 8 voices allocated, 1 sounding), ` +
		`peak ${peak.toFixed(4)}, RMS ${Math.sqrt(sq / out[0].length).toFixed(4)}, fingerprint ${fingerprint(out)}`);
	if (outWav) fs.writeFileSync(outWav, encodeWav(out, sr));
	if (!(peak > 0.01)) { console.error('FAIL: the note is silent'); process.exitCode = 1; }
}

if (mode === 'measure') {
	// FaustEngine::measure() inside the module, at the native probe's conditions (48 kHz, 128-frame blocks). The real
	// WASI clock: under the host's default virtual clock no time passes inside one process() call, and rtf reads 0.
	for (let i = 0; i < 2; ++i) {
		const { host, p } = await plugin({ blockSize: 128, clock: 'real' });
		p.render(128, [{ frame: 0, type: 'param', id: 'Measure', value: 1 }]);
		console.log(host.stdout.trim().split('\n').filter(l => l.startsWith('measure')).join('\n'));
	}
}

if (mode === 'voices') {
	// N notes held at once, steady state (after a warm-up block), 1 s timed per run, 3 fresh module instances per N:
	// the per-voice cost is stable inside one instance but differs between instances (see the spike note).
	const sr = 48000, block = 512, runs = 3;
	for (const n of [1, 2, 3, 4, 6, 8]) {
		const ms = [];
		let peak = 0;
		for (let r = 0; r < runs; ++r) {
			const { p } = await plugin({ blockSize: block });
			p.render(block, [{ frame: 0, type: 'param', id: 'Voices', value: n }]);
			p.render(block, Array.from({ length: n }, (_, i) => ({ frame: 0, type: 'noteOn', key: 48 + i * 2, velocity: 0.8, noteId: i + 1 })));
			const t0 = performance.now();
			const [l] = p.render(sr, []);
			ms.push(performance.now() - t0);
			for (const x of l) peak = Math.max(peak, Math.abs(x));
		}
		ms.sort((a, b) => a - b);
		console.log(`voices ${String(n).padStart(2)}: ${ms.map(x => x.toFixed(0)).join('/')} ms per second of audio ` +
			`(RTF ${(1000 / ms[ms.length - 1]).toFixed(2)}..${(1000 / ms[0]).toFixed(2)}), ` +
			`${(ms[0] / n).toFixed(0)}..${(ms[ms.length - 1] / n).toFixed(0)} ms per voice, peak ${peak.toFixed(3)}`);
	}
}
