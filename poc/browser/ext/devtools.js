// BrowserPS5's developer console (main.cpp, extensions_apply(); docs/
// 07-navegacao-e-definicoes.md, "Consola de programador"), in the main frame
// from the page's start, with the setting on. It keeps what the page writes
// to its console (log, info, warn, error, debug), its errors and the
// promises nobody caught, the last 300, and R3 shows them in a panel at the
// bottom of the screen (the app calls __bps5console.toggle()). The panel has
// a line for JavaScript, run in the page as if typed in a desktop browser's
// console. Nothing of it leaves the page.
function () {
  'use strict';
  if (window.__bps5console || window.top !== window) return;
  const MAX = 300;
  const entries = [];
  const counts = {error: 0, warn: 0};
  const t0 = performance.now();
  let host = null, list = null, info = null, badges = null, input = null, open = false;

  // 2026-11-14: sites
  // that look for a browser's developer tools (disable-devtool on movy.sx and popcornmovies.st, with
  // url: "about:blank") write to the console a thing with a trap in it, and take the tools for open when
  // the trap is sprung: an element whose own "id" has a getter, an Error whose "stack" has one, an object
  // with a getter among its keys. This console sprang the first by reading v.id to show "<div#id>". So
  // nothing of a page's own making is called here any more: an element's tag, id and class come from the
  // engine's own getters, and a property with a getter of its own is shown as "(getter)", never read.
  const native = (proto, name) => {
    try {
      const d = Object.getOwnPropertyDescriptor(proto, name);
      return d && d.get;
    } catch (e) {
      return null;
    }
  };
  const elementTag = native(Element.prototype, 'tagName'), elementId = native(Element.prototype, 'id'),
        elementClass = native(Element.prototype, 'className');
  const TRAP = {};
  function own(v, key) {  // v[key], or TRAP where the page put a getter there
    const d = Object.getOwnPropertyDescriptor(v, key);
    if (d && (d.get || d.set)) return TRAP;
    return d ? d.value : v[key];
  }
  function fmt(v, depth) {
    depth = depth || 0;
    try {
      if (v === TRAP) return '(getter)';
      if (typeof v === 'string') return depth ? JSON.stringify(v) : v;
      if (v === null || v === undefined || typeof v === 'number' || typeof v === 'boolean' ||
          typeof v === 'bigint' || typeof v === 'symbol') return String(v);
      if (typeof v === 'function') {
        const name = own(v, 'name');
        return 'ƒ ' + (typeof name === 'string' && name ? name : '(anónima)') + '()';
      }
      if (v instanceof Error) {
        const name = own(v, 'name'), message = own(v, 'message'), stack = own(v, 'stack');
        const first = (typeof name === 'string' ? name : 'Error') + ': ' + (typeof message === 'string' ? message : '');
        if (typeof stack !== 'string' || !stack) return first;
        return stack.indexOf(first) === 0 ? stack : first + '\n' + stack;  // WebKit's has no first line
      }
      if (typeof Element !== 'undefined' && v instanceof Element) {
        const tag = elementTag ? elementTag.call(v) : 'element', id = elementId ? elementId.call(v) : '',
              cls = elementClass ? elementClass.call(v) : '';
        return '<' + String(tag).toLowerCase() + (id ? '#' + id : '') +
          (typeof cls === 'string' && cls ? '.' + cls.trim().split(/\s+/).join('.') : '') + '>';
      }
      if (depth > 2) return Array.isArray(v) ? '[…]' : '{…}';
      if (Array.isArray(v)) {
        const items = [];
        for (let i = 0; i < v.length && i < 50; i++) items.push(fmt(own(v, i), depth + 1));
        return '[' + items.join(', ') + (v.length > 50 ? ', … (' + v.length + ')' : '') + ']';
      }
      const all = Object.keys(v), keys = all.slice(0, 30);
      const proto = Object.getPrototypeOf(v), maker = proto ? own(proto, 'constructor') : null;
      const made = typeof maker === 'function' ? own(maker, 'name') : '';
      const name = typeof made === 'string' && made && made !== 'Object' ? made + ' ' : '';
      return name + '{' + keys.map(k => k + ': ' + fmt(own(v, k), depth + 1)).join(', ') +
        (all.length > 30 ? ', …' : '') + '}';
    } catch (e) {
      return '[' + typeof v + ']';
    }
  }

  function add(level, parts, where) {
    const text = parts.map(p => fmt(p)).join(' ').slice(0, 4000);
    const entry = {level, text, where: where || '', t: performance.now() - t0};
    entries.push(entry);
    if (entries.length > MAX) entries.shift();
    if (level === 'error') counts.error++;
    if (level === 'warn') counts.warn++;
    if (open) {
      row(entry);
      while (list.childElementCount > MAX) list.firstElementChild.remove();
      list.scrollTop = list.scrollHeight;
      head();
    }
  }

  // (2026-11-14) A Proxy over the engine's own function, not a function of ours in its place: what a page
  // sees of console.log (its name, its length, its text "[native code]") is the engine's still.
  for (const level of ['log', 'info', 'warn', 'error', 'debug']) {
    const original = console[level];
    if (typeof original !== 'function') continue;
    console[level] = new Proxy(original, {
      apply(target, self, args) {
        try {
          add(level, Array.prototype.slice.call(args));
        } catch (e) {}
        return Reflect.apply(target, self, args);
      }
    });
  }
  window.addEventListener('error', (e) => {
    const target = e.target;
    if (target && target !== window && (target.src || target.href)) {
      add('warn', ['Não carregou: ' + (target.src || target.href)]);
    } else if (e.message || e.error) {
      add('error', [e.error || e.message], e.filename ? e.filename.split('/').pop() + ':' + e.lineno : '');
    }
  }, true);
  window.addEventListener('unhandledrejection', (e) => add('error', ['Promessa rejeitada:', e.reason]));

  const CSS = `
    :host { all: initial; }
    #box { position: fixed; left: 0; right: 0; bottom: 0; height: 42vh; z-index: 2147483646;
      display: flex; flex-direction: column; background: rgba(13, 15, 22, .96); color: #e8eaf0;
      font: 19px/1.45 ui-monospace, Menlo, Consolas, monospace; border-top: 2px solid #5b8cff;
      box-shadow: 0 -18px 48px rgba(0, 0, 0, .55); }
    #head { display: flex; align-items: center; gap: 14px; padding: 10px 18px; background: #161a26;
      font: 600 20px/1.2 system-ui, sans-serif; border-bottom: 1px solid #262b3a; }
    #head b { font-size: 22px; letter-spacing: .3px; }
    .badge { padding: 3px 11px; border-radius: 12px; font-size: 17px; background: #232838; color: #aab1c5; }
    .badge.err { background: rgba(255, 77, 109, .18); color: #ff8aa0; }
    .badge.warn { background: rgba(255, 196, 0, .16); color: #ffd666; }
    #info { flex: 1; color: #8a91a6; font-weight: 400; font-size: 17px; white-space: nowrap; overflow: hidden;
      text-overflow: ellipsis; }
    button { all: unset; cursor: pointer; padding: 6px 16px; border-radius: 10px; background: #262b3a;
      color: #e8eaf0; font: 600 18px/1.2 system-ui, sans-serif; }
    button:hover, button:focus { background: #5b8cff; color: #fff; }
    #list { flex: 1; overflow: auto; padding: 6px 0; }
    .row { display: flex; gap: 14px; padding: 4px 18px; border-bottom: 1px solid rgba(255, 255, 255, .04);
      white-space: pre-wrap; word-break: break-word; }
    .row .t { color: #5d647a; flex: none; width: 82px; text-align: right; }
    .row .m { flex: 1; }
    .row .w { color: #5d647a; flex: none; }
    .warn { background: rgba(255, 196, 0, .07); color: #ffd666; }
    .error { background: rgba(255, 77, 109, .09); color: #ff8aa0; }
    .info .m { color: #8fb3ff; }
    .debug .m { color: #8a91a6; }
    .input .m { color: #8fb3ff; }
    .input .m::before { content: '› '; }
    .result .m { color: #c7f0d0; }
    .result .m::before { content: '‹ '; }
    #line { display: flex; gap: 12px; padding: 10px 18px; background: #161a26; border-top: 1px solid #262b3a; }
    #line input { all: unset; flex: 1; padding: 8px 14px; border-radius: 10px; background: #0d0f16;
      border: 1px solid #2c3244; color: #e8eaf0; font: 19px ui-monospace, Menlo, Consolas, monospace; }
    #line input:focus { border-color: #5b8cff; }
  `;

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function row(entry) {
    const r = el('div', 'row ' + entry.level);
    r.append(el('span', 't', (entry.t / 1000).toFixed(2) + ' s'), el('span', 'm', entry.text));
    if (entry.where) r.append(el('span', 'w', entry.where));
    list.appendChild(r);
  }
  function head() {
    badges.textContent = '';
    badges.append(el('span', 'badge err', counts.error + (counts.error === 1 ? ' erro' : ' erros')),
                  el('span', 'badge warn', counts.warn + (counts.warn === 1 ? ' aviso' : ' avisos')));
    let nodes = 0, requests = 0, load = '';
    try {
      nodes = document.getElementsByTagName('*').length;
      requests = performance.getEntriesByType('resource').length;
      const nav = performance.getEntriesByType('navigation')[0];
      if (nav && nav.loadEventEnd) load = ' · carregou em ' + (nav.loadEventEnd / 1000).toFixed(1).replace('.', ',') + ' s';
    } catch (e) {}
    info.textContent = location.host + ' · ' + nodes.toLocaleString('pt-PT') + ' elementos · ' +
      requests + ' pedidos' + load;
  }
  function run() {
    const code = input.value.trim();
    if (!code) return;
    input.value = '';
    add('input', [code]);
    try {
      const result = (0, eval)(code);
      add('result', [result === undefined ? 'undefined' : result]);
      if (result && typeof result.then === 'function') {
        result.then((v) => add('result', ['(promessa) ', v]), (e) => add('error', ['(promessa) ', e]));
      }
    } catch (e) {
      add('error', [e]);
    }
  }
  function build() {
    host = document.createElement('bps5-console');
    // Open: the app's keyboard finds the line through it (FOCUSED_ELEMENT).
    const root = host.attachShadow({mode: 'open'});
    const style = el('style');
    style.textContent = CSS;
    const box = el('div');
    box.id = 'box';
    const top = el('div');
    top.id = 'head';
    badges = el('span');
    badges.style.cssText = 'display: flex; gap: 8px';
    info = el('span');
    info.id = 'info';
    const clear = el('button', '', 'Limpar');
    clear.onclick = () => {
      entries.length = 0;
      counts.error = counts.warn = 0;
      list.textContent = '';
      head();
    };
    const close = el('button', '', 'Fechar · R3');
    close.onclick = () => toggle();
    top.append(el('b', '', 'Consola'), badges, info, clear, close);
    list = el('div');
    list.id = 'list';
    const line = el('div');
    line.id = 'line';
    input = el('input');
    input.placeholder = 'Escreve JavaScript e carrega em Enter';
    input.setAttribute('autocomplete', 'off');
    input.setAttribute('spellcheck', 'false');
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        run();
      }
    });
    const go = el('button', '', 'Executar');
    go.onclick = run;
    line.append(input, go);
    box.append(top, list, line);
    root.append(style, box);
  }
  function toggle() {
    if (!host) build();
    open = !open;
    if (open) {
      (document.body || document.documentElement).appendChild(host);
      list.textContent = '';
      entries.forEach(row);
      head();
      list.scrollTop = list.scrollHeight;
    } else {
      host.remove();
    }
    return open;
  }

  window.__bps5console = {toggle, entries, counts};
}
