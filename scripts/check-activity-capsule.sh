#!/usr/bin/env bash
# check-activity-capsule.sh — grep-level acceptance checks for synthis-7o3.3
# (the bar part of the render-progress epic: the activity capsule in nav#chrome,
# the Library badge, the landed hand-off, reduced motion, and the removal of
# the arrival status line in pollAcquisitions).
#
# The gate (scripts/gate.sh) cannot see the editor's page, so these checks
# encode the task's acceptance criteria directly against src/. Run by hand:
#
#   bash scripts/check-activity-capsule.sh
#
# Exit 0 = every criterion is present in the sources and the preview builds,
# loads and answers fake activity events with no script errors; exit 1 = at
# least one is missing. This script is NOT part of gate.sh and changes nothing
# the gate relies on.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }

S=src/ShellPage.h

# ── 1. the capsule sits inside nav#chrome's .tabs row, before the Panic
#       button, so chromeHeight() keeps measuring the whole bar ──────────────
awk '
  /<div class="tabs">/  { tabs = NR }
  /id="capsule"/         { if (cap == 0) cap = NR }
  /id="panic"/           { if (panic == 0) panic = NR }
  /<div class="line">/   { line = NR }
  END { exit !(tabs > 0 && cap > tabs && panic > cap && line > panic) }
' "$S" \
  && ok "activity capsule sits in .tabs inside nav#chrome, before #panic" \
  || bad "activity capsule sits in .tabs inside nav#chrome, before #panic"

# ── 2. the capsule carries a progress ring (determinate circle + tick) and a
#       text span plus a count span for the narrow bar ───────────────────────
awk '
  /id="capsule"/ { incap = 1 }
  incap && !done && /id="panic"/ { done = 1; exit !(ring && ringfg && tick && captext && capcount) }
  incap && /class="ring"/     { ring = 1 }
  incap && /id="ringfg"/      { ringfg = 1 }
  incap && /class="tick"/     { tick = 1 }
  incap && /id="capsuletext"/ { captext = 1 }
  incap && /id="capsulecount"/{ capcount = 1 }
  END { if (!done) exit 1 }
' "$S" \
  && ok "capsule has the ring svg (ringfg circle, tick path), text and count spans" \
  || bad "capsule has the ring svg (ringfg circle, tick path), text and count spans"

# ── 2b. the ring's fill is written as an inline style, not a presentation
#       attribute: the stylesheet rule "stroke-dashoffset: 50.27" on .ringfg
#       beats any presentation attribute, so a setAttribute write would leave
#       the determinate ring (and the warn ring) empty ────────────────────────
awk '
  /function renderActivity/ { infn = 1; next }
  infn && !done && /^}/ { done = 1; exit !inline }
  infn && /ringfg\.style\.strokeDashoffset/ { inline = 1 }
  END { if (!done) exit 1 }
' "$S" \
  && ok "renderActivity drives the ring fill through ringfg.style.strokeDashoffset (inline style beats the stylesheet rule)" \
  || bad "renderActivity drives the ring fill through ringfg.style.strokeDashoffset (inline style beats the stylesheet rule)"
if grep -q "setAttribute('stroke-dashoffset'" "$S"; then
  bad "no setAttribute('stroke-dashoffset' left in ShellPage.h (a presentation attribute loses to the CSS rule)"
else
  ok "no setAttribute('stroke-dashoffset' left in ShellPage.h (a presentation attribute loses to the CSS rule)"
fi

# ── 3. the Library tab (data-dest="0") carries the badge ────────────────────
awk '
  /data-dest="0"/ && /id="libtab"/ { lib = NR }
  lib && /id="libbadge"/           { badge = NR }
  badge && !done && /<\/button>/   { done = 1; exit !(lib > 0 && badge > lib) }
  END { if (!done) exit 1 }
' "$S" \
  && ok "Library tab (data-dest=\"0\", id=libtab) carries #libbadge inside the button" \
  || bad "Library tab (data-dest=\"0\", id=libtab) carries #libbadge inside the button"

# ── 4. wiring: the page listens for activityChanged, pulls getActivity, and
#       re-pulls every 2 s while anything is active ──────────────────────────
grep -q "addEventListener('activityChanged'" "$S" \
  && ok "page listens for the activityChanged push" \
  || bad "page listens for the activityChanged push"
