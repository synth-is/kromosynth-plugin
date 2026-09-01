// ShellPage — the local UI the plugin serves to itself.
//
// Stage 1. Small on purpose, and the smallness is the design, not a shortcut.
//
// ── Why this exists at all ───────────────────────────────────────────────────
//
// WP_SYNTHIS_PLUGIN.md section 4 originally said "one UI codebase, not three"
// and served everything from the platform. That was superseded on 2026-08-28
// (WP section 12) for two reasons neither of which is a preference:
//
//   1. The web app's service worker handles `push` and `notificationclick`
//      only -- 36 lines, no `fetch` handler. Nothing is cached, so a
//      platform-served UI is BLANK with no network. For a plugin expected to
//      work during a live performance that is disqualifying.
//
//   2. The list of locally downloaded instruments is plugin-side state. The
//      web app cannot render it without the native bridge regardless of
//      caching.
//
// So: this page, served from C++ over the resource provider, is the offline
// surface and the only place `window.__JUCE__.backend` is exposed. The live
// site keeps doing everything discovery-shaped, in a second component with
// native integration OFF.
//
// ── Why it is one hand-written string and not a build step ───────────────────
//
// A bundler, a package.json and a node_modules for a page with one list in it
// would be a second frontend toolchain to maintain for no gain, and it would
// make the plugin's build depend on npm. When this page needs a framework it
// will be obvious, and the honest move then is a real frontend directory --
// not to have pre-emptively built one now and pretended the decision was made
// on evidence.
//
// Kept deliberately dependency-free: no fonts, no CDN, no images. Every
// external reference would be a network request on a surface whose entire
// justification is working without a network.

#pragma once

