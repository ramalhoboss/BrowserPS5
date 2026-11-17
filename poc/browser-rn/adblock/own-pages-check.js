// node own-pages-check.js: do the hiding rules for every site (out/adblock/cosmetic-rules.json) hit anything of the
// browser's own pages or of what its scripts put into pages? Class names and ids are taken from the sources as text.
'use strict';
const fs = require('fs'), path = require('path');
const poc = path.join(__dirname, '..', '..');
const d = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'out', 'adblock', 'cosmetic-rules.json'), 'utf8'));
const uni = d.filter((r) => Object.keys(r.trigger).length === 1).map((r) => r.action.selector);
const files = [];
const walk = (dir, okName) => { for (const f of fs.readdirSync(dir)) { const p = path.join(dir, f); const st = fs.statSync(p);
  if (st.isDirectory()) { if (f !== 'node_modules' && f !== 'out') walk(p, okName); } else if (okName(f)) files.push(p); } };
walk(path.join(poc, 'browser'), (f) => /^(start|settings|favorites)\.html$/.test(f) || false);
walk(path.join(poc, 'browser', 'ext'), (f) => f.endsWith('.js'));
walk(path.join(poc, 'browser-rn', 'pages'), (f) => /\.(html|js|css)$/.test(f));
const classes = new Set(), ids = new Set();
for (const f of files) {
  const t = fs.readFileSync(f, 'utf8');
  for (const m of t.matchAll(/class(?:Name)?\s*=\s*["']([^"']+)["']/g)) for (const c of m[1].split(/\s+/)) if (/^[A-Za-z_][\w-]*$/.test(c)) classes.add(c);
  for (const m of t.matchAll(/\bid\s*=\s*["']([A-Za-z_][\w-]*)["']/g)) ids.add(m[1]);
  for (const m of t.matchAll(/classList\.(?:add|toggle)\(\s*["']([\w-]+)["']/g)) classes.add(m[1]);
  for (const m of t.matchAll(/[.#]([A-Za-z_][\w-]*)\s*[{,]/g)) { if (f.endsWith('.css') || f.endsWith('.html')) classes.add(m[1]); }
}
const hits = [];
for (const s of uni) {
  let m;
  if ((m = /^\.([\w-]+)$/.exec(s)) && classes.has(m[1])) hits.push(s);
  else if ((m = /^#([\w-]+)$/.exec(s)) && ids.has(m[1])) hits.push(s);
  else if ((m = /^\[(class|id)\^="([^"]+)"\]$/.exec(s))) { for (const x of (m[1] === 'class' ? classes : ids)) if (x.startsWith(m[2])) hits.push(s + ' ~ ' + x); }
  else if ((m = /^\[(class|id)\*="([^"]+)"\]$/.exec(s))) { for (const x of (m[1] === 'class' ? classes : ids)) if (x.includes(m[2])) hits.push(s + ' ~ ' + x); }
  else if ((m = /^\[(class|id)\$="([^"]+)"\]$/.exec(s))) { for (const x of (m[1] === 'class' ? classes : ids)) if (x.endsWith(m[2])) hits.push(s + ' ~ ' + x); }
}
console.log(files.length + ' files, ' + classes.size + ' class names, ' + ids.size + ' ids, ' + uni.length + ' selectors for every site');
console.log(hits.length ? 'HITS: ' + hits.join(' | ') : 'PASS no rule for every site names anything of the browser\'s own');
