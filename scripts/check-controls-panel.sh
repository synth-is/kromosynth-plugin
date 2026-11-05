#!/usr/bin/env bash
# check-controls-panel.sh — grep-level acceptance checks for synthis-cda.3
# (the Controls sub-tab in the shell page: sliders, Learn, ranges, input
# monitor, OSC line).
#
# The gate (scripts/gate.sh) cannot see the editor, so these checks encode the
# task's acceptance criteria directly against src/. Run by hand:
#
#   bash scripts/check-controls-panel.sh
#
# Exit 0 = every criterion is present in the sources and the preview builds
# and runs clean; exit 1 = at least one is missing. This script is NOT part
# of gate.sh and changes nothing the gate relies on.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }

S=src/ShellPage.h

# ── 1. a Controls sub-tab exists, inside nav#chrome's .subtabs row ──────────
# The sub-tabs row must stay INSIDE nav#chrome so chromeHeight() keeps
# measuring the whole bar (see the comments beside the markup).
awk '
  /<nav id="chrome">/            { nav = NR }
  /<\/nav>/                      { if (nav && !navEnd) navEnd = NR }
  /<div class="subtabs">/        { if (nav && !subs) subs = NR }
  /data-view="controls"/         { if (ctl == 0) ctl = NR }
  END { exit !(nav > 0 && navEnd > nav && subs > nav && ctl > subs && ctl < navEnd) }
' "$S" \
  && ok "Controls sub-tab sits in the .subtabs row inside nav#chrome" \
  || bad "Controls sub-tab sits in the .subtabs row inside nav#chrome"

# ── 2. every bound slot gets a row: the rows are built from getCcMappings,
#       one row per entry, each with name, live-value slider, mapped-to chip,
#       Learn and a clear control ────────────────────────────────────────────
grep -q "call('getCcMappings'" "$S" \
  && ok "rows are read from the getCcMappings native bridge" \
  || bad "rows are read from the getCcMappings native bridge"
awk '
  /function updateControls/ { infn = 1 }
  infn && /rows\.forEach/   { loop = 1 }
  infn && /buildControlRow/ { build = 1 }
  END { exit !(loop && build) }
' "$S" \
  && ok "one row is built per entry the bridge returns (no filtering)" \
  || bad "one row is built per entry the bridge returns (no filtering)"
for cls in cname cslider cval cchip learn cclear; do
  grep -q "className = '$cls" "$S" \
    && ok "row carries a .$cls control" \
    || bad "row carries a .$cls control"
done
grep -q "type = 'range'" "$S" \
  && ok "the live value is shown on a range slider" \
  || bad "the live value is shown on a range slider"
grep -q "call('setCcLearnTarget'" "$S" \
  && ok "Learn arms the native learn target" \
  || bad "Learn arms the native learn target"
grep -q "call('clearCcMapping'" "$S" \
  && ok "the clear control calls clearCcMapping" \
  || bad "the clear control calls clearCcMapping"

# ── 2b. Learn / clear / Apply address a target the bridge can resolve ───────
# ccTargetFromName (src/PluginProcessor.cpp) resolves only "attack".."output"
# or a slot number 1..16; "macro1".getIntValue() is 0, which resolves to -1,
# so macro slots must be passed as slot numbers (target + 1) and the
# envelope/output targets as names.
grep -q 'function ccTargetArg' "$S" \
  && ok "a ccTargetArg helper builds the bridge-resolvable identifier" \
  || bad "a ccTargetArg helper builds the bridge-resolvable identifier"
grep -q 'String(m.target + 1)' "$S" \
  && ok "macro slots are addressed by slot number (target + 1)" \
  || bad "macro slots are addressed by slot number (target + 1)"
grep -q "call('setCcLearnTarget', arm ? ccTargetArg(m) : -1)" "$S" \
  && ok "Learn passes the resolvable identifier to setCcLearnTarget" \
  || bad "Learn passes the resolvable identifier to setCcLearnTarget"
grep -q "call('clearCcMapping', ccTargetArg(m))" "$S" \
  && ok "clear passes the resolvable identifier to clearCcMapping" \
  || bad "clear passes the resolvable identifier to clearCcMapping"
grep -q "call('setCcMapping', ccTargetArg(m)," "$S" \
  && ok "the range editor's Apply passes the resolvable identifier to setCcMapping" \
  || bad "the range editor's Apply passes the resolvable identifier to setCcMapping"

# ── 3. the live value is polled 10..20 times a second ───────────────────────
ms=$(grep -o 'const CC_POLL_MS = [0-9][0-9]*' "$S" | grep -o '[0-9][0-9]*$')
if [[ -n "$ms" ]] && awk -v ms="$ms" 'BEGIN { exit !(ms >= 50 && ms <= 100) }'; then
  ok "native values are polled every ${ms} ms (10..20 times a second)"
