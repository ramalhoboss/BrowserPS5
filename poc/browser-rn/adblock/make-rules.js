// node make-rules.js [--all] [--commit <sha>]     (2026-11-14, on the PC: node on Windows; WSL has none)
//
// The ad blocker's own list: uBlock Origin Lite's
// network rules (github.com/uBlockOrigin/uBOL-home, GPL-3.0), downloaded at one commit of its data and
// converted to WebKit's content rules by dnr-to-webkit.js (from Parallax Browser, GPL-3.0-or-later).
//
//   out/adblock/content-rules.json   the rules, as WKUserContentExtensionStoreCompile() takes them
//   out/adblock/info.txt             what they were made from, how many, their sha256
//   out/adblock-rn.tar               both, for /user/homebrew/browserps5-rn/adblock/ (install_tar, the release)
//
// By default the lists about advertisements: ublock-filters, easylist, pgl (Peter Lowe's). --all adds the
// others uBO Lite turns on by default (easyprivacy, ublock-badware, urlhaus-full): twice the rules, and
// WebKit compiles a list on the console itself, on one core (Parallax measured 44-67 s and 560-690 MB
// for all of them). Nothing is taken from this machine: only the files named below are fetched.
'use strict';
const fs = require('fs');
const path = require('path');
const https = require('https');
const crypto = require('crypto');
const {execFileSync} = require('child_process');
const DNRToWebKit = require('./dnr-to-webkit.js');
const Cosmetic = require('./make-cosmetic.js');

const args = process.argv.slice(2);
const all = args.includes('--all');
const at = args.indexOf('--commit');
// The commit of uBOL-home's main the list is made from (2026-11-13: uBO Lite 2026.1110.1816).
const commit = at >= 0 ? args[at + 1] : 'a58a26f1f76c9d880f406c53cbe4cb7f3e284e36';
const base = 'https://raw.githubusercontent.com/uBlockOrigin/uBOL-home/' + commit + '/chromium/';
const wanted = all ? null : ['ublock-filters', 'easylist', 'pgl'];
const out = path.join(__dirname, '..', 'out', 'adblock');

function get(url) {
  return new Promise((resolve, reject) => {
    https.get(url, {headers: {'User-Agent': 'make-rules'}}, (r) => {
      if (r.statusCode === 404) {
        r.resume();
        resolve(null);
        return;
      }
      if (r.statusCode !== 200) {
        r.resume();
        reject(new Error(url + ': HTTP ' + r.statusCode));
        return;
      }
      const parts = [];
      r.on('data', (d) => parts.push(d));
      r.on('end', () => resolve(Buffer.concat(parts)));
    }).on('error', reject);
  });
}
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