grep -q "call('getActivity'" "$S" \
  && ok "page pulls the activity list through getActivity" \
  || bad "page pulls the activity list through getActivity"
grep -q 'ACTIVITY_PULL_MS = 2000' "$S" \
  && ok "the safety-net pull interval is 2000 ms" \
  || bad "the safety-net pull interval is 2000 ms"
grep -q 'setInterval(pollActivityTimer, ACTIVITY_PULL_MS)' "$S" \
  && ok "the 2 s pull runs on a timer" \
  || bad "the 2 s pull runs on a timer"
awk '
  /function pollActivityTimer/ { infn = 1; next }
  infn && !done && /^}/ { done = 1; exit !pulls }
  infn && /activityActive > 0/ && /pullActivity/ { pulls = 1 }
  END { if (!done) exit 1 }
' "$S" \
  && ok "the timer pulls only while active > 0" \
  || bad "the timer pulls only while active > 0"

# ── 5. the landed hand-off: pulse on the Library tab, fold of the capsule,
#       coalescing window and minimum hold ───────────────────────────────────
grep -q "classList.add('pulse'" "$S" \
  && ok "landing pulses the Library tab" \
  || bad "landing pulses the Library tab"
grep -q "classList.add('fold'" "$S" \
  && ok "the capsule folds into the Library tab on landing" \
  || bad "the capsule folds into the Library tab on landing"
grep -q 'LANDING_COALESCE_MS = 1000' "$S" \
  && ok "landings within 1 s coalesce (LANDING_COALESCE_MS = 1000)" \
  || bad "landings within 1 s coalesce (LANDING_COALESCE_MS = 1000)"
grep -q 'LANDING_HOLD_MS = 1200' "$S" \
  && ok "a landing is held at least 1.2 s (LANDING_HOLD_MS = 1200)" \
  || bad "a landing is held at least 1.2 s (LANDING_HOLD_MS = 1200)"
grep -q 'capsule.hidden = true' "$S" \
  && ok "the capsule hides when idle" \
  || bad "the capsule hides when idle"
grep -q 'capsule.title' "$S" \
  && ok "the capsule tooltip lists the jobs (oldest first)" \
  || bad "the capsule tooltip lists the jobs (oldest first)"
grep -q "clearLibraryBadge" "$S" \
  && ok "opening the Library clears the badge" \
  || bad "opening the Library clears the badge"

# ── 6. the badge: unseen-landed count, ring dot while in flight ─────────────
grep -q 'unseenLanded' "$S" \
  && ok "the badge reads unseenLanded (landed-and-unseen only)" \
  || bad "the badge reads unseenLanded (landed-and-unseen only)"
grep -q "classList.add('dot')" "$S" \
  && ok "the tab carries a ring dot (no digit) while jobs are in flight" \
  || bad "the tab carries a ring dot (no digit) while jobs are in flight"

# ── 7. prefers-reduced-motion: no travel, no pulse, static tick ─────────────
awk '
  /prefers-reduced-motion: reduce/ { inrm = 1 }
  inrm && /animation: none/  { anim = 1 }
  inrm && /transition: none/ { trans = 1 }
  inrm && !done && /^[[:space:]]*}[[:space:]]*$/ { done = 1; exit !(anim && trans) }
  END { if (!done) exit 1 }
' "$S" \
  && ok "prefers-reduced-motion disables the ring spin, the pulse and the fold travel" \
  || bad "prefers-reduced-motion disables the ring spin, the pulse and the fold travel"

# ── 8. narrow bar (the 639px breakpoint): ring and count only ───────────────
awk '
  /max-width: 639px/ { inN = 1; t = 0; c = 0 }
  inN && /#capsuletext/ && /display: none/   { t = 1 }
  inN && /#capsulecount/ && /display: inline/ { c = 1 }
  inN && /^[[:space:]]*}[[:space:]]*$/ { if (t && c) found = 1; inN = 0 }
  END { exit !found }
' "$S" \
  && ok "in a narrow bar the capsule shows the ring and the count only" \
  || bad "in a narrow bar the capsule shows the ring and the count only"

# ── 9. the arrival status line is gone from pollAcquisitions; the failure
#       message, the clear, refresh() and refreshRenders() stay ──────────────
if grep -q "setStatus('Downloading '" "$S"; then
  bad "the in-flight 'Downloading <id> <pct>%' status line is removed"
