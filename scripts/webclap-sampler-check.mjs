// SPDX-License-Identifier: AGPL-3.0-or-later
// synthis-et4.18: load the sampler spike WCLAP in kromosynth-burst's wclap-host-node and
//   check   render keys 69 (the sample's own pitch), 81 (an octave up) and 57 (an octave down) and compare each with
//           the source WAV (tone.wav, made by scripts/webclap-sampler-data.mjs):
//             key 69: the output must be the sample times one gain, sample for sample (least-squares gain, best lag in
//                     -64..64; residual energy relative to the output's must be below -60 dB). Fitted in two parts,
//                     the first host block (512 frames) and the rest, each with its own lag: sfizz plays the source
//                     sample at the end of a voice's first block once more at the start of the next (lag 0, then
//                     lag 1). Native sfizz does the same (scripts/webclap-sampler-native.cpp), so it is sfizz's
//                     behaviour, not the module's; the two gains must agree to 1e-4;
//             key 81: the output must be every second sample (step exactly 2.0, so no interpolation between samples);
//                     same fit, bar -60 dB;
//             all three: fundamental from the autocorrelation peak over the first 0.25 s must be 440 Hz times 2^((key
//                     - 69)/12) within 0.5 %, and the output not silent (peak > 0.05); L and R identical (mono sample,
//                     centre pan).
//           Key 57 (step 0.5) is checked for pitch only: its samples fall between the source's, so they depend on
//           sfizz's interpolator; its residual against linear interpolation is printed, not judged.
//           If <out dir>/native-key<k>.f32 exist (scripts/webclap-sampler.sh native), each key is also compared with
//           native sfizz's render of the same SFZ: residual after a least-squares gain must be below -55 dB.
//   voices  N notes held at once (keys 69, 68, ..., so each lasts at least the sample's 1 s), N = 1..64 in one
//           plugin instance (sfizz's default polyphony is 64), then 2..8 instances of 64 in one module and thread;
//           48 kHz, block 512, 0.5 s timed after a warm-up block, three fresh modules per row. Linear memory size.
//           Fails if any instance does not report all its notes as active sfizz voices.
// Driven by scripts/webclap-sampler.sh. NOT part of the gate.
//
//   node scripts/webclap-sampler-check.mjs <host src dir> <bundle dir> <tone.wav> [check|voices] [<out dir>]
import fs from 'node:fs';

const [hostSrc, bundle, wavPath, mode = 'check', outDir] = process.argv.slice(2);
const { WclapHost, loadBundle, fingerprint } = await import(`${hostSrc}/index.mjs`);
const { encodeWav } = await import(`${hostSrc}/wav.mjs`);
const wasm = loadBundle(bundle).wasm;
const sr = 48000;

function readWav16Mono(p) {
	const b = fs.readFileSync(p);
	if (b.toString('ascii', 0, 4) !== 'RIFF' || b.readUInt16LE(22) !== 1 || b.readUInt16LE(34) !== 16) throw new Error('not 16-bit mono');
	const n = b.readUInt32LE(40) / 2, s = new Float32Array(n);
	for (let i = 0; i < n; ++i) s[i] = b.readInt16LE(44 + i * 2) / 32768;
	return s;
}

async function instance(n = 1) {
	const host = await WclapHost.load(wasm);
	const ps = [];
	for (let i = 0; i < n; ++i) { const p = host.createPlugin(); p.activate({ sampleRate: sr, blockSize: 512 }); ps.push(p); }
	return { host, ps };
}

// Least-squares gain of ref against y at the best lag; residual energy relative to y's, in dB. y[i] is compared with
// ref[refStart + i - lag] (zero outside ref).
function fit(y, ref, maxLag = 64, refStart = 0) {
	let best = null;
	for (let lag = -maxLag; lag <= maxLag; ++lag) {
		let yr = 0, rr = 0, yy = 0;
		for (let i = 0; i < y.length; ++i) {
			const j = refStart + i - lag, r = j >= 0 && j < ref.length ? ref[j] : 0;
			yr += y[i] * r; rr += r * r; yy += y[i] * y[i];
		}
		const g = rr > 0 ? yr / rr : 0, res = yy - g * yr; // ||y - g r||^2 = yy - 2g yr + g^2 rr = yy - g yr
		if (!best || res < best.res) best = { lag, g, res: Math.max(res, 0), yy };
	}
	let maxAbs = 0;
	for (let i = 0; i < y.length; ++i) {
		const j = refStart + i - best.lag, r = j >= 0 && j < ref.length ? ref[j] : 0;
		maxAbs = Math.max(maxAbs, Math.abs(y[i] - best.g * r));
	}
	return { ...best, dB: best.res === 0 ? -Infinity : 10 * Math.log10(best.res / best.yy), maxAbs };
}

// Fundamental: the first autocorrelation peak above 0.5 of zero lag, refined parabolically.
function f0(y, from = 0, len = Math.round(0.25 * sr)) {
	const x = y.subarray(from, from + len), ac = k => { let s = 0; for (let i = 0; i + k < x.length; ++i) s += x[i] * x[i + k]; return s; };
	const a0 = ac(0), minLag = Math.floor(sr / 2000), maxLag = Math.ceil(sr / 100);
	const v = []; for (let k = 0; k <= maxLag + 1; ++k) v.push(k >= minLag ? ac(k) : 0);
	for (let k = minLag + 1; k <= maxLag; ++k)
		if (v[k] > 0.5 * a0 && v[k] >= v[k - 1] && v[k] >= v[k + 1]) {
			const d = (v[k - 1] - v[k + 1]) / (2 * (v[k - 1] - 2 * v[k] + v[k + 1]));
			return sr / (k + d);
		}
	return NaN;
}

