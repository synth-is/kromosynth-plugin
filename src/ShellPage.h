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
  main { flex: 1 1 auto; overflow-y: auto; padding: 8px; position: relative; }
  /* The spectrogram sits BEHIND the list, not beside it. It is context for the
     loaded instrument, not a control, and anything that took vertical space
     away from the list would be paying for decoration with the thing the page
     exists to show. */
  #spectro {
    position: absolute; inset: 0; width: 100%; height: 100%;
    z-index: 0; pointer-events: none; opacity: .34;
    transition: opacity .4s ease;
    /* Explicit rather than relying on the default. A 192x128 image stretched
       across the whole panel needs interpolation, and a browser that chose
       nearest-neighbour would draw visible blocks. */
    image-rendering: auto;
    /* Fades into the footer instead of ending on a hard edge, which read as a
       misplaced image rather than as backdrop. */
    -webkit-mask-image: linear-gradient(to bottom, rgba(0,0,0,1) 55%, rgba(0,0,0,0) 100%);
    mask-image: linear-gradient(to bottom, rgba(0,0,0,1) 55%, rgba(0,0,0,0) 100%);
  }
  #spectro.empty { opacity: 0; }
  #list { position: relative; z-index: 1; }
  .toolbar {
    flex: 0 0 auto; display: flex; gap: 8px; align-items: center;
    padding: 8px 18px; border-bottom: 1px solid #21262d;
  }
  .toolbar input, .toolbar select {
    background: #0d1117; color: #e6edf3; border: 1px solid #30363d;
    border-radius: 6px; padding: 4px 8px; font-size: 12px;
    font-family: inherit;
  }
  .toolbar input { flex: 1 1 auto; min-width: 0; }
  .row {
    padding: 9px 12px; border-radius: 6px; cursor: pointer;
    border: 1px solid transparent;
  }
  .row:hover { background: #161b22; border-color: #21262d; }
  .row.active { background: #16281d; border-color: #2ea04326; }
  .row .top { display: flex; align-items: center; gap: 12px; }
  .row .name {
    flex: 1 1 auto; font-variant-numeric: tabular-nums;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .row .meta { color: #7d8590; font-size: 12px; flex: 0 0 auto; }
  .row .sub {
    margin: 2px 0 0 19px; color: #6e7681; font-size: 11.5px;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .row .sub .sep { opacity: .4; padding: 0 5px; }
  .row .sub .warn { color: #d29922; }
  .row.fresh { border-color: #2ea04355; }
  .row .new {
    flex: 0 0 auto; font-size: 10px; text-transform: uppercase;
    letter-spacing: .04em; color: #3fb950;
    border: 1px solid #2ea04355; border-radius: 4px; padding: 1px 5px;
  }
  .row .treebtn {
    flex: 0 0 auto; font: inherit; font-size: 11px; cursor: pointer;
    background: #21262d; color: #7d8590;
    border: 1px solid #30363d; border-radius: 4px; padding: 1px 7px;
  }
  .row .treebtn:hover { background: #30363d; color: #e6edf3; }

  /* The list goes quiet while an instrument loads. Loading BLOCKS the message
     thread for one to three seconds -- measure() renders 105 s of audio to
     level the keyboard -- so further clicks would queue up behind it and
     arrive as a burst afterwards. */
  #list.busy { opacity: .55; pointer-events: none; }
  #list.busy .row.pending { opacity: 1; }

  /* CSS animation, deliberately. WKWebView runs the page in its own process,
     so this keeps turning while our message thread is blocked -- which is the
     whole point: the plugin is busy, not hung, and a static "Loading..." is
     indistinguishable from a freeze. */
  .spinner {
    display: inline-block; width: 11px; height: 11px; flex: 0 0 auto;
    border: 2px solid #30363d; border-top-color: #58a6ff; border-radius: 50%;
    animation: spin .7s linear infinite; vertical-align: -1px;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .row .rmbtn {
    flex: 0 0 auto; font: inherit; font-size: 11px; cursor: pointer;
    background: transparent; color: #6e7681;
    border: 1px solid transparent; border-radius: 4px; padding: 1px 7px;
  }
  .row:hover .rmbtn { border-color: #30363d; }
  .row .rmbtn:hover { background: #3d1d1d; border-color: #6e2f2f; color: #f85149; }
  .row .confirm { flex: 0 0 auto; font-size: 11px; color: #f0883e; }
  .row .confirm button {
    font: inherit; font-size: 11px; cursor: pointer; margin-left: 6px;
    border-radius: 4px; padding: 1px 7px;
  }
  .row .confirm .yes {
    background: #6e2f2f; color: #ffdcd7; border: 1px solid #8e3b3b;
  }
  .row .confirm .no {
    background: transparent; color: #7d8590; border: 1px solid #30363d;
  }
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
<div class="toolbar">
  <input id="filter" type="search" autocomplete="off"
         placeholder="Filter by name, type or character&hellip;">
  <select id="sort">
    <option value="added">Recently added</option>
    <option value="discovered">Recently discovered</option>
    <option value="adopted">Recently kept</option>
    <option value="name">Name</option>
    <option value="type">Type</option>
    <option value="bright">Brightest first</option>
  </select>
</div>
<main id="listwrap">
  <canvas id="spectro" class="empty"></canvas>
  <div id="list"></div>
</main>
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
let sortBy = 'added';
let filterText = '';

// Ids that appeared in the list since the last poll, and the one to scroll to.
//
// A sound acquired from the site lands wherever the CURRENT sort puts it, which
// is rarely the top: sorted by type or by name it can arrive in the middle of
// twenty entries with nothing to distinguish it, and the user who just added it
// has to hunt. Changing the sort for them would be worse -- it throws away the
// order they chose.
//
// So the new arrival is marked and scrolled to instead. That works under every
// ordering, including whatever was restored from a previous session.
let knownIds = null;      // null until the first successful list, so a cold
                          // start does not mark all 21 instruments as new
let newIds = new Set();
let scrollToId = null;

// ── ordering and description ────────────────────────────────────────────────
//
// listInstruments hands over synthis.json's own shape -- source, character,
// measured -- so everything below is a read. Before this the list was whatever
// order the filesystem returned scan()'s directories in, which is why it looked
// random.
//
// THE HARD PART IS ABSENCE. Instruments that predate the artefact contract,
// ones placed by hand, and renders of unadopted feed candidates carry almost
// none of this; and of the Faust-substrate sounds in the archive only 73 of 231
// have descriptors at all. So every field here is optional and unset sorts
// LAST under every ordering -- a missing discovery date is not the epoch, and
// must not lead a list sorted by discovery.

const DESCENDING = { added: 1, discovered: 1, adopted: 1, bright: 1 };

function timeOf (iso) {
  const t = iso ? Date.parse(iso) : NaN;
  return isNaN(t) ? null : t;
}

function sortKey (e) {
  const src = e.source || {}, chr = e.character || {};

  switch (sortBy) {
    case 'name': return (e.name || '').toLowerCase();
    case 'discovered': return timeOf(src.discoveredAt);
    case 'adopted': return timeOf(src.adoptedAt);
    case 'type': return chr.soundType || null;
    // Ranked against THIS LIBRARY rather than against a corpus. The labels are
    // P33/P67 across the whole archive and shift when it is relabelled; the raw
    // vector does not, and "the brightest of the forty I have" is the question
    // actually being asked here. Index 0 is brightness in DESCRIPTOR_DIMENSIONS
    // order.
    case 'bright': {
      const d = chr.descriptors;
      return (d && d.length === 7) ? d[0] : null;
    }
    default: return timeOf(src.acquiredAt);
  }
}

function compareEntries (a, b) {
  const ka = sortKey(a), kb = sortKey(b);
  const missingA = (ka === null || ka === undefined);
  const missingB = (kb === null || kb === undefined);

  if (missingA && missingB) return (a.name || '').localeCompare(b.name || '');
  if (missingA) return 1;
  if (missingB) return -1;
  if (ka === kb) return (a.name || '').localeCompare(b.name || '');

  const ascending = ka < kb ? -1 : 1;
  return DESCENDING[sortBy] ? -ascending : ascending;
}

// Everything worth typing at the box, including the labels -- which are not
// shown on the row (seven of them would not fit) but are exactly what someone
// means when they type "bright" or "noisy".
function searchText (e) {
  const chr = e.character || {}, src = e.source || {}, msd = e.measured || {};
  const labels = chr.labels ? Object.keys(chr.labels).map(function (k) {
    return chr.labels[k];
  }) : [];

  return [e.name, e.kind, chr.soundType, msd.articulation, src.class]
    .concat(labels).filter(Boolean).join(' ').toLowerCase();
}

function ago (iso) {
  const t = timeOf(iso);
  if (t === null) return null;

  const days = Math.floor((Date.now() - t) / 86400000);
  if (days <= 0) return 'today';
  if (days === 1) return 'yesterday';
  if (days < 30) return days + 'd ago';
  if (days < 365) return Math.round(days / 30) + 'mo ago';
  return Math.round(days / 365) + 'y ago';
}

// The date shown follows the SORT, so the column being ordered by is the one
// that can be read. Sorting by discovery and showing the acquisition date would
// make the order look arbitrary all over again.
function datePart (e) {
  const src = e.source || {};

  if (sortBy === 'discovered') { const a = ago(src.discoveredAt); return a && 'found ' + a; }
  if (sortBy === 'adopted')    { const a = ago(src.adoptedAt);    return a && 'kept ' + a; }

  const a = ago(src.acquiredAt);
  return a && 'added ' + a;
}

function describe (e) {
  const chr = e.character || {}, msd = e.measured || {};
  const bits = [];

  if (chr.soundType) bits.push({ text: chr.soundType });
  if (chr.labels && chr.labels.brightness) bits.push({ text: chr.labels.brightness });

  // A drone is a fine pad and a poor MIDI instrument, and it is worth knowing
  // before playing one rather than after.
  if (msd.articulation) {
    bits.push(msd.articulation === 'drone'
      ? { text: 'drone \u2014 sustains after note-off', warn: true }
      : { text: msd.articulation });
  }

  // Only when the artefact says so. hasGate is a tri-state here: true, false,
  // and "the artefact never said", and the third must not read as the second.
  if (msd.hasGate === false) bits.push({ text: 'no gate', warn: true });

  if (msd.capAt4x > 0) bits.push({ text: msd.capAt4x + ' voices' });

  const when = datePart(e);
  if (when) bits.push({ text: when });

  return bits;
}

function render () {
  const list = document.getElementById('list');

  if (entries.length === 0) {
    document.getElementById('count').textContent = '0 instruments';
    list.innerHTML =
      '<div class="empty">No instruments in the local library yet.' +
      '<code id="root"></code></div>';
    call('getLibraryRoot').then(function (p) {
      const el = document.getElementById('root');
      if (el) el.textContent = p;
    }).catch(function () {});
    return;
  }

  const needle = filterText.trim().toLowerCase();
  const matched = needle
    ? entries.filter(function (e) { return searchText(e).indexOf(needle) >= 0; })
    : entries.slice();

  // New arrivals are PINNED ABOVE the chosen ordering rather than sorted into
  // it. Sorted by type or by name a just-acquired instrument can land in the
  // middle of twenty with nothing to distinguish it, and a badge alone still
  // leaves the user scanning for it. Pinning puts it where they look; the rest
  // of the list keeps exactly the order they chose, which is why this is a
  // partition and not a change of sort.
  const fresh = matched.filter(function (e) { return newIds.has(e.id); }).sort(compareEntries);
  const rest = matched.filter(function (e) { return !newIds.has(e.id); }).sort(compareEntries);
  const shown = fresh.concat(rest);

  document.getElementById('count').textContent =
    (needle ? shown.length + ' of ' + entries.length : entries.length)
    + (entries.length === 1 && !needle ? ' instrument' : ' instruments');

  list.innerHTML = '';

  if (shown.length === 0) {
    // Escapes rather than literal curly quotes. Everything user-visible in this
    // page is ASCII plus entities or \u escapes: a literal multi-byte character
    // in the source came back as mojibake in the rendered placeholder, because
    // the page is served from a C++ string over the resource provider and the
    // decoding is not ours to assume.
    list.innerHTML = '<div class="empty">Nothing matches \u201c'
      + needle.replace(/&/g, '&amp;').replace(/</g, '&lt;') + '\u201d.</div>';
    return;
  }

  shown.forEach(function (e) {
    const row = document.createElement('div');
    const fresh = newIds.has(e.id);
    row.className = 'row' + (e.id === currentId ? ' active' : '') + (fresh ? ' fresh' : '');
    row.dataset.id = e.id;

    const top = document.createElement('div');
    top.className = 'top';

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

    top.appendChild(dot);
    top.appendChild(name);

    if (fresh) {
      const badge = document.createElement('span');
      badge.className = 'new';
      badge.textContent = 'new';
      top.appendChild(badge);
    }

    top.appendChild(meta);

    // Where this sound sits in its family tree. Only when the artefact knows
    // both ids -- the tree view is addressed by run, and highlighting needs the
    // sound -- so it is absent for instruments that predate the sidecar rather
    // than present and broken.
    const src = e.source || {};

    if (src.soundId && src.evolutionRunId) {
      const tree = document.createElement('button');
      tree.className = 'treebtn';
      tree.textContent = 'tree';
      tree.title = 'See this sound\u2019s relatives on synth.is';
      tree.addEventListener('click', function (ev) {
        // Or the row's own handler would also fire and load the instrument,
        // which is not what a navigation button asked for.
        ev.stopPropagation();

        const params = 'run=' + encodeURIComponent(src.evolutionRunId)
          + '&view=tree&highlight=' + encodeURIComponent(src.soundId);

        call('openSitePath', '/tree?' + params).catch(function (err) {
          setStatus(String(err), 'err');
        });
      });
      top.appendChild(tree);
    }

    // Remove. Inline confirmation rather than a modal: the whole page is one
    // list, and a dialog over it to ask about one row is more interruption than
    // the action deserves -- especially since it goes to the Trash and is
    // recoverable, so the confirm is politeness rather than a safety net.
    const rm = document.createElement('button');
    rm.className = 'rmbtn';
    rm.textContent = '\u00d7';
    rm.title = 'Move this instrument to the Trash';
    rm.addEventListener('click', function (ev) {
      ev.stopPropagation();

      const confirm = document.createElement('span');
      confirm.className = 'confirm';
      confirm.appendChild(document.createTextNode('Move to Trash?'));

      const yes = document.createElement('button');
      yes.className = 'yes';
      yes.textContent = 'remove';
      yes.addEventListener('click', async function (e2) {
        e2.stopPropagation();
        try {
          const ok = await call('removeInstrument', e.id);
          setStatus(ok ? 'Moved \u201c' + e.name + '\u201d to the Trash.'
                       : 'Could not remove that instrument.', ok ? undefined : 'err');
          // Dropped from the new set too, or a re-added copy would inherit the
          // badge from the one that was just removed.
          newIds.delete(e.id);
          refresh();
        } catch (err) {
          setStatus(String(err), 'err');
        }
      });

      const no = document.createElement('button');
      no.className = 'no';
      no.textContent = 'cancel';
      no.addEventListener('click', function (e2) {
        e2.stopPropagation();
        confirm.replaceWith(rm);
      });

      confirm.appendChild(yes);
      confirm.appendChild(no);
      rm.replaceWith(confirm);
    });

    top.appendChild(rm);

    row.appendChild(top);

    // Omitted entirely when there is nothing to say, rather than rendered as an
    // empty line. An instrument with no metadata should look plain, not broken.
    const bits = describe(e);

    if (bits.length) {
      const sub = document.createElement('div');
      sub.className = 'sub';

      bits.forEach(function (bit, i) {
        if (i > 0) {
          const sep = document.createElement('span');
          sep.className = 'sep';
          sep.textContent = '\u00b7';
          sub.appendChild(sep);
        }
        const span = document.createElement('span');
        if (bit.warn) span.className = 'warn';
        span.textContent = bit.text;
        sub.appendChild(span);
      });

      row.appendChild(sub);
    }

    row.addEventListener('click', function () { load(e.id); });
    list.appendChild(row);
  });

  // After the rows exist, not before. Deferred a frame because the canvas and
  // the rows land in the same paint and scrollIntoView on an element the
  // browser has not laid out yet does nothing.
  if (scrollToId) {
    const target = list.querySelector('[data-id="' + scrollToId + '"]');
    scrollToId = null;
    if (target) requestAnimationFrame(function () {
      target.scrollIntoView({ block: 'center', behavior: 'smooth' });
    });
  }
}

async function load (id) {
  // Marked BEFORE the native call, because that call blocks the message thread
  // and nothing after it will run until the load finishes. Everything the user
  // needs to see has to be in the DOM by this point.
  const row = list.querySelector('[data-id="' + id + '"]');
  list.classList.add('busy');

  let spinner = null;

  if (row) {
    row.classList.add('pending');
    spinner = document.createElement('span');
    spinner.className = 'spinner';
    const top = row.firstChild;
    if (top) top.insertBefore(spinner, top.firstChild);
  }

  // Says WHY it takes a moment. "Loading" invites the reading that something is
  // stuck; naming the work makes a two-second wait legible.
  setStatus('Measuring levels across the keyboard\u2026');

  try {
    let r = await call('loadInstrument', id);

    // Traced, because the processor's log said ok=1 and the shell said failed,
    // twice, and reconciling those by reasoning produced two wrong answers.
    call('logFromShell', 'load start id=' + id
      + ' ok=' + JSON.stringify(r && r.ok)
      + ' measuring=' + JSON.stringify(r && r.measuring)
      + ' detail=' + JSON.stringify(r && r.detail));

    // The Faust path hands the measurement to a background thread and returns
    // as soon as the module is in, so this first reply means "started", not
    // "ready". Polling rather than waiting for a push: the editor can be
    // destroyed while a measurement runs, and a push into a destroyed editor
    // is the problem AcquisitionRunner already avoided the same way.

    // Loop on MEASURING ALONE, never on ok.
    //
    // getLoadState reports the last COMPLETED load, and on the first load of a
    // session there isn't one -- the outcome is default-constructed, so ok is
    // false. Testing `r.ok && r.measuring` therefore exited on the first poll
    // and reported "Load failed" for a load that was measuring perfectly well
    // and went on to succeed. Reopening the window showed the instrument
    // active, because it had loaded all along.
    //
    // Capped so a measurement that never clears cannot hang the list forever.
    // 20 s is comfortably past the slowest measurement observed, 4.8 s.
    let waited = 0;

    while (r.measuring && waited < 20000) {
      await new Promise(function (res) { setTimeout(res, 120); });
      waited += 120;
      r = await call('getLoadState');
    }

    if (r.measuring) {
      setStatus('Still measuring after 20 s \u2014 see the log', 'err');
      return;
    }

    call('logFromShell', 'load settled id=' + id
      + ' polls=' + (waited / 120)
      + ' ok=' + JSON.stringify(r && r.ok)
      + ' rid=' + JSON.stringify(r && r.id)
      + ' regions=' + JSON.stringify(r && r.regions)
      + ' tail=' + JSON.stringify(r && r.tailSeconds)
      + ' detail=' + JSON.stringify(r && r.detail));

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
    drawSpectrogram();
  } catch (err) {
    call('logFromShell', 'load threw id=' + id + ' err=' + String(err));
    setStatus(String(err), 'err');
  } finally {
    // In a finally, not after the try: a failed load must not leave the list
    // permanently unclickable. render() rebuilds the rows and takes the
    // spinner with them, but the busy class lives on the list itself and
    // would survive.
    list.classList.remove('busy');
    if (spinner && spinner.parentNode) spinner.parentNode.removeChild(spinner);
    if (row) row.classList.remove('pending');
  }
}

// ── the loaded instrument's own spectrogram ─────────────────────────────────
//
// Rendered by the plugin during the load measurement, not fetched. The
// measurement already produces five seconds of audio at a4 and keeps only the
// peak, so the picture costs nothing extra -- and it is this instrument as this
// machine plays it, at the host's sample rate, rather than a render made
// elsewhere. A network fetch would also be the one thing this page must never
// do: it is the offline surface.
async function drawSpectrogram () {
  const canvas = document.getElementById('spectro');
  if (!canvas) return;

  let spec = null;
  try { spec = await call('getSpectrogram'); } catch (e) { /* stays empty */ }

  if (!spec || !spec.data) { canvas.className = 'empty'; return; }

  const bins = spec.bins, frames = spec.frames;

  // One byte per cell, base64 over the bridge. atob gives a binary string, so
  // charCodeAt is the decode.
  let raw;
  try { raw = atob(spec.data); } catch (e) { canvas.className = 'empty'; return; }
  if (raw.length < bins * frames) { canvas.className = 'empty'; return; }

  // Drawn at data resolution and stretched by CSS. Sizing the backing store to
  // the element instead would mean re-rendering on every resize for an image
  // that is deliberately blurred to 16% opacity anyway.
  canvas.width = frames;
  canvas.height = bins;

  const ctx = canvas.getContext('2d');
  const image = ctx.createImageData(frames, bins);

  for (let f = 0; f < frames; f++) {
    for (let b = 0; b < bins; b++) {
      const v = raw.charCodeAt(f * bins + b);
      // Low frequencies at the BOTTOM, which is how every spectrogram on the
      // site is drawn; the data runs the other way.
      const px = ((bins - 1 - b) * frames + f) * 4;
      // Blue-green ramp, matching the site's spectrograms closely enough to
      // read as the same kind of picture without importing a palette.
      //
      // Alpha is CURVED rather than linear in the magnitude. A linear ramp put
      // most of the image in the 20-60 range, where it was too faint to read
      // at any panel opacity that stayed subdued; the square-root lifts the
      // quiet detail without letting the loud partials dominate.
      const a = Math.sqrt(v / 255);
      image.data[px]     = Math.round(v * 0.18);
      image.data[px + 1] = Math.round(v * 0.85);
      image.data[px + 2] = Math.round(60 + v * 0.55);
      image.data[px + 3] = Math.round(a * 255);
    }
  }

  ctx.putImageData(image, 0, 0);
  canvas.className = '';
}

async function refresh () {
  if (!backend) {
    setStatus('Native bridge unavailable \u2014 this page is not being served '
              + 'by the plugin.', 'err');
    return;
  }
  try {
    entries = await call('listInstruments');

    // Anything that was not here last time. On the FIRST list knownIds is
    // null, so nothing is marked -- otherwise opening the editor would badge
    // every instrument in the library as new.
    const ids = new Set(entries.map(function (e) { return e.id; }));

    if (knownIds !== null) {
      const arrived = entries.filter(function (e) { return !knownIds.has(e.id); });

      if (arrived.length) {
        arrived.forEach(function (e) { newIds.add(e.id); });
        // The last one, because acquisitions finish in submission order and the
        // most recent is the one the user was waiting for.
        scrollToId = arrived[arrived.length - 1].id;
      }
    }

    knownIds = ids;
    // Ids that have since been deleted should not stay marked forever.
    newIds.forEach(function (id) { if (!ids.has(id)) newIds.delete(id); });

    const cur = await call('getStatus');
    currentId = cur && cur.ok ? cur.id : null;
    render();
    drawSpectrogram();
    setStatus(currentId
      ? 'Loaded \u00b7 ' + cur.regions + ' regions \u00b7 '
        + cur.tailSeconds.toFixed(1) + ' s tail'
      : 'Nothing loaded yet \u2014 pick an instrument.');
  } catch (err) {
    setStatus(String(err), 'err');
  }
}

document.getElementById('refresh').addEventListener('click', function () {
  // An explicit refresh is the user saying "show me the current state", so the
  // new-arrival marks are cleared: they exist to point out something that
  // appeared while attention was elsewhere.
  newIds.clear();
  refresh();
});

document.getElementById('filter').addEventListener('input', function (ev) {
  filterText = ev.target.value;
  render();
});

document.getElementById('sort').addEventListener('change', function (ev) {
  sortBy = ev.target.value;
  // Remembered across editor opens, which happen constantly -- a DAW creates
  // and destroys the view whenever the window is closed. try/catch because the
  // shell is served from a custom scheme and storage may be unavailable on an
  // opaque origin; losing the preference is not worth an exception.
  try { localStorage.setItem('synthis.sort', sortBy); } catch (e) { /* fine */ }
  render();
});

try {
  const saved = localStorage.getItem('synthis.sort');
  if (saved) { sortBy = saved; document.getElementById('sort').value = saved; }
} catch (e) { /* fine */ }

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
      + (st.voiceCap > 0 ? ' \u00b7 ' + st.voiceCap + '-voice cap (RTF '
                           + st.rtf.toFixed(0) + 'x)' : '')
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