else
  ok "the in-flight 'Downloading <id> <pct>%' status line is removed"
fi
if grep -q "'Added ' + added" "$S" || grep -q "'Saved ' + renders" "$S"; then
  bad "the 'Added N instrument(s)' / 'Saved N render(s)' status line is removed"
else
  ok "the 'Added N instrument(s)' / 'Saved N render(s)' status line is removed"
fi
awk '
  /async function pollAcquisitions/ { infn = 1; next }
  infn && /setInterval\(pollAcquisitions/ { exit !(failmsg && clear && refresh && refreshR) }
  infn && /failed\[0\]\.message/          { failmsg = 1 }
  infn && /clearFinishedAcquisitions/     { clear = 1 }
  infn && /[^f]refresh\(\);/              { refresh = 1 }
  infn && /refreshRenders\(\)/            { refreshR = 1 }
' "$S" \
  && ok "pollAcquisitions keeps the failure message, clearFinishedAcquisitions, refresh() and refreshRenders()" \
  || bad "pollAcquisitions keeps the failure message, clearFinishedAcquisitions, refresh() and refreshRenders()"

# ── 10. the page builds with the preview tool and the generated HTML loads
#        with the stub bridge fed fake activity events, no script errors ─────
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
// preview's own stub bridge, then drives the capsule with fake
// activityChanged events through window.__fireActivity and checks the
// capsule, the ring state, the Library badge, the landing pulse and the
// badge clear on opening the Library.
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
    type: '', min: '', max: '', step: '', parentNode: null, _listeners: {},
    appendChild (c) { c.parentNode = el; el.children.push(c); return c; },
    removeChild (c) {
      const i = el.children.indexOf(c);
      if (i >= 0) el.children.splice(i, 1);
      c.parentNode = null; return c;
    },
    insertBefore (c) { return el.appendChild(c); },
    addEventListener (type, fn) { (el._listeners[type] = el._listeners[type] || []).push(fn); },
    removeEventListener () {},
    click () { (el._listeners.click || []).forEach(function (fn) { fn.call(el, {}); }); },
    querySelector () { return null; }, querySelectorAll () { return []; },
    getBoundingClientRect () { return { height: 0, width: 0 }; },
    getContext () { return null; },
    setAttribute () {}, getAttribute () { return null; },
    replaceWith () {}, scrollIntoView () {}, focus () {}
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
function elById (id) {
  if (!byId.has(id)) byId.set(id, makeEl('#' + id));
  return byId.get(id);
}

// Parse the static markup: register ids/classes, and collect the nav tab and
// sub-tab buttons so querySelectorAll('nav#chrome .tab') returns something
// the page can attach its click handlers to (and the check can click).
const tabs = [], subs = [];
const tagRe = /<([a-zA-Z]+)((?:[^>"']|"[^"]*"|'[^']*')*)>/g;
let tm;
while ((tm = tagRe.exec(html)) !== null) {
  const attrs = tm[2];
  const idm = attrs.match(/\bid="([^"]+)"/);
  const clm = attrs.match(/\bclass="([^"]*)"/);
  const elm = makeEl(tm[1]);
  if (clm) elm.className = clm[1];
  if (idm) byId.set(idm[1], elm);
  if (tm[1].toLowerCase() === 'button') {
    const cls = clm ? clm[1] : '';
    const dm = attrs.match(/\bdata-dest="([^"]*)"/);
    const pm = attrs.match(/\bdata-path="([^"]*)"/);
    const wm = attrs.match(/\bdata-view="([^"]*)"/);
    if (dm) elm.dataset.dest = dm[1];
    if (pm) elm.dataset.path = pm[1];
    if (wm) elm.dataset.view = wm[1];
    if (/\btab\b/.test(cls)) tabs.push(elm);
    if (/\bsub\b/.test(cls)) subs.push(elm);
  }
}

const document = {
  body: makeEl('body'),
  getElementById: elById,
  querySelectorAll (sel) {
    if (sel === 'nav#chrome .tab') return tabs;
    if (sel.indexOf('.subtabs .sub') >= 0) return subs;
    return [];
  },
  querySelector () { return null; },
  createElement (tag) { return makeEl(tag); },
  createTextNode (t) { const el = makeEl('#text'); el.textContent = String(t); return el; },
  addEventListener () {},
  activeElement: null
};

