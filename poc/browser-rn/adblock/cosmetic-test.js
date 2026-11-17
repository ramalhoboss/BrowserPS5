// node cosmetic-test.js: make-cosmetic.js's own pieces, and the lists it made (out/adblock), looked at on the PC.
'use strict';
const fs = require('fs'), path = require('path'), assert = require('assert');
const C = require('./make-cosmetic.js');
let n = 0;
const ok = (what, cond) => { assert(cond, what); n++; };
const NL = String.fromCharCode(92) + 'n';  // a backslash and an n, as uBO Lite's files have them inside their strings
const LF = String.fromCharCode(10);
ok('literal: a map', JSON.stringify(C.literal('x' + LF + 'const lowlyGeneric = new Map(/* 2 */[[1,"#a"],[2,".b,' + NL + '.c"]]);' + LF, 'lowlyGeneric'))
  === JSON.stringify([[1, '#a'], [2, '.b,' + LF + '.c']]));
ok('literal: a string', C.literal('const highlyGeneric = /* 1 */"A,' + NL + 'B";' + LF, 'highlyGeneric') === 'A,' + LF + 'B');
ok('related: same', C.related('a.com', 'a.com'));
ok('related: under', C.related('x.a.com', 'a.com') && C.related('a.com', 'x.a.com'));
ok('related: not', !C.related('ba.com', 'a.com') && !C.related('a.com', 'b.com'));
ok('related: entity and name', C.related('yts.*', 'yts.mx') && C.related('www.yts.co.uk', 'yts.*') && !C.related('yts.*', 'myts.mx'));
ok('related: entities', C.related('yts.*', 'a.yts.*') && !C.related('yts.*', 'xyts.*'));
const re = (h) => new RegExp(C.topUrl(h));
ok('topUrl: name', re('a.com').test('https://a.com/') && re('a.com').test('https://x.a.com:8080/p') && !re('a.com').test('https://ba.com/')
  && !re('a.com').test('https://a.com.evil.org/'));
ok('topUrl: entity', re('yts.*').test('https://yts.mx/') && re('yts.*').test('https://www.yts.co.uk/x') && !re('yts.*').test('https://myts.mx/'));
ok('plainFrom: a removal is hidden', C.plainFrom('{"selector":".x, .y","action":["remove",""]}') === '.x, .y');
ok('plainFrom: has', C.plainFrom('{"selector":"article","tasks":[["has",{"selector":"header:has(.s)"}]]}') === 'article:has(header:has(.s))');
ok('plainFrom: what needs a script stays out', C.plainFrom('{"selector":".x","tasks":[["has-text","Ad"]]}') === null
  && C.plainFrom('{"selector":"a","tasks":[["has",{"selector":"p","tasks":[["has-text","S"]]}]]}') === null
  && C.plainFrom('{"selector":".x","action":["style","top:0"],"cssable":true}') === null && C.plainFrom('{"selector":"x"}') === null);
// A small set through convert().
const out = C.convert({t: {
  generic: 'const lowlyGeneric = new Map(/* 2 */[[1,"#ad"],[2,".b,' + NL + '.c"]]);' + LF + 'const highlyGeneric = /* 1 */"[data-ad]";' + LF
    + 'const exceptions = /* 2 */["#ad",".b"];' + LF + 'const hostnames = /* 2 */["keep.com","ent.*"];' + LF,
  specific: {selectors: ['.s0', '.s1', '{"selector":"x"}', '.s3'], selectorLists: ['0,1', '-2,2', '3', '-4'], selectorListRefs: [0, 1, 2, 3],
             hostnames: ['a.com', 'b.*', 'c.com', 'x.c.com'], regexes: []},
  strict: [{action: {type: 'redirect'}, condition: {regexFilter: '^https?://.*', requestDomains: ['z.com', 'b.net'], resourceTypes: ['main_frame']}},
           {action: {type: 'redirect'}, condition: {regexFilter: '^x', resourceTypes: ['main_frame']}}],
}});
const find = (list, sel) => list.filter((r) => r.action.selector === sel);
ok('generic with a site where it stays', JSON.stringify(find(out.domains, '#ad')[0].trigger['unless-domain']) === '["*keep.com"]');
ok('generic kept away from an entity goes to the other list', find(out.domains, '.b').length === 0
  && find(out.entities, '.b')[0].trigger['unless-top-url'].length === 1);
