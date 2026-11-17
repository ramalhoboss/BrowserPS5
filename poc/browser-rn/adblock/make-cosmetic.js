// make-cosmetic.js: used by make-rules.js (2026-11-14, the ad blocker's second part)
//
// Two more things of uBlock
// Origin Lite's data (github.com/uBlockOrigin/uBOL-home, GPL-3.0), at the same commit as the network rules:
//
//   cosmetic-rules.json     what it hides in the pages, as WebKit content rules (css-display-none): the generic
//                           selectors for every site, with the sites where each one must stay (unless-domain),
//                           and each site's own selectors (if-domain)
//   cosmetic-entities.json  the same for the sites it names without their ending ("yts.*"): WebKit takes those
//                           only as expressions over the page's address (if-top-url, unless-top-url), and one
//                           list cannot have both kinds
//   strict-hosts.txt        its "strict blocking": the hosts whose whole pages are advertisements or worse, one
//                           per line, sorted; the browser itself does not let a page of another site take the
//                           tab there (main.cpp, strict_host())
//
// uBO Lite's files are read as TEXT and DATA only: the .js ones hold four literals, each on a line of its own,
// taken out and given to JSON.parse. Nothing of them is run.
//
// Left out: what needs uBO Lite's own scripts (the selectors written as {"selector":...}: :has-text, :remove,
// :style and the like), its scriptlets, and the few rules by an expression over the host's name. An exception
// uBO Lite makes for a site under another (S hidden on example.com, not on a.example.com) cannot be said in a
// WebKit rule: S is then left alone on the whole of example.com.
'use strict';

const OK_HOST = /^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/;

