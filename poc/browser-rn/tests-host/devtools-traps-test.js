// node devtools-traps-test.js: ext/devtools.js (the browser's own developer console) must not spring the traps
// that sites set for a browser's developer tools (2026-11-14: disable-devtool sent the user's pages to
// about:blank). A small page of our own making (no DOM library needed), the console's script loaded into it,
// then what the detectors do: console.log of a thing with a trap, and how often the trap went.
'use strict';
const fs = require('fs');
const path = require('path');

class Element {
  constructor(tag) { this._tag = tag; this._id = ''; this._class = ''; }
  get tagName() { return this._tag.toUpperCase(); }
  get id() { return this._id; }
  set id(v) { this._id = String(v); }
  get className() { return this._class; }
  set className(v) { this._class = String(v); }
}
const written = [];
const page = {
  performance: {now: () => Date.now()},
  Element,
  document: {createElement: (tag) => new Element(tag), body: null, documentElement: null},
  addEventListener() {},
  console: {
    log(...a) { written.push(a.length); }, info() {}, warn() {}, error() {}, debug() {},
  },
};
page.window = page;
page.top = page;

const source = fs.readFileSync(path.join(__dirname, '..', '..', 'browser', 'ext', 'devtools.js'), 'utf8');
const body = source.slice(source.indexOf('function () {'));
const nativeLog = page.console.log;
new Function('window', 'document', 'console', 'performance', 'Element', '(' + body + ').call(window)')(
  page, page.document, page.console, page.performance, Element);

let failed = 0;
function check(what, ok, detail) {
  console.log((ok ? 'PASS ' : 'FAIL ') + what + (detail ? ': ' + detail : ''));
  if (!ok) failed++;
}
const c = page.console;
check('the console is in the page', !!page.__bps5console);

// disable-devtool's DefineId: an element whose own id has a getter.
let idReads = 0;
const div = page.document.createElement('div');
Object.defineProperty(div, 'id', {get() { idReads++; return ''; }});
c.log(div);
check('an element with a getter on its id: not read', idReads === 0, idReads + ' read(s)');

// DateToString, FuncToString, RegToString: toString counted.
let d = 0, f = 0, r = 0;
const date = new Date(); date.toString = () => { d++; return ''; };
const func = function () {}; func.toString = () => { f++; return ''; };
const reg = /./; reg.toString = () => { r++; return ''; };
c.log(date); c.log(func); c.log(reg);
check('toString of a Date, a function, a RegExp: not called', d === 0 && f === 0 && r === 0, d + ', ' + f + ', ' + r);

// An Error with a getter on its stack, an object with a getter among its keys, an array with one.
let stack = 0, key = 0, item = 0;
const err = new Error('x'); Object.defineProperty(err, 'stack', {get() { stack++; return ''; }});
const obj = {a: 1}; Object.defineProperty(obj, 'b', {enumerable: true, get() { key++; return 2; }});
const arr = [1]; Object.defineProperty(arr, 1, {enumerable: true, get() { item++; return 2; }});
c.log(err); c.log(obj); c.log(arr);
check('getters on an Error\'s stack, an object\'s key, an array\'s item: not read', stack === 0 && key === 0 && item === 0,
      stack + ', ' + key + ', ' + item);

// The function itself looks like the engine's.
// (The page here has a function of this file for its console.log; in the engine it is a native one. What
// counts: the text says "[native code]", as a native function's does, and the name and length are its own.)
check('console.log looks native', /\[native code\]/.test(Function.prototype.toString.call(c.log))
      && c.log.name === nativeLog.name && c.log.length === nativeLog.length, Function.prototype.toString.call(c.log).slice(0, 40));
check('what the page wrote still reaches the engine\'s console', written.length === 7, written.length + ' of 7');

// And the console still says something useful.
const plain = page.document.createElement('span'); plain.id = 'main'; plain.className = 'a  b';
c.log(plain, {x: [1, 'two'], when: new Date(0)}, new TypeError('bad'), function named() {});
const last = page.__bps5console.entries[page.__bps5console.entries.length - 1].text;
check('an element, an object, an error and a function are shown', last.indexOf('<span#main.a.b>') === 0
      && last.indexOf("{x: [1, \"two\"], when: Date {}}") > 0 && last.indexOf('TypeError: bad') > 0 && last.indexOf('ƒ named()') > 0, last.slice(0, 120));
const trapped = page.__bps5console.entries.map(e => e.text);
check('a trapped property is shown as (getter)', trapped.some(t => t.indexOf('b: (getter)') >= 0), '');
process.exit(failed ? 1 : 0);
