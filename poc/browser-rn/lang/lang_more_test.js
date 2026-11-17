// BrowserPS5 RN's languages, checked with node (2026-11-14):  node lang_more_test.js
//   1. With lang-rn.js after lang.js, Portuguese and English are what lang.js alone gives: every text, every
//      pattern, T() and langDay().
//   2. Each table of lang-more.js: its texts are texts the pages have (a key of LANG_EN), none is empty, none is
//      itself a Portuguese text with another translation (the page would swap it again), its patterns are
//      lang.js's with the same %s and %d, its days and months are 7 and 12; and how much of the pages it covers.
'use strict';
const fs = require('fs');
const path = require('path');
const read = f => fs.readFileSync(path.join(__dirname, f), 'utf8');
const base = read('../../browser/lang.js');
const rn = read('../pages/lang-rn.js');
const more = fs.existsSync(path.join(__dirname, '../pages/lang-more.js')) ? read('../pages/lang-more.js') : '';
const tail = '\nif (typeof LANG_LAZY !== "undefined") for (const c of Object.keys(LANG_LAZY)) langWake(c);' +
  '\nreturn {LANG_EN, LANG_EN_PATTERNS, langEn, T, langDay, set: l => { LANG = l; }, more: typeof LANG_MORE === "undefined" ? {} : LANG_MORE};';
const document = {querySelectorAll: () => []};
const A = new Function('document', base + tail)(document);
const B = new Function('document', base + rn + more + tail)(document);
let pass = 0, fail = 0;
function check(ok, what) {
  if (ok) pass++; else { fail++; if (fail <= 40) console.log('FAIL: ' + what); }
}

// 1. Portuguese and English as before
const day = new Date(2026, 2, 7);
for (const lang of ['pt', 'en']) {
  A.set(lang); B.set(lang);
  for (const pt of Object.keys(A.LANG_EN)) {
    check(A.langEn(pt) === B.langEn(pt), lang + ': langEn changed for ' + pt);
    check(A.T(pt) === B.T(pt), lang + ': T changed for ' + pt);
  }
  for (const t of ['Entrar como joão', '3 de 5 (o máximo)', 'Rio de Janeiro', '  Favoritos ', 'Funciona em: todos os sites',
                   'A transferir: 45%', 'Disco externo 2', 'Extraído para /data/x; 3 ficaram de fora', ''])
    check(A.langEn(t) === B.langEn(t), lang + ': langEn changed for the pattern text ' + t);
  check(A.T('de %s', 'x.pt') === B.T('de %s', 'x.pt') && A.T('phone|Ligado') === B.T('phone|Ligado'), lang + ': T with parts');
  check(A.langDay(day) === B.langDay(day), lang + ': langDay ' + B.langDay(day));
}
const rnOwn = Object.keys(B.LANG_EN).filter(k => !(k in A.LANG_EN));
check(rnOwn.length > 0, 'lang-rn.js adds its own texts');

// 2. the tables
const marks = s => (s.match(/%[sd]/g) || []).join('');
const patterns = [...base.matchAll(/\['((?:[^'\\]|\\.)*)',\s*'(?:[^'\\]|\\.)*'\]/g)].map(m => m[1].replace(/\\'/g, "'"));
for (const [code, table] of Object.entries(B.more)) {
  const keys = Object.keys(B.LANG_EN);
  let have = 0;
  B.set(code);
  for (const [pt, to] of Object.entries(table.dict)) {
    check(pt in B.LANG_EN, code + ': not a text of the pages: ' + pt);
    check(typeof to === 'string' && to.trim() === to && to.length > 0, code + ': empty or with spaces at the ends: ' + pt);
    check(marks(pt) === marks(to), code + ': other %s/%d than the Portuguese: ' + pt + ' -> ' + to);
    const again = B.langEn(to);
    check(again === null || again === to, code + ': its translation is a text itself: ' + pt + ' -> ' + to + ' -> ' + again);
  }
  for (const k of keys) have += k in table.dict;
  check(table.patterns.length <= patterns.length, code + ': more patterns than lang.js');
  check(Array.isArray(table.days) && table.days.length === 7 && Array.isArray(table.months) && table.months.length === 12
        && /%w/.test(table.day) && /%d/.test(table.day) && /%m/.test(table.day), code + ': days, months, day');
  check(B.langEn('3 de 5 (o máximo)') !== null && B.langEn('Rio de Janeiro') === null, code + ': patterns work');
  check(B.T('de %s', 'x.pt').includes('x.pt'), code + ': T puts its part in');
  console.log(code + ': ' + have + ' of ' + keys.length + ' texts (' + Math.round(have * 100 / keys.length) + ' %), ' +
              table.patterns.length + ' of ' + patterns.length + ' patterns; "' + B.langDay(day) + '"; "' + B.langEn('Favoritos') +
              '", "' + B.langEn('3 de 5 (o máximo)') + '", "' + B.T('faltam %s', '2 min') + '"');
}
console.log((fail ? 'FAIL' : 'PASS') + ': ' + pass + ' checks, ' + fail + ' failed, ' + Object.keys(B.more).length + ' more language(s)');
process.exit(fail ? 1 : 0);
