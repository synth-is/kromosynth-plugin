// Renders the shell page standalone in a browser, with a stub bridge.
//
// WHY THIS EXISTS. §6 of the session prompt records, twice, asserting layout
// from source and being wrong both times -- once for justify-between putting a
// lone child at the start, once for a calc() reading with an obvious
// interpretation that was not the right one. A compiler accepting a C++ string
// literal says nothing whatsoever about where flexbox puts things inside it.
//
// So: pull kIndexHtml out of ShellPage.h, rebuild the :root block from
// Palette.h the way the C++ does, and stub window.__JUCE__ so the page has
// something to draw. Then a real browser lays it out and a screenshot settles
// it, with no plugin, no build and no DAW.
//
// The stub answers only what the page asks for at startup. It is NOT a
// simulator: a control that looks right here can still be wrong against the
// real bridge, and anything about audio, drag or the native strip is outside
// what this can tell you.

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const shell = fs.readFileSync(path.join(root, 'src/ShellPage.h'), 'utf8');
const palette = fs.readFileSync(path.join(root, 'src/Palette.h'), 'utf8');

// The page is a raw string literal: R"HTML( ... )HTML".
const m = shell.match(/R"HTML\(([\s\S]*?)\)HTML"/);

if (!m) {
  console.error('could not find the R"HTML(...)HTML" literal in ShellPage.h');
  process.exit(1);
}

let html = m[1];

// Rebuild :root the way Palette.h's emitter does -- var("css-name", constant).
//
// TWO PASSES, because the constants are ALIASES: surfaceDeep = gray950, and
// gray950 is what actually holds the hex. A single pass recovered 8 of 23 vars
// and the guard below is what caught that rather than a page that rendered in
// the wrong colours and looked deliberate.
const raw = {};
for (const line of palette.split('\n')) {
  const d = line.match(/constexpr\s+const\s+char\*\s+(\w+)\s*=\s*("[^"]+"|\w+)\s*;/);
  if (d) raw[d[1]] = d[2];
}

function resolve (token, depth) {
  if (depth > 8) return null;                       // a cycle, not a colour
  if (token === undefined) return null;
  if (token.startsWith('"')) return token.slice(1, -1);

  return resolve(raw[token], depth + 1);
}

const values = {};
for (const name of Object.keys(raw)) {
  const v = resolve(raw[name], 0);
  if (v) values[name] = v;
}

const vars = [];
for (const line of palette.split('\n')) {
  const v = line.match(/var\s*\(\s*"([a-z-]+)"\s*,\s*(\w+)\s*\)/);
  if (v && values[v[2]]) vars.push(`    --${v[1]}: ${values[v[2]]};`);
}

if (vars.length < 18) {
  console.error(`only recovered ${vars.length} palette vars -- the emitter shape must have changed`);
  process.exit(1);
}

const rootBlock = '<style>\n  :root {\n    color-scheme: dark;\n'
  + vars.join('\n')
  + '\n    --font-stack: -apple-system, BlinkMacSystemFont, sans-serif;\n  }\n</style>';

// Sample rows with the shapes that actually matter: a long name that must
// ellipsis, a sub-10s duration that keeps a decimal, one over 10s that should
// not, and a zero duration standing for a header that could not be read.
const stub = [
'<script>',
'var listeners = {};',
'window.__JUCE__ = {',
'  backend: {',
'    emitEvent: function (event, payload) {',
'      var answers = {',
'        getRenders: [',
'          { file: "WCM9MKA 9.0s 4T09A.wav", name: "WCM9MKA 9.0s 4T09A",',
'            seconds: 9.0, bytes: 1728044, modifiedMs: Date.now() },',
'          { file: "4V5JN1R9 4.0s DNCHQ.wav", name: "4V5JN1R9 4.0s DNCHQ",',
'            seconds: 4.0, bytes: 768044, modifiedMs: Date.now() - 6e4 },',
'          { file: "a very long taken sound name that has to be truncated somewhere 8Q2KD.wav",',
'            name: "a very long taken sound name that has to be truncated somewhere 8Q2KD",',
'            seconds: 12.5, bytes: 2400044, modifiedMs: Date.now() - 12e4 },',
'          { file: "unreadable header ZZZ11.wav", name: "unreadable header ZZZ11",',
'            seconds: 0, bytes: 44, modifiedMs: Date.now() - 18e4 }',
'        ],',
'        listInstruments: [',
'          { id: "inst-1", name: "Virtual Instrument #24RM6", kind: "faust",',
'            characterTags: ["textural", "neutral"], regions: 0 },',
'          { id: "inst-2", name: "Virtual Instrument #PN67X", kind: "sfz",',
'            characterTags: ["textural"], regions: 12 }',
'        ],',
'        engineState: {}, getLoadState: {}, getStatus: {},',
'        getSpectrogram: null, chromeHeight: 0,',
'        getAcquisitions: [],',
'        getAuthState: { state: "registered", label: "amplitudesigner" },',
'        getLibraryRoot: "/Users/you/Music/Synth.is/Instruments",',
'        getSurface: { site: false },',
'        hostDrawsChrome: true',
'      };',
'',
'      var value = Object.prototype.hasOwnProperty.call(answers, payload.name)',
'        ? answers[payload.name] : null;',
'',
'      // Replies on __juce__complete, mirroring the real protocol, so the',
'      // page\'s own invoke path is what runs rather than a shortcut.',
'      setTimeout(function () {',
'        (listeners["__juce__complete"] || []).forEach(function (fn) {',
'          fn({ promiseId: payload.resultId, result: value });',
'        });',
'      }, 0);',
'    },',
'    addEventListener: function (name, fn) {',
'      (listeners[name] = listeners[name] || []).push(fn);',
'    },',
'    removeEventListener: function () {}',
'  }',
'};',
'</script>'
].join('\n');

// Stub BEFORE the page's own script, so window.__JUCE__ exists when the page
// reads it at parse time. The palette goes in the head for the same reason the
// C++ substitutes it there.
html = html.replace('</head>', rootBlock + '\n' + stub + '\n</head>');

const out = path.join(__dirname, 'shell-preview.html');
fs.writeFileSync(out, html);

console.log('wrote ' + out);
console.log('  page ' + html.length + ' bytes, ' + vars.length + ' palette vars');