namespace kromosynth::shell
{
// The page. `%%STATUS%%` is not templated -- everything is fetched through the
// bridge after load, so the HTML is static and can be served from a constant.
inline constexpr const char* kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Synth.is</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 0;
    background: #0d1117; color: #e6edf3;
    font: 14px/1.5 -apple-system, BlinkMacSystemFont, "SF Pro Text", sans-serif;
    height: 100vh; display: flex; flex-direction: column;
  }
  header {
    padding: 14px 18px; border-bottom: 1px solid #21262d;
    display: flex; align-items: baseline; gap: 12px; flex: 0 0 auto;
  }
  header h1 { margin: 0; font-size: 15px; font-weight: 600; letter-spacing: .01em; }
  header .count { color: #7d8590; font-size: 12px; }
  header button {
    margin-left: auto; background: #21262d; color: #e6edf3;
    border: 1px solid #30363d; border-radius: 6px;
    padding: 5px 12px; font-size: 12px; cursor: pointer;
  }
  header button:hover { background: #30363d; }
  main { flex: 1 1 auto; overflow-y: auto; padding: 8px; }
  .row {
    display: flex; align-items: center; gap: 12px;
    padding: 10px 12px; border-radius: 6px; cursor: pointer;
    border: 1px solid transparent;
  }
  .row:hover { background: #161b22; border-color: #21262d; }
  .row.active { background: #16281d; border-color: #2ea04326; }
  .row .name { flex: 1 1 auto; font-variant-numeric: tabular-nums; }
  .row .meta { color: #7d8590; font-size: 12px; }
  .row .dot {
    width: 7px; height: 7px; border-radius: 50%;
    background: #30363d; flex: 0 0 auto;
  }
  .row.active .dot { background: #3fb950; }
  .empty { padding: 40px 18px; color: #7d8590; }
  .empty code {
    display: block; margin-top: 10px; padding: 10px;
    background: #161b22; border-radius: 6px; font-size: 12px;
    color: #a5d6ff; user-select: text; word-break: break-all;
  }
  footer {
    flex: 0 0 auto; padding: 9px 18px; border-top: 1px solid #21262d;
    color: #7d8590; font-size: 12px; min-height: 34px;
  }
  footer.warn { color: #d29922; }
  footer.err { color: #f85149; }
</style>
</head>
<body>
<header>
  <h1>Instruments</h1>
  <span class="count" id="count"></span>
  <button id="browse">Browse synth.is</button>
  <button id="refresh">Refresh</button>
  <button id="panic">Panic</button>
</header>
<main id="list"></main>
<footer id="status">Loading&hellip;</footer>
<footer id="voices"></footer>

<script>
// The bridge. JUCE injects window.__JUCE__.backend when native integration is
// enabled; if it is missing we are being viewed somewhere it is not, and
// saying so plainly beats a page that silently does nothing.
const backend = window.__JUCE__ && window.__JUCE__.backend;

// The invoke protocol, implemented against JUCE's own frontend source rather
// than guessed. The first version of this called backend.callNativeFunction(),
// which does not exist -- the shell rendered, the bridge was present, and every
// call threw "callNativeFunction is not a function".
//
// The real contract (juce_gui_extra/native/typescript/webview-interop/src/
// index.ts:81-99) is an event pair:
//
//   emit  '__juce__invoke'   { name, params, resultId }
//   listen '__juce__complete' { promiseId, result }
//
// JUCE ships a helper module that wraps this, but importing it would mean a
// bundler and a node_modules for one function. Reimplemented here, in about
// fifteen lines, with the source reference above so the next person can check
// it against upstream rather than trusting this comment.
let nextPromiseId = 0;
const pending = new Map();

if (backend) {
  backend.addEventListener('__juce__complete', function (payload) {
    const entry = pending.get(payload.promiseId);
    if (entry) {
      entry(payload.result);
      pending.delete(payload.promiseId);
    }
  });
}

function setStatus (text, kind) {
  const el = document.getElementById('status');
  el.textContent = text;
  el.className = kind || '';
}

function call (name, ...args) {
  if (!backend) return Promise.reject(new Error('native bridge unavailable'));

  return new Promise(function (resolve) {
    const id = nextPromiseId++;
    pending.set(id, resolve);
    backend.emitEvent('__juce__invoke', {
      name: name, params: args, resultId: id
    });
  });
}

let entries = [];
let currentId = null;

function render () {
  const list = document.getElementById('list');
  document.getElementById('count').textContent =
    entries.length === 1 ? '1 instrument' : entries.length + ' instruments';

  if (entries.length === 0) {
    list.innerHTML =
      '<div class="empty">No instruments in the local library yet.' +
      '<code id="root"></code></div>';
    call('getLibraryRoot').then(function (p) {
      const el = document.getElementById('root');
      if (el) el.textContent = p;
    }).catch(function () {});
    return;
  }

  list.innerHTML = '';

  entries.forEach(function (e) {
    const row = document.createElement('div');
    row.className = 'row' + (e.id === currentId ? ' active' : '');

    const dot = document.createElement('span');
    dot.className = 'dot';

    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = e.name;

    const meta = document.createElement('span');
    meta.className = 'meta';
    // A Faust instrument has no regions to count -- it is synthesised, not
    // sampled -- so the column says which engine will play it instead of
    // sitting empty and looking like a failed load.
    meta.textContent = e.kind === 'faust'
      ? 'live \u00b7 Faust'
      : (e.regions > 0 ? e.regions + ' regions' : '');

    row.appendChild(dot);
    row.appendChild(name);
    row.appendChild(meta);
    row.addEventListener('click', function () { load(e.id); });
    list.appendChild(row);
  });
}

async function load (id) {
  setStatus('Loading\u2026');
  try {
    const r = await call('loadInstrument', id);

    // ok and regions are reported separately on purpose. F11: sfizz's
    // loadSfzFile returns true when every region was culled for an
    // unresolvable sample, so "it loaded" and "there is an instrument" are
    // different claims. A UI that collapses them would show a green tick on a
    // silent instrument.
    if (r.ok) {
      currentId = id;
      setStatus('Loaded \u00b7 ' + r.regions + ' regions \u00b7 '
                + r.tailSeconds.toFixed(1) + ' s tail');
    } else {
      setStatus(r.detail || 'Load failed', 'err');
    }
    render();
  } catch (err) {
    setStatus(String(err), 'err');
  }
}

async function refresh () {
  if (!backend) {
    setStatus('Native bridge unavailable \u2014 this page is not being served '
              + 'by the plugin.', 'err');
    return;
  }
  try {
    entries = await call('listInstruments');
    const cur = await call('getStatus');
    currentId = cur && cur.ok ? cur.id : null;
    render();
    setStatus(currentId
      ? 'Loaded \u00b7 ' + cur.regions + ' regions \u00b7 '
        + cur.tailSeconds.toFixed(1) + ' s tail'
      : 'Nothing loaded yet \u2014 pick an instrument.');
  } catch (err) {
    setStatus(String(err), 'err');
  }
}

document.getElementById('refresh').addEventListener('click', refresh);

// A DAW's panic is not guaranteed to reach a plugin and GarageBand shows none.
// 16 of 33 measured archive genomes ignore their own gate, so without this the
// only way to stop a stuck drone is to delete the track.
document.getElementById('panic').addEventListener('click', async () => {
  await call('panic');
  setStatus('Panicked \u2014 all voices reinitialised.');
});

// Polled only while a Faust instrument is loaded. notesOn far ahead of
// notesOff means the host is not delivering note-offs, which is a completely
// different problem from the engine holding a voice open, and the two are
// indistinguishable by ear.
setInterval(async () => {
  try {
    const st = await call('engineState');
    if (!st || !st.faust) return;
    const stuck = st.notesOn - st.notesOff;
    document.getElementById('voices').textContent =
      st.activeVoices + ' voices \u00b7 on ' + st.notesOn + ' / off ' + st.notesOff
      + (st.rawPeak > 0 ? ' \u00b7 trim ' + st.trimDb.toFixed(1) + ' dB (raw peak '
                          + st.rawPeak.toFixed(3) + ')' : '')
      + (stuck > 0 ? ' \u00b7 ' + stuck + ' UNRELEASED' : '')
      + (st.forcedStops > 0 ? ' \u00b7 ' + st.forcedStops + ' force-stopped' : '');
  } catch (e) { /* editor closing */ }
}, 500);

// The only way into the site surface. The site cannot put the user back here --
// it has no bridge -- so the return trip is the plugin's own strip, drawn in
// C++ above the site component.
document.getElementById('browse').addEventListener('click', function () {
  call('openSite').catch(function (e) { setStatus(String(e), 'err'); });
});

// Downloads are polled, not pushed. They run on a pool thread owned by the
// processor and outlive this editor, so there is nothing for them to push to.
// One second is well inside human patience and nowhere near often enough to
// matter for a call that reads a vector under a lock.
//
// This poll will usually MISS the running state entirely. A download starts
// while the user is on the site, so the shell is hidden, and WKWebView throttles
// timers in a hidden view. The shell typically wakes up to find the job already
// finished. That is normal, and the reason the refresh below is unconditional:
// an earlier version only refreshed if it had seen the job while active, which
// meant the list silently failed to update and the user had to press Refresh.
//
// Refreshing on every finished batch is safe because this is the only clearer.
// The SiteBar deliberately does not clear, so each batch is processed once.

async function pollAcquisitions () {
  if (!backend) return;
  try {
    const jobs = await call('getAcquisitions');
    if (!jobs || !jobs.length) return;

    const active = jobs.filter(function (j) { return !j.finished; });

    if (active.length) {
      const j = active[0];
      const pct = j.progress >= 0 ? ' ' + Math.round(j.progress * 100) + '%' : '';
      setStatus('Downloading ' + j.id + pct);
      return;
    }

    // Everything finished. Report the outcomes once, then clear them so the
    // list does not grow for the life of the session.
    const failed = jobs.filter(function (j) { return !j.ok; });
    setStatus(failed.length ? failed[0].message
                            : 'Downloaded ' + jobs.length + ' instrument(s).',
              failed.length ? 'err' : undefined);
    await call('clearFinishedAcquisitions');

    refresh();
  } catch (e) { /* a poll that fails is not worth a visible error */ }
}

setInterval(pollAcquisitions, 1000);
refresh();
</script>
</body>
</html>
)HTML";
} // namespace kromosynth::shell
