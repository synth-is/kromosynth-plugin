# Gate 0 on iOS — path B, the one measurement the decision turns on

`EXPERIMENT_01_DSP_EXECUTION`'s rule:

> If B sustains 8 voices at RTF >= 4x on the slowest decile on iOS hardware
> -> B everywhere.

Path B is WASM interpreted. iOS forbids JIT, so wasmtime/Cranelift — the
desktop engine, a median 5.56x faster — cannot ship there. wasm3 can.

## This is not an AUv3

An AUv3 target is a host app plus an extension plus provisioning plus an AUv3
host on the phone, and all of it is wasted if the gate fails. The gate is about
interpreter throughput: no audio, no plugin, no host.

`src/Gate0Bench.c` is a port of `probe/gate0/host_wasm3.c`, deliberately rather
than of `FaustEngine`, so the numbers are directly comparable to the desktop
wasm3 figures already recorded.

## Already answered on desktop, and not encouragingly

Run on an M1 Pro — considerably faster than any iPhone:

```
module                              1 voice  8 voice  desktop     8v
elite_08_deep_pluck_c23-12            12.5x     1.6x     6.1x   fail
elite_02_deep_pluck_c24-29             6.5x     0.8x     6.7x   fail
...
8 voices at >= 4x: 0 of 12
§3.5 peak resident: 34.6 MB
```

**0 of 12.** One genome is below real time at 8 voices on a desktop CPU. An A15
is slower and an A11 much slower, so the device number can only be worse. The
decision is rule 2 or 3, not rule 1.

The device measurement is still worth taking, because §4 requires it on the
record and because §3.5's memory figure is iOS-specific — a hard wall on AUv3
rather than a slowdown.

## Building it

Everything arrives through ONE source file. `Gate0Bench.c` unity-includes
wasm3's twelve sources and `Gate0Data.c` (the twelve slowest-decile modules as
byte arrays), so there is no Resources build phase and nothing to drag in.

1. Xcode → File → New → Project → iOS → App, SwiftUI, name it `Gate0Bench`,
   save it into this directory.
2. Set **iOS Deployment Target to 15.0** — an iPhone 8 caps at 16.7 and Xcode 27
   still offers 15.0. Verified: `DEPLOYMENT_TARGET_SUGGESTED_VALUES` starts at
   15.0.
3. Drag `src/Gate0Bench.c` into the target. Say yes to the bridging header;
   put `void gate0_run(char *out, unsigned long outLen);` in it.
4. Header Search Paths → `$(PROJECT_DIR)/..` and `$(PROJECT_DIR)/../src`.
5. Replace `ContentView.swift` with `src/ContentView.swift.txt`.
6. **Build Release, not Debug.** A Debug build measures the optimiser being off.
7. Run on the device. **A simulator result does not count** (§4) — it runs on
   the host CPU and would measure the Mac wearing a costume.

## Desktop reference

```
cc -O3 -std=c11 -I . -I src src/Gate0Bench.c main.c -lm -o gate0
```

where `main.c` calls `gate0_run` into a 64 KB buffer and prints it.