ok('generic for all', !find(out.domains, '.c')[0].trigger['if-domain'] && !find(out.domains, '[data-ad]')[0].trigger['unless-domain']);
ok('a site\'s own', JSON.stringify(find(out.domains, '.s0')[0].trigger['if-domain']) === '["*a.com"]');
ok('.s1: on a.com; on b.* it is excepted there itself, so not there', find(out.entities, '.s1').length === 0 && find(out.domains, '.s1').length === 1);
ok('the scripted one is left out', out.stats.procedural === 1);
ok('.s3 on c.com is left alone: x.c.com keeps it', find(out.domains, '.s3').length === 0 && out.stats.droppedByException >= 1);
ok('strict hosts, sorted', out.strictText === 'b.net' + LF + 'z.com' + LF && out.stats.strictOther === 1);
// The lists made.
const dir = path.join(__dirname, '..', 'out', 'adblock');
if (fs.existsSync(path.join(dir, 'cosmetic-rules.json'))) {
  const d = JSON.parse(fs.readFileSync(path.join(dir, 'cosmetic-rules.json'), 'utf8'));
  const e = JSON.parse(fs.readFileSync(path.join(dir, 'cosmetic-entities.json'), 'utf8'));
  const bad = new RegExp(':has-text[(]|:matches-css|:upward[(]|:xpath[(]|:remove[(]|:style[(]|:matches-path|:min-text-length|:watch-attr|:others[(]|:matches-attr|:matches-media|:matches-prop');
  ok('domains list: only domain conditions', d.every((r) => !r.trigger['if-top-url'] && !r.trigger['unless-top-url']
    && !(r.trigger['if-domain'] && r.trigger['unless-domain'])));
  ok('entities list: only address conditions', e.every((r) => !r.trigger['if-domain'] && !r.trigger['unless-domain']
    && (r.trigger['if-top-url'] || r.trigger['unless-top-url'])));
  ok('every rule hides', d.concat(e).every((r) => r.action.type === 'css-display-none' && r.action.selector && !r.action.selector.startsWith('{')));
  const odd = d.concat(e).filter((r) => bad.test(r.action.selector));
  ok('no selector of uBO\'s own kind (' + odd.length + (odd[0] ? ': ' + odd[0].action.selector : '') + ')', odd.length === 0);
  const domainOk = new RegExp('^[*][a-z0-9.-]+$');
  ok('domains are lower-case ASCII', d.every((r) => (r.trigger['if-domain'] || r.trigger['unless-domain'] || []).every((x) => domainOk.test(x))));
  const strict = fs.readFileSync(path.join(dir, 'strict-hosts.txt'), 'utf8').split(LF).filter(Boolean);
  ok('strict hosts sorted by bytes, no two alike', strict.every((h, i) => i === 0 || strict[i - 1] < h));
  const longest = d.concat(e).reduce((m, r) => Math.max(m, r.action.selector.length), 0);
  console.log('lists: ' + d.length + ' + ' + e.length + ' rules; for every site ' + d.filter((r) => Object.keys(r.trigger).length === 1).length
    + ', unless-domain ' + d.filter((r) => r.trigger['unless-domain']).length + ', if-domain ' + d.filter((r) => r.trigger['if-domain']).length
    + '; longest selector ' + longest + ' chars; strict hosts ' + strict.length + ' (doubleclick.net there: ' + strict.includes('doubleclick.net') + ')');
}
console.log('PASS ' + n + ' checks');
