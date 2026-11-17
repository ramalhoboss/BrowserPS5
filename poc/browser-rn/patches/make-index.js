// node make-index.js        (2026-11-15; BrowserPS5 RN, docs/32: "60 fps patches beside the cheats")
//
// Makes ../pages/patches-index.js: WHICH FILE of the scene's game patch repository
// (github.com/illusionyy/PS-Game-Patch: patches/xml for PS4 games, patches/xml_prospero for PS5 ones, a file by
// game with the ids it serves in <TitleID><ID>...) has patches for WHICH GAME. Only names and ids go into the
// browser: the patch files themselves are read from that repository by the start page when the user opens a
// game's patches (start-rn.js patchesPanel()), and saved for etaHEN's patches plugin only when he turns one on.
// The repository has no licence file: nothing of its files is copied into the browser.
//
// The files are read as TEXT (never run), kept in cache/ by their git hash so a second run fetches only what
// changed. One request to api.github.com for the list, the rest from raw.githubusercontent.com.
'use strict';
const fs = require('fs');
const path = require('path');

const REPO = 'illusionyy/PS-Game-Patch';
const RAW = 'https://raw.githubusercontent.com/' + REPO + '/main/';
const CACHE = path.join(__dirname, 'cache');
const OUT = path.join(__dirname, '..', 'pages', 'patches-index.js');

const get = async (url, json) => {
  const r = await fetch(url, {headers: {'User-Agent': 'patches-index', 'Accept': json ? 'application/vnd.github+json' : '*/*'}});
  if (!r.ok)
    throw new Error(url + ': HTTP ' + r.status);
  return json ? r.json() : r.text();
};

(async () => {
  fs.mkdirSync(CACHE, {recursive: true});
  const head = await get('https://api.github.com/repos/' + REPO + '/commits/main', true);
  const tree = await get('https://api.github.com/repos/' + REPO + '/git/trees/' + head.sha + '?recursive=1', true);
  if (tree.truncated)
    throw new Error('the list of files came cut');
  const entries = tree.tree.filter(e => e.type === 'blob' && /^patches\/xml(_prospero)?\/[A-Za-z0-9_.-]+\.xml$/.test(e.path))
    .sort((a, b) => (a.path < b.path ? -1 : 1));
  const files = [], ids = {};
  let fetched = 0, patches = 0;
  for (const e of entries) {
    const kept = path.join(CACHE, e.sha + '.xml');
    let text;
    if (fs.existsSync(kept)) {
      text = fs.readFileSync(kept, 'utf8');
    } else {
      text = await get(RAW + e.path.split('/').map(encodeURIComponent).join('/'), false);
      fs.writeFileSync(kept, text);
      fetched++;
    }
    const name = e.path.slice('patches/'.length);
    const mine = new Set();
    for (const m of text.matchAll(/<ID>\s*([A-Z]{4}[0-9]{5})\s*<\/ID>/g))
      mine.add(m[1]);
    const count = (text.match(/<Metadata\b/g) || []).length;
    if (!mine.size || !count)
      continue;
    patches += count;
    const at = files.push(name) - 1;
    for (const id of mine)
      (ids[id] = ids[id] || []).push(at);
  }
  const sorted = {};
  for (const id of Object.keys(ids).sort())
    sorted[id] = ids[id];
  const date = String(head.commit.committer.date).slice(0, 10);
  const out = '// Made by poc/browser-rn/patches/make-index.js: which file of github.com/' + REPO + ' (commit ' + head.sha.slice(0, 10) +
    ', ' + date + ')\n// has patches for which game. Names and ids only; the files are read from there when the user asks.\n' +
    'window.BPS5_PATCH_INDEX = ' + JSON.stringify({at: date, files, ids: sorted}) + ';\n';
  fs.writeFileSync(OUT, out);
  console.log(files.length + ' files with patches (' + entries.length + ' in the repository, ' + fetched + ' fetched now), ' +
    Object.keys(sorted).length + ' game ids, ' + patches + ' patches; ' + out.length + ' bytes -> ' + path.relative(process.cwd(), OUT));
})().catch(e => { console.error('FAILED: ' + e.message); process.exit(1); });
