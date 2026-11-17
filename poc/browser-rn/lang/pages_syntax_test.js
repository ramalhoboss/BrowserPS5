// Every script of the browser's built pages parses (2026-11-14):  node pages_syntax_test.js <folder of *.built.html>
// A page's script with a syntax error stops the whole page on the console, where nothing says why; here it is
// said at once. (Only that it parses: nothing is run.)
'use strict';
const fs = require('fs');
const path = require('path');
const dir = process.argv[2];
let pages = 0, scripts = 0, fail = 0;
for (const name of fs.readdirSync(dir).filter(n => n.endsWith('.built.html')).sort()) {
  const html = fs.readFileSync(path.join(dir, name), 'utf8');
  pages++;
  let n = 0;
  for (const m of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)) {
    n++;
    scripts++;
    try {
      new Function(m[1]);
    } catch (e) {
      fail++;
      console.log('FAIL: ' + name + ', script ' + n + ' (' + m[1].length + ' bytes): ' + e.message);
    }
  }
  console.log(name + ': ' + n + ' script(s), ' + html.length + ' bytes' + (/langLazy\('nl'/.test(html) ? ', with the languages' : ''));
}
console.log((fail ? 'FAIL' : 'PASS') + ': ' + pages + ' pages, ' + scripts + ' scripts, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
