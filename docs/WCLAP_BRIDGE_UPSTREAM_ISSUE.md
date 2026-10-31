# Draft issue for github.com/WebCLAP/wclap-bridge (synthis-aa4.51)

Not yet filed: the run that wrote this had no GitHub access and posting
publicly needs the owner's say-so. File it as one issue, then put the URL in
`docs/PLUGIN_AS_HOST_SPIKE.md` (findings 4 and 5).

**Title:** Uninitialised `hostTemplate` makes `create_plugin` fail at random; null funcref in `wtCall` aborts Wasmtime

Found against cd11d22 with the Surge XT WCLAP (juce-webclap-plugins), Wasmtime 39.0.1 and 48.0.1.

## 1. `hostTemplate` is uninitialised

`source/_generic/wclap-module-base.h:127` declares `wclap_host hostTemplate;` and only the
function pointers are ever set. `clap_version`, `name`, `vendor`, `url` and `version` hold
whatever the heap had. The JUCE WCLAP wrapper's `create_plugin` returns null unless
`clap_version_is_compatible(host->clap_version)`, so most runs return a null plugin with
nothing logged (3 of 12 runs reached audio).

Fix: `wclap_host hostTemplate{};` and, in the `WclapModule` constructor
(`source/_generic/wclap-module.h`):

```cpp
hostTemplate.clap_version = {CLAP_VERSION_MAJOR, CLAP_VERSION_MINOR, CLAP_VERSION_REVISION};
```

(ideally also set name, vendor, url, version). With it: 12 of 12 creations on both Wasmtime versions.

## 2. `wtCall` calls a null funcref

In `wclap-instance-wasmtime.h`, `InstanceImpl::wtCall` calls the funcref returned by
`wasmtime_table_get` without checking it. Calling e.g. `Plugin::state_load` before
`get_extension()` has filled the extension pointer calls table slot 0 (null funcref, `fnP == 0`),
and Wasmtime panics with "object used with the wrong store" (`store/data.rs:196`) and aborts.
Suggest: if `fnP == 0`, record an error and return instead of calling.

## 3. Build against Wasmtime 48.0.1

`wasi_config_preopen_dir` now takes a bool, not the `WASMTIME_WASI_*_PERMS_*` constants, so the
bridge does not compile as is.

## 4. webview-gui: a relative `get_uri` 404s the page (read-only)

From `docs/WCLAP_ENGINE_UI_2026-10-27.md` "Why not the bridge's own webview" (synthis-et4.16).
Read from the bridge source at the pinned revision `cd11d22`, NOT run: **read-only**.

`get_uri` returns the relative `/ui/index.html`; the bridge then serves resources by asking the
guest's `get_resource`, and juce_webclap's answer is `false` ("hosts serve the bundle"). Only a
`file:` URI makes the bridge serve from disk. So the bundle's page would 404.

Source: `source/_generic/wclap-plugin.h` — `webview_get_uri` at :1132 (the `file:` check at :1143
sets `wasFileUri`, :1152 clears it) and `webview_get_resource` at :1155 (serves from disk only
when `wasFileUri`, :1156; otherwise forwards to the guest's `get_resource` at :1209).

## 5. webview-gui: `WebviewGui::send` dispatches without `source` (read-only)

Same provenance as item 4: **read-only**.

`WebviewGui::send` builds `new MessageEvent('message', {data})` with no `source`, and
`webclap-ui.js` drops every message whose `source !== window.parent`. Every engine-to-page
frame (snapshot, parameter updates) would be lost.

Source: `modules/webview-gui/include/webview-gui/_impl/platform/apple-osx.h:310-315`
(`WebviewGui::send`; the `MessageEvent` construction is at :311). The page side is open-webclap's
`webclap-ui.js`.

## 6. webview-gui: the `Uint8Array.fromBase64` polyfill over-allocates (read-only)

Same provenance as item 4: **read-only**.

Its `Uint8Array.fromBase64` polyfill allocates `b64.length` bytes, not the decoded length (trailing
zeros on every frame).

Source: `modules/webview-gui/include/webview-gui/_impl/platform/apple-osx.h:190-199`
(`new Uint8Array(b64.length)` at :193).
