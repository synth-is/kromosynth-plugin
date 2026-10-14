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