(async () => {
  const manifestBytes = await get(base + 'manifest.json');
  if (!manifestBytes) throw new Error('no manifest.json at ' + commit);
  const listed = JSON.parse(DNRToWebKit.rulesetsFromManifest(manifestBytes.toString('utf8')));
  const ids = listed.rulesets.filter((id) => !wanted || wanted.includes(id));
  if (!ids.length) throw new Error('none of the rulesets is in the manifest');
  const sets = {}, files = [];
  for (const id of ids) {
    for (const kind of ['main', 'regex']) {
      const name = 'rulesets/' + kind + '/' + id + '.json';
      const bytes = await get(base + name);
      if (!bytes) continue;  // (no regex part for this ruleset)
      files.push(name + ' ' + bytes.length + ' bytes, sha256 ' + sha256(bytes));
      sets[kind === 'main' ? id : id + '-regex'] = JSON.parse(bytes.toString('utf8'));
    }
  }
  const text = DNRToWebKit.convertJSON(sets, null);
  const line = text.indexOf('\n');
  const stats = JSON.parse(text.slice(0, line)).stats, rules = text.slice(line + 1);
  const count = JSON.parse(rules).length;
  fs.mkdirSync(out, {recursive: true});
  fs.writeFileSync(path.join(out, 'content-rules.json'), rules);
  const info = [
    'uBlock Origin Lite ' + listed.version + ' (uBOL-home ' + commit + '), GPL-3.0: github.com/uBlockOrigin/uBOL-home',
    'rulesets: ' + ids.join(', '),
    'converter: dnr-to-webkit.js version ' + DNRToWebKit.VERSION + ' (Parallax Browser, GPL-3.0-or-later)',
    stats.input + ' rules of uBO Lite -> ' + count + ' rules for WebKit, ' + stats.skipped + ' left out; '
      + rules.length + ' bytes, sha256 ' + sha256(Buffer.from(rules)),
    'made from:',
  ].concat(files.map((f) => '  ' + f));
  // 2026-11-14, the second part (make-cosmetic.js): what uBO Lite hides in the pages, and its strict blocking.
  const more = {}, moreFiles = [];
  for (const id of ids) {
    more[id] = {};
    for (const [key, name] of [['generic', 'rulesets/scripting/generic/' + id + '.js'],
                               ['specific', 'rulesets/scripting/specific/' + id + '.json'],
                               ['strict', 'rulesets/strictblock/' + id + '.json']]) {
      const bytes = await get(base + name);
      if (!bytes) continue;  // (this ruleset has no such part)
      moreFiles.push(name + ' ' + bytes.length + ' bytes, sha256 ' + sha256(bytes));
      const text = bytes.toString('utf8');
      more[id][key] = key === 'generic' ? text : JSON.parse(text);  // (the .js one is read as text, never run)
    }
  }
  const cosmetic = Cosmetic.convert(more);
  const cs = cosmetic.stats;
  const written = {};
  for (const [name, text] of [['cosmetic-rules.json', JSON.stringify(cosmetic.domains)],
                              ['cosmetic-entities.json', JSON.stringify(cosmetic.entities)],
                              ['strict-hosts.txt', cosmetic.strictText]]) {
    fs.writeFileSync(path.join(out, name), text);
    written[name] = text.length + ' bytes, sha256 ' + sha256(Buffer.from(text));
  }
  const info2 = [
    'hiding (make-cosmetic.js): ' + cs.genericSelectors + ' generic selectors (' + cs.genericWithExceptions
      + ' with sites where they stay), ' + cs.specificSelectors + ' selectors of ' + cs.specificPairs
      + ' site-and-selector pairs (' + cs.madePlain + ' of them said as plain CSS here: uBO Lite removes, or looks inside); left out: ' + cs.procedural + ' that need uBO Lite\'s scripts, '
      + cs.droppedByException + ' with an exception on a related site, ' + cs.byExpression + ' by expression, '
      + cs.badHosts + ' host names not taken',
    '  cosmetic-rules.json: ' + cosmetic.domains.length + ' rules, ' + written['cosmetic-rules.json'],
    '  cosmetic-entities.json: ' + cosmetic.entities.length + ' rules, ' + written['cosmetic-entities.json'],
    'strict blocking: ' + cs.strictHosts + ' hosts (' + cs.strictOther + ' rules by address left out); strict-hosts.txt: '
      + written['strict-hosts.txt'],
  ];
  info.splice(4, 0, ...info2);
  info.push(...moreFiles.map((f) => '  ' + f));
  fs.writeFileSync(path.join(out, 'info.txt'), info.join('\n') + '\n');
  console.log(info.slice(0, 4 + info2.length).join('\n'));
  console.log('per ruleset: ' + Object.keys(stats.perRuleset).map((k) => k + ' ' + stats.perRuleset[k].input + ' -> '
    + stats.perRuleset[k].output).join('; '));
  // The tar, as install_tar reads it (ustar, paths from the console's root).
  const stage = fs.mkdtempSync(path.join(require('os').tmpdir(), 'adblock-'));
  const dir = path.join(stage, 'user', 'homebrew', 'browserps5-rn', 'adblock');
  fs.mkdirSync(dir, {recursive: true});
  const packed = ['content-rules.json', 'cosmetic-rules.json', 'cosmetic-entities.json', 'strict-hosts.txt', 'info.txt'];
  for (const name of packed) fs.copyFileSync(path.join(out, name), path.join(dir, name));
  const tar = path.join(out, '..', 'adblock-rn.tar');
  // (--force-local: Git for Windows' tar takes "C:..." for another machine's name)
  execFileSync('tar', ['--force-local', '--format=ustar', '--owner=0', '--group=0', '-cf', tar.split(path.sep).join('/')]
                 .concat(packed.map((name) => 'user/homebrew/browserps5-rn/adblock/' + name)),
               {cwd: stage});
  fs.rmSync(stage, {recursive: true, force: true});
  console.log('adblock-rn.tar: ' + fs.statSync(tar).size + ' bytes');
})().catch((e) => {
  console.error('error: ' + e.message);
  process.exit(1);
});