if (mode === 'check') {
	const src = readWav16Mono(wavPath);
	let fail = false;
	for (const key of [69, 81, 57]) {
		const { host, ps: [p] } = await instance();
		const step = 2 ** ((key - 69) / 12), frames = Math.ceil(src.length / step) + 4800;
		const t0 = performance.now();
		const [l, r] = p.renderNote({ key, velocity: 0.8, noteFrames: frames, tailFrames: 0 });
		const ms = performance.now() - t0;
		if (key === 69) console.log(host.stdout.trim());
		let peak = 0, lrDiff = 0;
		for (let i = 0; i < l.length; ++i) { peak = Math.max(peak, Math.abs(l[i])); lrDiff = Math.max(lrDiff, Math.abs(l[i] - r[i])); }
		// The reference at this pitch: the source read at step `step`, linearly interpolated (exact for steps 1 and 2).
		const ref = new Float32Array(Math.floor((src.length - 1) / step) + 1);
		for (let i = 0; i < ref.length; ++i) { const x = i * step, j = Math.floor(x), f = x - j; ref[i] = src[j] * (1 - f) + (f ? f * src[j + 1] : 0); }
		// The first block and the rest, fitted separately (see the header).
		const a = fit(l.subarray(0, 512), ref), b = fit(l.subarray(512), ref, 64, 512);
		const hz = f0(l), want = 440 * step, hzErr = Math.abs(hz / want - 1);
		const judged = key !== 57;
		const fitOk = a.dB < -60 && b.dB < -60 && Math.abs(a.g - b.g) < 1e-4;
		let nat = null;
		const natPath = outDir && `${outDir}/native-key${key}.f32`;
		if (natPath && fs.existsSync(natPath)) {
			const nb = fs.readFileSync(natPath);
			nat = fit(l, new Float32Array(nb.buffer, nb.byteOffset, nb.byteLength / 4), 4);
		}
		const ok = peak > 0.05 && hzErr < 0.005 && lrDiff === 0 && (!judged || fitOk) && (!nat || nat.dB < -55);
		fail ||= !ok;
		const fmt = c => `gain ${c.g.toFixed(6)}, lag ${c.lag}, residual ${c.dB === -Infinity ? '-inf' : c.dB.toFixed(1)} dB, max |diff| ${c.maxAbs.toExponential(2)}`;
		console.log(`key ${key}: ${l.length} frames in ${ms.toFixed(0)} ms (${(l.length / sr / (ms / 1000)).toFixed(0)}x real time), ` +
			`peak ${peak.toFixed(4)}, L-R max ${lrDiff}, f0 ${hz.toFixed(2)} Hz (want ${want.toFixed(2)}, ${(hzErr * 100).toFixed(3)} %)\n` +
			`  vs source${step === 1 ? '' : ` read at step ${step}`}${judged ? '' : ' (not judged)'}: first block ${fmt(a)}; rest ${fmt(b)}\n` +
			(nat ? `  vs native sfizz: ${fmt(nat)}\n` : '  vs native sfizz: no native render (scripts/webclap-sampler.sh native)\n') +
			`  fingerprint ${fingerprint([l, r])}  ${ok ? 'OK' : 'FAIL'}`);
		if (outDir) fs.writeFileSync(`${outDir}/key${key}.wav`, encodeWav([l, r], sr));
	}
	if (fail) { console.error('FAIL'); process.exitCode = 1; } else console.log('check: ok');
}

if (mode === 'voices') {
	const block = 512, timed = sr / 2, runs = 3;
	const rows = [...[1, 2, 4, 8, 16, 32, 64].map(v => [1, v]), ...[2, 4, 8, 12, 16].map(k => [k, 64])];
	for (const [plugins, perPlugin] of rows) {
		const ms = [];
		let peak = 0, mem = 0, mem0 = 0;
		for (let run = 0; run < runs; ++run) {
			const { host, ps } = await instance(plugins);
			mem0 = host.memory.buffer.byteLength;
			for (const p of ps) p.render(block, Array.from({ length: perPlugin }, (_, i) => ({ frame: 0, type: 'noteOn', key: 69 - i, velocity: 0.8, noteId: i + 1 })));
			// Every instance must report all its notes sounding (the module prints "voices: N" when the count rises).
			const counts = host.stdout.split('\n').filter(x => x.startsWith('voices: ')).map(x => +x.slice(8));
			if (counts.filter(c => c === perPlugin).length !== plugins) { console.error(`FAIL: voices reported ${counts}`); process.exitCode = 1; }
			const t0 = performance.now();
			for (let f = 0; f < timed; f += block) for (const p of ps) { const [l] = p.render(block, []); for (const x of l) peak = Math.max(peak, Math.abs(x)); }
			ms.push((performance.now() - t0) * (sr / timed));
			mem = host.memory.buffer.byteLength;
		}
		ms.sort((a, b) => a - b);
		const n = plugins * perPlugin;
		console.log(`voices ${String(n).padStart(4)} (${plugins} x ${perPlugin}): ${ms.map(x => x.toFixed(1)).join('/')} ms per second of audio ` +
			`(RTF ${(1000 / ms[ms.length - 1]).toFixed(1)}..${(1000 / ms[0]).toFixed(1)}), ${(ms[0] / n * 1000).toFixed(0)}..${(ms[ms.length - 1] / n * 1000).toFixed(0)} us per voice-second, ` +
			`peak ${peak.toFixed(2)}, linear memory ${(mem0 / 1048576).toFixed(1)} -> ${(mem / 1048576).toFixed(1)} MB`);
	}
}