else
  bad "native values are polled every ${ms:-?} ms (10..20 times a second)"
fi
grep -q 'setInterval(pollControls, CC_POLL_MS)' "$S" \
  && ok "the poll runs on a timer" \
  || bad "the poll runs on a timer"

# ── 3b. the row slider is a disabled live read-out ──────────────────────────
# No native setter moves a parameter from the panel, so the slider must be
# built disabled (with a CSS rule keeping it readable) and updateControls
# must always write its value -- otherwise a click or drag leaves it showing
# the dragged position while the real value (host, OSC, MIDI) moves on.
grep -q 'slider\.disabled = true' "$S" \
  && ok "the row slider is built disabled (live read-out, not a control)" \
  || bad "the row slider is built disabled (live read-out, not a control)"
grep -qF '.crow .cslider:disabled { opacity: 1; cursor: default; }' "$S" \
  && ok "a disabled slider stays readable (opacity/cursor CSS rule)" \
  || bad "a disabled slider stays readable (opacity/cursor CSS rule)"
if awk '
  /^function updateControls/ { infn = 1; next }
  infn && /^function /        { infn = 0 }
  infn && /activeElement/    { hit = 1 }
  END { exit hit ? 0 : 1 }
' "$S"; then
  bad "updateControls no longer exempts the focused slider (still mentions activeElement)"
else
  ok "updateControls always writes the slider value (no activeElement exemption)"
fi

# ── 4. the expandable range / smoothing / invert editor ─────────────────────
grep -q 'cedit' "$S" \
  && ok "each row has an expandable editor (.cedit)" \
  || bad "each row has an expandable editor (.cedit)"
for f in rangeMin rangeMax smoothing invert; do
  grep -q "$f" "$S" \
    && ok "the editor edits $f" \
    || bad "the editor edits $f"
done
grep -q "call('setCcMapping'" "$S" \
  && ok "the editor applies through setCcMapping" \
  || bad "the editor applies through setCcMapping"

# ── 5. the five envelope/output parameters under their own heading ──────────
grep -q 'id="envhead"' "$S" \
  && ok "an envelope/output heading exists" \
  || bad "an envelope/output heading exists"
grep -q 'id="envlist"' "$S" \
  && ok "the envelope/output rows render into their own list" \
  || bad "the envelope/output rows render into their own list"
awk '
  /ENV_TARGET_NAMES/ {
    found = 1
    if ($0 ~ /attack/ && $0 ~ /decay/ && $0 ~ /sustain/ \
     && $0 ~ /release/ && $0 ~ /output/) ok = 1
  }
  END { exit !(found && ok) }
' "$S" \
  && ok "the five envelope/output targets are named (attack decay sustain release output)" \
  || bad "the five envelope/output targets are named (attack decay sustain release output)"

# ── 6. the input monitor and the OSC port/IP line exist and are shown only
#       when the native bridge reports the Standalone ────────────────────────
grep -q 'id="ccmonitor"' "$S" \
  && ok "the input monitor line exists" \
  || bad "the input monitor line exists"
grep -q 'id="oscline"' "$S" \
  && ok "the OSC port/IP line exists" \
  || bad "the OSC port/IP line exists"
awk '
  /id="ccmonitor"/ && /sa-only/ { mon = 1 }
  /id="oscline"/   && /sa-only/ { osc = 1 }
  END { exit !(mon && osc) }
' "$S" \
  && ok "both lines carry the standalone-only class" \
  || bad "both lines carry the standalone-only class"
grep -q 'body:not(.standalone) .sa-only' "$S" \
  && ok "the standalone-only class is hidden unless body.standalone" \
  || bad "the standalone-only class is hidden unless body.standalone"
grep -q 'receiverExists' "$S" \
  && ok "the Standalone is detected from the bridge's receiverExists" \
  || bad "the Standalone is detected from the bridge's receiverExists"
grep -q "call('getOscState'" "$S" \
  && ok "the OSC state is read from the bridge" \
  || bad "the OSC state is read from the bridge"
grep -q "call('getLocalIpAddresses'" "$S" \
  && ok "this device's IP addresses are read from the bridge" \
  || bad "this device's IP addresses are read from the bridge"
grep -q "call('getCcMonitor'" "$S" \
  && ok "the input monitor reads the bridge's monitor ring" \
  || bad "the input monitor reads the bridge's monitor ring"