// "const name = [new Map(] [/* n */] <literal> [)];" on one line of a uBO Lite script: the literal, parsed.
function literal(text, name) {
  const lines = text.split('\n');
  const start = 'const ' + name + ' = ';
  const line = lines.find((l) => l.startsWith(start));
  if (!line) throw new Error('no "' + start + '" line');
  let s = line.slice(start.length).trim();
  s = s.replace(/^new Map\(/, '').replace(/^\/\*[^*]*\*\//, '').replace(/\)?;$/, '');
  return JSON.parse(s);
}

const isEntity = (h) => h.endsWith('.*');
const escapeHost = (h) => h.replace(/\./g, '\\.');
// The page's address at host h or under it; for "name.*", at name.<anything>.
function topUrl(h) {
  return isEntity(h) ? '^[^:]+://([^/:]*\\.)?' + escapeHost(h.slice(0, -2)) + '\\.[^/:]+[:/]'
                     : '^[^:]+://([^/:]*\\.)?' + escapeHost(h) + '[:/]';
}
const under = (child, parent) => child === parent || child.endsWith('.' + parent);
// Is host e (a name, or "name.*") one that pages of host h (a name, or "name.*") are at or under, or the reverse?
function related(h, e) {
  if (h === e) return true;
  const he = isEntity(h), ee = isEntity(e);
  if (!he && !ee) return under(h, e) || under(e, h);
  const stem = (x) => x.slice(0, -2);
  if (he && ee) return under(stem(h), stem(e)) || under(stem(e), stem(h));
  const ent = he ? stem(h) : stem(e), name = he ? e : h;  // "ent.*" against a full name
  const labels = name.split('.');
  for (let cut = 1; cut < labels.length; cut++) {  // name without its last cut labels, as an ending might be
    const head = labels.slice(0, labels.length - cut).join('.');
    if (under(head, ent) || under(ent, head)) return true;
  }
  return false;
}

// A rule uBO Lite gives to its own scripts that plain CSS can say as well, or null: one that removes what it
// names (hidden here instead), and one that hides what has something inside it (:has(), which this WebKit knows).
function plainFrom(s) {
  let o;
  try { o = JSON.parse(s); } catch (e) { return null; }
  if (!o || typeof o.selector !== 'string' || !o.selector) return null;
  const tasks = o.tasks || [];
  if (o.action ? !(o.action[0] === 'remove' && !tasks.length) : !tasks.length) return null;
  let sel = o.selector;
  for (const t of tasks) {
    const inner = t[1];
    if (t[0] !== 'has' || !inner || typeof inner.selector !== 'string' || !inner.selector || (inner.tasks && inner.tasks.length)
        || inner.action || sel.includes(',')) return null;
    sel += ':has(' + inner.selector + ')';
  }
  return sel;
}

function rule(selector, condition) {
  return {trigger: Object.assign({'url-filter': '.*'}, condition || {}), action: {type: 'css-display-none', selector}};
}

// sets: {id: {generic: text of scripting/generic/id.js or null, specific: object of scripting/specific/id.json
// or null, strict: array of strictblock/id.json or null}}
function convert(sets) {
  const stats = {genericSelectors: 0, genericWithExceptions: 0, specificSelectors: 0, specificPairs: 0, procedural: 0,
                 droppedByException: 0, badHosts: 0, byExpression: 0, strictHosts: 0, strictOther: 0, madePlain: 0};
  const generic = new Set();
  const genericExc = new Map();   // selector -> Set(host)
  const hostSel = new Map();      // host -> Set(selector): the site's own
  const hostExc = new Map();      // host -> Set(selector): not hidden there
  const strict = new Set();
  const add = (map, k, v) => { let s = map.get(k); if (!s) map.set(k, s = new Set()); s.add(v); };

  for (const id of Object.keys(sets)) {
    const set = sets[id];
    if (set.generic) {
      for (const pair of literal(set.generic, 'lowlyGeneric')) {
        for (const s of String(pair[1]).split(',\n')) if (s) generic.add(s);
      }
      for (const s of String(literal(set.generic, 'highlyGeneric') || '').split(',\n')) if (s) generic.add(s);
      const exceptions = literal(set.generic, 'exceptions'), hostnames = literal(set.generic, 'hostnames');
      if (exceptions.length !== hostnames.length) throw new Error(id + ': generic exceptions and hostnames differ');
      hostnames.forEach((h, i) => {
        for (const s of exceptions[i].split('\n')) if (s) add(genericExc, s, h);
      });
    }
    if (set.specific) {
      const d = set.specific;
      if (d.selectorListRefs.length !== d.hostnames.length) throw new Error(id + ': specific refs and hostnames differ');
      stats.byExpression += (d.regexes || []).length / 3;
      d.hostnames.forEach((h, i) => {
        for (const n of JSON.parse('[' + d.selectorLists[d.selectorListRefs[i]] + ']')) {
          if (n >= 0) add(hostSel, h, d.selectors[n]);
          else add(hostExc, h, d.selectors[~n]);
        }
      });
    }
    if (set.strict) {
      for (const r of set.strict) {
        const c = r.condition || {};
        const whole = r.action && r.action.type === 'redirect' && c.regexFilter === '^https?://.*'
          && Array.isArray(c.requestDomains) && (c.resourceTypes || []).includes('main_frame')
          && !c.excludedRequestDomains && !c.initiatorDomains && !c.excludedInitiatorDomains;
        if (!whole) { stats.strictOther++; continue; }
        for (const h of c.requestDomains) {
          if (OK_HOST.test(h)) strict.add(h); else stats.badHosts++;
        }
      }
    }
  }

  const domains = [], entities = [];  // the two lists' rules
  const usable = (h) => (isEntity(h) ? OK_HOST.test(h.slice(0, -2)) : OK_HOST.test(h));

  // The generic selectors.
  stats.genericSelectors = generic.size;
  for (const s of [...generic].sort()) {
    const exc = genericExc.get(s);
    if (!exc) { domains.push(rule(s)); continue; }
    stats.genericWithExceptions++;
    const hosts = [...exc].filter((h) => (usable(h) ? true : (stats.badHosts++, false))).sort();
    if (!hosts.length) domains.push(rule(s));
    else if (hosts.some(isEntity)) entities.push(rule(s, {'unless-top-url': hosts.map(topUrl)}));
    else domains.push(rule(s, {'unless-domain': hosts.map((h) => '*' + h)}));
  }

  // Each site's own selectors: by selector, the sites it is hidden on.
  const excHosts = new Map();  // selector -> [host] with an exception for it
  for (const [h, set] of hostExc) for (const s of set) { let a = excHosts.get(s); if (!a) excHosts.set(s, a = []); a.push(h); }
  const selHosts = new Map();
  for (const [h, set] of hostSel) {
    if (!usable(h)) { stats.badHosts++; continue; }
    for (const s of set) {
      const plain = s.startsWith('{') ? plainFrom(s) : s;
      if (!plain) { stats.procedural++; continue; }
      if ((excHosts.get(s) || []).some((e) => related(h, e))) { stats.droppedByException++; continue; }
      if (plain !== s) stats.madePlain++;
      add(selHosts, plain, h);
      stats.specificPairs++;
    }
  }
  stats.specificSelectors = selHosts.size;
  for (const s of [...selHosts.keys()].sort()) {
    const hosts = [...selHosts.get(s)].sort();
    const plain = hosts.filter((h) => !isEntity(h)), ents = hosts.filter(isEntity);
    if (plain.length) domains.push(rule(s, {'if-domain': plain.map((h) => '*' + h)}));
    if (ents.length) entities.push(rule(s, {'if-top-url': ents.map(topUrl)}));
  }

  stats.strictHosts = strict.size;
  // Sorted as strcmp() sorts: by bytes (all of them ASCII).
  const strictText = [...strict].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0)).join('\n') + '\n';
  return {domains, entities, strictText, stats};
}

module.exports = {convert, literal, related, topUrl, plainFrom};