const errors = [];
process.on('uncaughtException', (e) => errors.push(String((e && e.stack) || e)));
process.on('unhandledRejection', (e) => errors.push('rejection: ' + String(e)));

const sandbox = {
  console: console,
  setTimeout: setTimeout, clearTimeout: clearTimeout,
  setInterval: setInterval, clearInterval: clearInterval,
  atob: (s) => Buffer.from(s, 'base64').toString('binary'),
  document: document,
  navigator: { userAgent: 'check-activity-capsule' },
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

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  await wait(150);   // let the startup pulls settle

  const problems = [];
  if (errors.length) problems.push('script errors:\n  ' + errors.join('\n  '));

  const capsule = byId.get('capsule');
  const captext = byId.get('capsuletext');
  const capring = byId.get('capring');
  const ringfg = byId.get('ringfg');
  const badge = byId.get('libbadge');
  const libtab = tabs.find(function (t) { return t.dataset.dest === '0'; });

  if (!capsule || !captext || !capring || !badge || !libtab)
    problems.push('capsule / ring / badge / libtab missing from the parsed markup');
  if (capsule && capsule.hidden !== true)
    problems.push('capsule should start hidden when idle');

  const fire = (a) => vm.runInContext(
    'window.__fireActivity(' + JSON.stringify(a) + ')', sandbox);

  // one job in flight, determinate percentage
  fire({ now: 1000, active: 1, unseenLanded: 0, entries: [
    { job: 'j1', title: 'Pluck 14', phase: 'rendering', pct: 42,
      startedMs: 900, updatedMs: 1000 } ] });
  await wait(60);
  if (capsule && capsule.hidden !== false)
    problems.push('capsule not visible while a job is rendering');
  if (captext && !(/Rendering/.test(captext.textContent) && /Pluck 14/.test(captext.textContent)))
    problems.push('one job should read "Rendering <title>", got '
      + JSON.stringify(captext && captext.textContent));
  if (capring && capring.classList.contains('indet'))
    problems.push('ring should be determinate when pct >= 0');

  // The ring fill must land in the inline style: the stylesheet rule
  // stroke-dashoffset: 50.27 beats a presentation attribute, so a
  // setAttribute write would leave the ring empty in a real browser. The vm
  // harness has no CSS engine, so this reads the inline style the page
  // writes -- pct 40 -> 50.27 * (1 - 0.40) = 30.162.
  fire({ now: 1500, active: 1, unseenLanded: 0, entries: [
    { job: 'j1', title: 'Pluck 14', phase: 'rendering', pct: 40,
      startedMs: 900, updatedMs: 1500 } ] });
  await wait(60);
  const off40 = ringfg ? Number(ringfg.style.strokeDashoffset) : NaN;
  if (!(Math.abs(off40 - 30.16) <= 0.01))
    problems.push('ring inline stroke-dashoffset for pct 40 should be ~30.16, got '
      + JSON.stringify(ringfg && ringfg.style.strokeDashoffset));

  // two jobs in flight: "Rendering 2", indeterminate ring, dot on the tab
  fire({ now: 2000, active: 2, unseenLanded: 0, entries: [
    { job: 'j1', title: 'Pluck 14', phase: 'rendering', pct: 55,
      startedMs: 900, updatedMs: 2000 },
    { job: 'j2', title: 'Pad 3', phase: 'queued', pct: -1,
      startedMs: 1500, updatedMs: 2000 } ] });
  await wait(60);
  if (captext && captext.textContent !== 'Rendering 2')
    problems.push('two jobs should read "Rendering 2", got '
      + JSON.stringify(captext && captext.textContent));
  if (capring && !capring.classList.contains('indet'))
    problems.push('ring should be indeterminate with two or more jobs');
  if (badge && (badge.hidden !== false || !badge.classList.contains('dot')))
    problems.push('Library tab should carry the ring dot (no digit) while jobs are in flight');

  // a landing: "Added", the badge counts it, the tab pulses
  fire({ now: 3000, active: 0, unseenLanded: 1, entries: [
    { job: 'j1', title: 'Pluck 14', phase: 'landed', pct: -1,
      startedMs: 900, updatedMs: 3000, artifact: 'a1' } ] });
  await wait(60);
  if (captext && !/Added/.test(captext.textContent))
    problems.push('capsule should read "Added ..." on landing, got '
      + JSON.stringify(captext && captext.textContent));
  if (badge && (badge.hidden !== false || badge.textContent !== '1'))
    problems.push('badge should show the unseen-landed count 1, got hidden='
      + (badge && badge.hidden) + ' text=' + JSON.stringify(badge && badge.textContent));
  if (libtab && !libtab.classList.contains('pulse'))
    problems.push('Library tab should pulse on landing');

  // opening the Library clears the badge (showLibrary clears unseen natively
  // and pushes activityChanged, which the page answers with getActivity)
  if (libtab) {
    libtab.click();
    await wait(150);
    if (badge && badge.hidden !== true)
      problems.push('badge should clear when the Library is opened');
  }

  // The fold race: a frame that arrives while the capsule is folding (inside
  // the 320 ms) must be shown, not hidden by the fold's timeout. The empty
  // list at now=4300 clears the flash (held since 3000, >= 1.2 s) and starts
  // the fold; the rendering job at now=4400 arrives inside the 320 ms window.
  fire({ now: 4300, active: 0, unseenLanded: 1, entries: [] });
  fire({ now: 4400, active: 1, unseenLanded: 1, entries: [
    { job: 'j3', title: 'Bass 7', phase: 'rendering', pct: 10,
      startedMs: 4400, updatedMs: 4400 } ] });
  await wait(500);   // past the 320 ms fold timeout
  if (capsule && capsule.hidden !== false)
    problems.push('a job arriving during the fold must stay visible (the fold timeout hid it)');
  if (capsule && capsule.classList.contains('fold'))
    problems.push('the show branch must remove the fold class when a new frame arrives');

  // The landing hold: a landing 1.1 s after the flash started must merge into
  // the flash, not restart it, so the flash is never on screen < 1.2 s. With
  // the merge, firstMs stays at the first landing (10000), so the empty list
  // at 12150 (2.15 s later) clears the flash and the capsule folds away.
  fire({ now: 10000, active: 0, unseenLanded: 1, entries: [
    { job: 'jA', title: 'Aaa', phase: 'landed', pct: -1,
      startedMs: 9900, updatedMs: 10000 } ] });
  await wait(60);
  fire({ now: 11100, active: 0, unseenLanded: 2, entries: [
    { job: 'jA', title: 'Aaa', phase: 'landed', pct: -1,
      startedMs: 9900, updatedMs: 10000 },
    { job: 'jB', title: 'Bbb', phase: 'landed', pct: -1,
      startedMs: 11000, updatedMs: 11100 } ] });
  await wait(60);
  fire({ now: 12150, active: 0, unseenLanded: 2, entries: [] });
  await wait(500);
  if (capsule && capsule.hidden !== true)
    problems.push('a landing 1.1 s into a flash must merge, not restart it (the 1.2 s hold was truncated)');

  if (problems.length) {
    console.error('preview activity problems:');
    problems.forEach((p) => console.error('  ' + p));
    process.exit(1);
  }
  console.log('preview loads clean: ' + scripts.length
    + ' script blocks, capsule/badge answer fake activity events, no script errors');
  process.exit(0);
})().catch((e) => { console.error(String((e && e.stack) || e)); process.exit(1); });
LOADER_EOF

if [[ -s tools/shell-preview/shell-preview.html ]]; then
  if node "$TMPD/load-preview.cjs" tools/shell-preview/shell-preview.html; then
    ok "generated HTML loads with no script errors and the capsule/badge respond to fake activity events"
  else
    bad "generated HTML loads with no script errors and the capsule/badge respond to fake activity events"
  fi
else
  bad "generated HTML loads with no script errors and the capsule/badge respond to fake activity events (no preview built)"
fi

# ── 11. the gate and the protected files are unchanged ──────────────────────
if git diff --quiet HEAD -- scripts/gate.sh scripts/null-test.sh \
     docs/BASELINE_FAUSTENGINE_2026-09-07.txt scripts/processor-diff.sh \
     tools/processor-probe/diff_wav.py 2>/dev/null; then
  ok "scripts/gate.sh and the protected files are unchanged"
else
  bad "scripts/gate.sh and the protected files are unchanged"
fi

echo
if (( fail == 0 )); then
  echo "check-activity-capsule: all criteria present"
  exit 0
else
  echo "check-activity-capsule: criteria missing (see FAIL lines)"
  exit 1
fi
