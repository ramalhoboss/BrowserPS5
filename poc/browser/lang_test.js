// lang.js's checks (docs/13-idioma.md), with node (make test runs it when
// node is there): no English text is itself a Portuguese key with another
// English (the pages would swap it back and forth), every pattern compiles
// and comes out, T() puts a text together, and every Portuguese text of the
// pages listed in lang_keys.txt (if given) has its English.
'use strict';
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'lang.js'), 'utf8');
const sandbox = new Function(src + '\nreturn {LANG_EN, LANG_EN_PATTERNS, langEn, T, langDay, setLangOnly: l => { LANG = l; }};');
const L = sandbox();
let pass = 0, fail = 0;
function check(ok, what) {
  if (ok) pass++; else { fail++; console.log('FAIL: ' + what); }
}

for (const [pt, en] of Object.entries(L.LANG_EN)) {
  const again = L.langEn(en);
  check(again === null || again === en, 'loop: ' + pt + ' -> ' + en + ' -> ' + again);
  check(en.trim() === en, 'spaces at the ends: ' + pt);
}
check(L.langEn('Entrar como joão') === 'Sign in as joão', 'pattern %s');
check(L.langEn('3 de 5 (o máximo)') === '3 of 5 (the most)', 'pattern %d');
check(L.langEn('Rio de Janeiro') === null, 'no pattern on a name');
check(L.langEn('  Favoritos ') === '  Favorites ', 'spaces kept');
check(L.langEn('Funciona em: todos os sites') === 'Runs on: all sites', 'part translated');
check(L.langEn('A transferir: 45%') === 'Downloading: 45%', 'percent');
check(L.langEn('Disco externo 2') === 'External drive 2', 'disk');
check(L.T('de %s', 'x.pt') === 'de x.pt', 'T in Portuguese');
check(L.T('phone|Ligado') === 'Ligado', 'T context in Portuguese');
L.setLangOnly('en');
check(L.T('de %s', 'x.pt') === 'from x.pt', 'T in English');
check(L.T('%s% · %s de %s', 45, '1 MB', '3 MB') === '45% · 1 MB of 3 MB', 'T with args');
check(L.T('phone|Ligado') === 'Connected', 'T context in English');
check(L.T('Ligado') === 'On', 'T without context');
check(L.T('/data/a|b.mp4') === '/data/a|b.mp4', 'T on a path');
check(L.langDay(new Date(2026, 2, 7)) === 'Saturday, 7 March', 'date in English');
L.setLangOnly('pt');
check(L.langDay(new Date(2026, 2, 7)) === 'sábado, 7 de março', 'date in Portuguese');

const keys = process.argv[2];
if (keys) {
  for (const line of fs.readFileSync(keys, 'utf8').split('\n')) {
    const t = line.trim();
    if (!t || t.startsWith('#')) continue;
    check(L.langEn(t) !== null, 'no English for: ' + t);
  }
}
console.log((fail ? 'FAIL' : 'PASS') + ': lang.js, ' + pass + ' checks passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