# ── 6b. the whole mapping UI is Standalone-only, including the range editor ──
# In a DAW the host owns MIDI input: the chip, Learn, clear, the expand
# control, the range editor (its Apply writes a CC mapping via setCcMapping)
# and the empty state (which points at the hidden Learn button) must all
# carry .sa-only so body:not(.standalone) hides them.
for cls in cchip learn cclear cexpand cedit; do
  grep -q "className = '$cls sa-only'" "$S" \
    && ok ".$cls is standalone-only" \
    || bad ".$cls is standalone-only"
done
grep -q 'class="empty sa-only" id="controlsempty"' "$S" \
  && ok "the empty state is standalone-only (it points at the hidden Learn button)" \
  || bad "the empty state is standalone-only (it points at the hidden Learn button)"

# ── 7. an empty-state sentence explaining Learn ──────────────────────────────
grep 'id="controlsempty"' "$S" | grep -q 'Learn' \
  && ok "the empty state explains in one sentence what Learn does" \
  || bad "the empty state explains in one sentence what Learn does"

# ── 8. the page builds with the preview tool and the generated HTML runs with
#       no script errors under the preview's stub bridge ─────────────────────
if node tools/shell-preview/build-preview.cjs >/dev/null 2>&1 \
   && [[ -s tools/shell-preview/shell-preview.html ]]; then
  ok "node tools/shell-preview/build-preview.cjs builds the page"
else
  bad "node tools/shell-preview/build-preview.cjs builds the page"
fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
cat > "$TMPD/load-preview.cjs" <<'LOADER_EOF'
// Loads the generated preview HTML in a vm with a minimal DOM and the
// preview's own stub bridge, switches to the Controls view, lets the polls
// run, and reports any script error or missing structure.
const fs = require('fs');
const vm = require('vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const scripts = [];
const re = /<script>([\s\S]*?)<\/script>/g;
let m;
while ((m = re.exec(html)) !== null) scripts.push(m[1]);

function makeEl (tag) {
  const el = {
    tagName: String(tag || 'div').toUpperCase(),
    children: [], dataset: {}, style: {}, className: '', textContent: '',
    hidden: false, value: '', checked: false, disabled: false, title: '',
    type: '', min: '', max: '', step: '', parentNode: null,
    appendChild (c) { c.parentNode = el; el.children.push(c); return c; },
    removeChild (c) {
      const i = el.children.indexOf(c);
      if (i >= 0) el.children.splice(i, 1);
      c.parentNode = null; return c;
    },
    insertBefore (c) { return el.appendChild(c); },
    addEventListener () {}, removeEventListener () {},
    querySelector () { return null; }, querySelectorAll () { return []; },
    getBoundingClientRect () { return { height: 0, width: 0 }; },
    getContext () { return null; },
    setAttribute () {}, getAttribute () { return null; },
    replaceWith () {}, scrollIntoView () {}, focus () {}, click () {}
  };
  const classes = new Set();
  el.classList = {
    add: function () { for (const c of arguments) classes.add(c); },
    remove: function () { for (const c of arguments) classes.delete(c); },
    toggle: function (c, on) {
      if (on === undefined) on = !classes.has(c);
      if (on) classes.add(c); else classes.delete(c);
      return on;
    },
    contains: function (c) { return classes.has(c); }
  };
  Object.defineProperty(el, 'innerHTML', {
    get () { return ''; },
    set () { el.children.length = 0; }
  });
  return el;
}

const byId = new Map();
const document = {
  body: makeEl('body'),
  getElementById (id) {
    if (!byId.has(id)) byId.set(id, makeEl('#' + id));
    return byId.get(id);
  },
  querySelectorAll () { return []; },
  createElement (tag) { return makeEl(tag); },
  createTextNode (t) { const el = makeEl('#text'); el.textContent = String(t); return el; },
  addEventListener () {},
  activeElement: null
};

// Pre-register the static markup's id/class pairs, so the checks below see
// the classes the generated HTML ships with (e.g. sa-only on #ccmonitor),
// not just the elements the page script happens to fetch by id.
const tagRe = /<([a-zA-Z]+)((?:[^>"']|"[^"]*"|'[^']*')*)>/g;
let tm;
while ((tm = tagRe.exec(html)) !== null) {
  const idm = tm[2].match(/\bid="([^"]+)"/);
  if (idm && !byId.has(idm[1])) {
    const el = makeEl(tm[1]);
    const clm = tm[2].match(/\bclass="([^"]*)"/);
    if (clm) el.className = clm[1];
    byId.set(idm[1], el);
  }
}

const errors = [];
process.on('uncaughtException', (e) => errors.push(String((e && e.stack) || e)));
process.on('unhandledRejection', (e) => errors.push('rejection: ' + String(e)));

const sandbox = {
  console: console,
  setTimeout: setTimeout, clearTimeout: clearTimeout,
  setInterval: setInterval, clearInterval: clearInterval,
  atob: (s) => Buffer.from(s, 'base64').toString('binary'),
  document: document,
  navigator: { userAgent: 'check-controls-panel' },
  requestAnimationFrame: (fn) => setTimeout(fn, 0)
};
sandbox.window = sandbox;
sandbox.addEventListener = function () {};
sandbox.dispatchEvent = function () {};
vm.createContext(sandbox);

for (const src of scripts) {
  try { vm.runInContext(src, sandbox, { filename: 'preview-script' }); }
  catch (e) { errors.push(String((e && e.stack) || e)); }
}

// Switch to the Controls view the way the sub-tab does, so the row-building
// and polling code actually runs against the stub's answers.
try { vm.runInContext('setLibraryView("controls")', sandbox); }
catch (e) { errors.push('setLibraryView: ' + String((e && e.stack) || e)); }

setTimeout(() => {
  const problems = [];
  if (errors.length) problems.push('script errors:\n  ' + errors.join('\n  '));

  const cclist = byId.get('cclist');
  const envlist = byId.get('envlist');
  const rows = (cclist ? cclist.children : []).concat(envlist ? envlist.children : []);
  if (rows.length !== 21)
    problems.push('expected 21 control rows (16 slots + 5 envelope/output), got ' + rows.length);
  if (!cclist || cclist.children.length !== 16)
    problems.push('expected 16 slot rows in #cclist, got ' + (cclist ? cclist.children.length : 'none'));
  if (!envlist || envlist.children.length !== 5)
    problems.push('expected 5 rows in #envlist, got ' + (envlist ? envlist.children.length : 'none'));

  function hasDesc (el, cls) {
    return el.children.some(function (c) {
      return String(c.className || '').split(/\s+/).indexOf(cls) >= 0 || hasDesc(c, cls);
    });
  }
  rows.forEach(function (row, i) {
    ['cname', 'cslider', 'cval', 'cchip', 'learn', 'cclear'].forEach(function (cls) {
      if (!hasDesc(row, cls))
        problems.push('row ' + i + ' has no .' + cls);
    });
  });

  const mon = byId.get('ccmonitor');
  const osc = byId.get('oscline');
  if (!document.body.classList.contains('standalone'))
    problems.push('body never got .standalone although the stub reports receiverExists: true');
  if (!mon || !String(mon.className || '').match(/sa-only/))
    problems.push('#ccmonitor missing or not standalone-only');
  if (!osc || !String(osc.className || '').match(/sa-only/))
    problems.push('#oscline missing or not standalone-only');
  if (osc && !osc.textContent)
    problems.push('#oscline has no text (port/IP line not rendered)');
  if (mon && !mon.textContent)
    problems.push('#ccmonitor has no text (input monitor not rendered)');
  if (!byId.get('controlsempty'))
    problems.push('#controlsempty missing');
  if (!byId.get('envhead'))
    problems.push('#envhead missing');

  if (problems.length) {
    console.error('preview load problems:');
    problems.forEach((p) => console.error('  ' + p));
    process.exit(1);
  }
  console.log('preview loads clean: ' + scripts.length + ' script blocks, '
    + rows.length + ' control rows, no script errors');
  process.exit(0);
}, 600);
LOADER_EOF

if [[ -s tools/shell-preview/shell-preview.html ]]; then
  if node "$TMPD/load-preview.cjs" tools/shell-preview/shell-preview.html; then
    ok "generated HTML runs with no script errors under the preview's stub bridge"
  else
    bad "generated HTML runs with no script errors under the preview's stub bridge"
  fi
else
  bad "generated HTML runs with no script errors under the preview's stub bridge (no preview built)"
fi

# ── 9. the gate and the protected files are unchanged ────────────────────────
if git diff --quiet HEAD -- scripts/gate.sh scripts/null-test.sh \
     docs/BASELINE_FAUSTENGINE_2026-09-07.txt scripts/processor-diff.sh \
     tools/processor-probe/diff_wav.py 2>/dev/null; then
  ok "scripts/gate.sh and the protected files are unchanged"
else
  bad "scripts/gate.sh and the protected files are unchanged"
fi

# ── 10. no leftover debug tooling at the repo root ───────────────────────────
if [[ ! -e .dbg-loader.cjs ]]; then
  ok "no leftover .dbg-loader.cjs at the repo root"
else
  bad "no leftover .dbg-loader.cjs at the repo root"
fi

echo
if (( fail == 0 )); then
  echo "check-controls-panel: all criteria present"
  exit 0
else
  echo "check-controls-panel: criteria missing (see FAIL lines)"
  exit 1
fi
