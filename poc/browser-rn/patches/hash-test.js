// node hash-test.js        (2026-11-15)
//
// start-rn.js patchHash() must give the number etaHEN's patches plugin gives a patch
// (etaHEN-Plugins, Plugin_samples/itemzflow_xml_patches/source/game_patch_xml.cpp: djb2_hash(), "hash * 33 ^ c" in
// 64 bits with c a char widened to 32 bits as it is, so a byte over 127 comes with its sign; the text cut at 1023
// bytes by the snprintf before it), or a switch written by the browser would be one the plugin never reads.
// Here: the page's function, taken out of start-rn.js, against the same sum done another way (BigInt), over
// plain texts, texts with bytes over 127, a text longer than the cut, and every patch of the index's files in
// cache/ (make-index.js). The numbers of the first texts were also checked once against the plugin's own C
// (clang, x86-64, char signed), see docs/28.
'use strict';
const fs = require('fs');
const path = require('path');

const page = fs.readFileSync(path.join(__dirname, '..', 'pages', 'start-rn.js'), 'utf8');
const m = /const patchHash = (text => \{[\s\S]*?\n  \});?\n/.exec(page);
if (!m) {
  console.error('FAIL: patchHash() is not where it was in start-rn.js');
  process.exit(1);
}
const patchHash = new Function('return ' + m[1].replace(/\};?\s*$/, '}'))();

const reference = text => {
  let hash = 5381n;
  for (const b of Buffer.from(String(text), 'utf8').subarray(0, 1023)) {
    const c = b < 128 ? BigInt(b) : BigInt(0xffffff00 + b);  // (uint32_t)(signed char)
    hash = ((hash * 33n) & 0xffffffffffffffffn) ^ c;
  }
  return hash.toString(16).padStart(16, '0');
};

const KNOWN = {  // from the plugin's own C on x86-64
  '': '0000000000001505',
  'a': '000000000002b5c4',
  'Bloodborne60 FPS01.09/data/etaHEN/patches/xml/CUSA00900.xmleboot.bin': null,
  'Bloodborneé60 FPS01.09/data/etaHEN/patches/xml/CUSA00900.xmleboot.bin': null,
};
let bad = 0, done = 0;
const check = (text, known) => {
  const a = patchHash(text), b = reference(text);
  done++;
  if (a !== b || (known && a !== known)) {
    bad++;
    console.log('FAIL: ' + JSON.stringify(text.slice(0, 60)) + ': the page ' + a + ', the reference ' + b + (known ? ', known ' + known : ''));
  }
};
for (const [text, known] of Object.entries(KNOWN))
  check(text, known);
check('x'.repeat(2000) + '€');
check('ÿĀ🎮' + 'Pokémon™');
let state = 12345;
for (let i = 0; i < 2000; i++) {  // made-up texts with every kind of byte
  let s = '';
  for (let k = 0; k < 1 + (i % 90); k++) {
    state = (state * 1103515245 + 12345) >>> 0;
    s += String.fromCharCode(32 + (state >>> 16) % (i % 3 ? 95 : 900));
  }
  check(s);
}
const cache = path.join(__dirname, 'cache');
let patches = 0;
if (fs.existsSync(cache)) {
  for (const name of fs.readdirSync(cache).filter(n => n.endsWith('.xml'))) {
    const text = fs.readFileSync(path.join(cache, name), 'utf8');
    for (const tag of text.match(/<Metadata\b[^>]*>/g) || []) {
      const a = n => ((new RegExp('\\b' + n + '="([^"]*)"').exec(tag) || [])[1] || '').replace(/&amp;/g, '&');
      check(a('Title') + a('Name') + a('AppVer') + '/data/etaHEN/patches/xml/CUSA00000.xml' + a('AppElf'));
      patches++;
    }
  }
}
console.log((bad ? 'FAIL' : 'PASS') + ': ' + done + ' texts (' + patches + ' of them the patches of the files in cache/), ' + bad + ' wrong');
console.log('sample: ' + patchHash('Bloodborne60 FPS01.09/data/etaHEN/patches/xml/CUSA00900.xmleboot.bin') + ' ' +
  patchHash('Bloodborneé60 FPS01.09/data/etaHEN/patches/xml/CUSA00900.xmleboot.bin'));
process.exit(bad ? 1 : 0);
