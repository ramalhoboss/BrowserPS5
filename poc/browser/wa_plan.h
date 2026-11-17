// 2026-11-10 21:35: WhatsApp Web's probe plan for the speed test's code
// (make SPEED_TEST=1 SPEED_PLAN=wa_plan.h): opens web.whatsapp.com (the
// user's login is in RN's data), and every 20-30 s writes what the page waits
// on: its workers (messages in/out), WebSockets, IndexedDB opens and
// transactions still open, Web Locks, the main thread's lag, whether the chat
// list (#pane-side) is there. Quits after 190 s. WhatsApp stayed on "A
// carregar as suas conversas" in RN after the worker RunLoop fix.
// wa_plan_moves.h: the same with the page moved to a process of its own.
#pragma once
enum { SP_LOAD, SP_TIMING, SP_SCROLL, SP_SCROLL_READ, SP_BENCH_SET, SP_BENCH_RUN, SP_DONE, SP_SHOT };
static const char* const SPEED_URLS[] = {"https://web.whatsapp.com/"};
static const char wa_start_js[] = R"JS((() => {
  if (window !== window.top || window.__wa)
    return;
  const s = window.__wa = {w: 0, wErr: 0, wIn: 0, wOut: 0, wLastIn: 0, wUrls: [],
    ws: 0, wsOpen: 0, wsCloses: [], wsIn: 0, wsOut: 0, wsLastIn: 0,
    idb: 0, idbOk: 0, idbErr: 0, idbBlocked: 0, idbNames: [],
    tx: 0, txDone: 0, txAbort: 0, txErrs: [], txOpen: {},
    locks: 0, locksDone: 0, lag: 0, rej: [], errs: []};
  const now = () => Math.round(performance.now() / 1000);
  const short = (m) => String(m).replace(/\s+/g, ' ').slice(0, 90);
  const keep = (list, m) => { if (list.indexOf(m) < 0) { list.push(m); if (list.length > 3) list.shift(); } };
  const W = window.Worker;
  if (W) {
    const pm = W.prototype.postMessage;
    W.prototype.postMessage = function() { s.wOut++; return pm.apply(this, arguments); };
    window.Worker = new Proxy(W, {construct(t, args, nt) {
      const w = Reflect.construct(t, args, nt);
      s.w++;
      if (s.wUrls.length < 6)
        s.wUrls.push(short(args[0]).slice(-50) + (args[1] && args[1].type ? '/' + args[1].type : ''));
      w.addEventListener('error', (e) => { s.wErr++; keep(s.errs, 'worker: ' + short(e.message || e.filename || 'error')); });
      w.addEventListener('messageerror', () => { s.wErr++; keep(s.errs, 'worker messageerror'); });
      w.addEventListener('message', () => { s.wIn++; s.wLastIn = now(); });
      return w;
    }});
  }
  const S = window.WebSocket;
  if (S) {
    const send = S.prototype.send;
    S.prototype.send = function() { s.wsOut++; return send.apply(this, arguments); };
    window.WebSocket = new Proxy(S, {construct(t, args, nt) {
      const w = Reflect.construct(t, args, nt);
      s.ws++;
      w.addEventListener('open', () => s.wsOpen++);
      w.addEventListener('close', (e) => keep(s.wsCloses, e.code + '@' + now()));
      w.addEventListener('message', () => { s.wsIn++; s.wsLastIn = now(); });
      return w;
    }});
  }
  if (window.IDBFactory) {
    const open = IDBFactory.prototype.open;
    IDBFactory.prototype.open = function(name) {
      const r = open.apply(this, arguments);
      s.idb++;
      if (s.idbNames.length < 12 && s.idbNames.indexOf(name) < 0)
        s.idbNames.push(short(name).slice(0, 24));
      r.addEventListener('success', () => s.idbOk++);
      r.addEventListener('error', () => { s.idbErr++; keep(s.errs, 'idb open ' + name + ': ' + short(r.error && r.error.message)); });
      r.addEventListener('blocked', () => s.idbBlocked++);
      return r;
    };
    const transaction = IDBDatabase.prototype.transaction;
    IDBDatabase.prototype.transaction = function(stores, mode) {
      const t = transaction.apply(this, arguments);
      const key = this.name + ':' + (mode || 'readonly')[4];
      s.tx++;
      s.txOpen[key] = (s.txOpen[key] || 0) + 1;
      const end = () => { s.txOpen[key]--; };
      t.addEventListener('complete', () => { s.txDone++; end(); });
      t.addEventListener('abort', () => { s.txAbort++; end(); keep(s.txErrs, key + ' ' + short(t.error && t.error.name)); });
      return t;
    };
  }
  if (navigator.locks && navigator.locks.request) {
    const request = navigator.locks.request;
    navigator.locks.request = function() {
      s.locks++;
      const p = request.apply(this, arguments);
      p.then(() => s.locksDone++, () => s.locksDone++);
      return p;
    };
  }
  window.addEventListener('unhandledrejection', (e) => {
    const r = e.reason;
    keep(s.rej, short(r && r.name ? r.name + ': ' + r.message : r));
  });
  window.addEventListener('error', (e) => keep(s.errs, short(e.message)));
  let last = performance.now();
  setInterval(() => {
    const t = performance.now();
    s.lag = Math.max(s.lag, Math.round(t - last - 1000));
    last = t;
  }, 1000);
  window.__wa_report = () => {
    const open = Object.keys(s.txOpen).filter((k) => s.txOpen[k] > 0).map((k) => k + '=' + s.txOpen[k]).join(',');
    const sw = navigator.serviceWorker ? (navigator.serviceWorker.controller ? 'ctl' : 'none') : 'no';
    return 't=' + now() + ' coi=' + self.crossOriginIsolated + ' sab=' + typeof SharedArrayBuffer +
      ' workers=' + s.w + '[' + s.wUrls.join(' ') + '] werr=' + s.wErr + ' win=' + s.wIn + '@' + s.wLastIn +
      ' wout=' + s.wOut + ' ws=' + s.ws + ' open=' + s.wsOpen + ' closes=' + s.wsCloses.join(',') +
      ' wsin=' + s.wsIn + '@' + s.wsLastIn + ' wsout=' + s.wsOut +
      ' idb=' + s.idb + '/' + s.idbOk + '/' + s.idbErr + '/' + s.idbBlocked + '[' + s.idbNames.join(',') + ']' +
      ' tx=' + s.tx + ' done=' + s.txDone + ' abort=' + s.txAbort + ' txopen={' + open + '} txerr={' + s.txErrs.join(' | ') + '}' +
      ' locks=' + s.locks + '/' + s.locksDone + ' lag=' + s.lag + ' sw=' + sw +
      ' pane=' + (document.querySelector('#pane-side') ? 1 : 0) +
      ' els=' + document.getElementsByTagName('*').length + ' title=' + document.title +
      ' text=[' + short(document.body ? document.body.innerText : '').slice(0, 80) + ']' +
      ' rej={' + s.rej.join(' | ') + '} errs={' + s.errs.join(' | ') + '}';
  };
})())JS";
#define SPEED_START_JS wa_start_js
#define SPEED_TIMING_JS "(function(){return 'timing'})()"
#define SPEED_SCROLL_JS "(function(){return 'scroll'})()"
#define SPEED_SCROLL_READ_JS "(function(){return 'scroll'})()"
#define SPEED_BENCH_RUN_JS "(function(){return 'wa: '+(window.__wa_report?window.__wa_report():'no probe')})()"
#define SPEED_BENCH_JS "(function(){return 'set'})()"
#define SPEED_PLAN(X) \
  X(0, SP_LOAD, 0) \
  X(15, SP_BENCH_RUN, 0) \
  X(30, SP_BENCH_RUN, 0) \
  X(45, SP_BENCH_RUN, 0) \
  X(60, SP_BENCH_RUN, 0) \
  X(80, SP_BENCH_RUN, 0) \
  X(100, SP_BENCH_RUN, 0) \
  X(101, SP_SHOT, 0) \
  X(125, SP_BENCH_RUN, 0) \
  X(150, SP_BENCH_RUN, 0) \
  X(180, SP_BENCH_RUN, 0) \
  X(181, SP_SHOT, 0) \
  X(190, SP_DONE, 0) \
// end of the plan
