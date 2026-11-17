// node make-bisect.js: parts of the hiding lists as packages of their own (out/adblock-hide-<X>.tar), to find on the
// console which part upsets the engine (2026-11-14: a crash and a hang in show-sites with both lists; none without).
//   A  the rules for every site only        B  the rules with domains only (if-domain, unless-domain)
//   C  the list by address only (yts.*)     D  A without the selectors that use :has()   E  the first 2000 rules of A
'use strict';
const fs = require('fs'), path = require('path'), os = require('os');
const {execFileSync} = require('child_process');
const out = path.join(__dirname, '..', 'out');
const d = JSON.parse(fs.readFileSync(path.join(out, 'adblock', 'cosmetic-rules.json'), 'utf8'));
const e = JSON.parse(fs.readFileSync(path.join(out, 'adblock', 'cosmetic-entities.json'), 'utf8'));
const every = d.filter((r) => Object.keys(r.trigger).length === 1), byDomain = d.filter((r) => Object.keys(r.trigger).length > 1);
const parts = {
  A: [every, []], B: [byDomain, []], C: [[], e],
  D: [every.filter((r) => !r.action.selector.includes(':has(')), []], E: [every.slice(0, 2000), []],
};
for (const k of Object.keys(parts)) {
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'hide-'));
  const dir = path.join(stage, 'user', 'homebrew', 'browserps5-rn', 'adblock');
  fs.mkdirSync(dir, {recursive: true});
  // (a list of 2 bytes, "[]", is not read by the browser: rules_start() wants more than 2)
  fs.writeFileSync(path.join(dir, 'cosmetic-rules.json'), JSON.stringify(parts[k][0]));
  fs.writeFileSync(path.join(dir, 'cosmetic-entities.json'), JSON.stringify(parts[k][1]));
  const tar = path.join(out, 'adblock-hide-' + k + '.tar').split(path.sep).join('/');
  execFileSync('tar', ['--force-local', '--format=ustar', '--owner=0', '--group=0', '-cf', tar,
                       'user/homebrew/browserps5-rn/adblock/cosmetic-rules.json', 'user/homebrew/browserps5-rn/adblock/cosmetic-entities.json'], {cwd: stage});
  fs.rmSync(stage, {recursive: true, force: true});
  console.log(k + ': ' + parts[k][0].length + ' + ' + parts[k][1].length + ' rules, ' + fs.statSync(tar).size + ' bytes');
}
