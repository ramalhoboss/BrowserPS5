// 2026-11-11 03:40: BrowserPS5 RN's "tour" (make SPEED_TEST=1
// SPEED_PLAN=tour_plan.h; browser-rn/run-feel-rn.sh menu-rn-tour.tar 200):
// the functions made on the PC during the night of 2026-11-11, each opened as
// a person would, with no person: the reading mode, find in the page, RN's
// keyboard (the address's suggestions, the symbols, the accents), the menu's
// second row and the QR code, a private tab, the translation and a site's
// notification. A screen after each (SHOT: lines, shots.py) and a line of
// what the page says. SP_UI, SP_PAD, SP_*_JS, SP_BAR_KEYS and SP_MENU are
// main.cpp's SPEED_TOUR steps (url = the index in the lists below).
#pragma once
#define FEEL_CUSTOM 1
#define SPEED_TOUR 1
#include "feel_plan.h"
enum { SP_UI = 100, SP_PAD, SP_KEYS_JS, SP_PAGE_JS, SP_MENU_JS, SP_BAR_KEYS, SP_MENU, SP_DIALOG_JS };

static const char* const SPEED_URLS[] = {
  "https://en.wikipedia.org/wiki/PlayStation_5",
  "https://example.com/",
  "https://web.telegram.org/k/",
  "http://example.com/",  // local_plan.h: a plain http page (an https one may not ask http at all)
  "https://proof.ovh.net/files/",  // dl_plan.h: the folder of OVH's test files (the page that reads one of them)
  "http://speedtest.belwue.net/",  // dl_plan.h: the same with no TLS (its /1G; thinkbroadband answered 403)
};

// SP_UI: what the menu's tiles ask for (ui_command()).
enum { U_READER, U_FIND, U_PRIVATE, U_TRANSLATE, U_LOCAL };
static const char* const SPEED_UI[] = {"reader", "find", "newprivate", "translate",
  // local_plan.h: the user's own navigation to a service of the console (the test's own, main.cpp local_test_thread()): opens
  "go?http://127.0.0.1:8099/"};

// SP_PAD: the controller, as the title's input.c sends it.
enum { P_R1, P_CIRCLE, P_L3, P_R3, P_L2, P_SQUARE, P_DOWN, P_CROSS_DOWN, P_CROSS_UP };
static const input_event_t SPEED_PADS[] = {
  {INPUT_FORWARD, 960, 540, 0, 0, 0},
  {INPUT_BACK, 960, 540, 0, 0, 0},
  {INPUT_STICK, 960, 540, -1, 0, 1},
  {INPUT_STICK, 960, 540, 1, 0, 1},
  {INPUT_RADIAL, 960, 540, -1, 0, 1},
  {INPUT_ENTER, 960, 540, 0, 0, 0},
  {INPUT_SCROLL, 960, 540, 0, 120, 1},
  {INPUT_BUTTON_DOWN, 960, 540, 0, 0, 0},
  {INPUT_BUTTON_UP, 960, 540, 0, 0, 0},
};

// SP_KEYS_JS: in the keyboard's page (browser-rn/pages/keyboard.html).
enum { K_TYPE_FIND, K_DONE, K_TYPE_WI, K_STATE, K_CANCEL, K_HOLD_E };
static const char* const SPEED_KEYS_JS[] = {
  "(function(){insert('console');return 'keys: typed, field '+document.querySelector('#field,.field').textContent.slice(0,40)})()",
  "(function(){kb.done();return 'keys: done'})()",
  "(function(){insert('wi');return 'keys: typed wi'})()",
  "(function(){var c=[];document.querySelectorAll('.chip').forEach(function(e){c.push(e.textContent)});"
  "return 'keys: layer '+s.layer+', shift '+s.shift+', '+document.querySelectorAll('.key').length+' keys, suggestions: '+c.join(' | ')})()",
  "(function(){kb.cancel();return 'keys: cancelled'})()",
  // the accents of "e": the highlight on it, cross held
  "(function(){var k=null;document.querySelectorAll('.key').forEach(function(e){if(e.textContent.trim().toLowerCase()==='e'&&!k)k=e});"
  "if(!k)return 'keys: no e key';highlight(k);openPop(k);var v=[];document.querySelectorAll('#pop > *,.pop > *').forEach(function(e){v.push(e.textContent)});"
  "return 'keys: accents of e: '+v.join(' ')})()",
};

// SP_PAGE_JS: in the page.
enum { J_READER, J_PRIVATE, J_NOTIFY, J_ENGINE, J_LINK_DOWN, J_LINK_STATE, J_UPLOAD_ASK, J_UPLOAD_READ, J_ASK, J_ASK_READ, J_IDB_RUN, J_IDB_READ, J_LOCAL_RUN, J_LOCAL_READ, J_LOCAL_PAGE, J_DL_RUN, J_DL_READ, J_LOCAL_OPEN };
static const char* const SPEED_PAGE_JS[] = {
  "(function(){var h=document.getElementById('bps5-reader');if(!h||!h.shadowRoot)return 'reader: not shown';"
  "var w=h.shadowRoot.getElementById('wrap');return 'reader: on, '+w.textContent.length+' characters, '+"
  "w.querySelectorAll('img').length+' pictures, '+w.querySelectorAll('p').length+' paragraphs, letter '+w.style.fontSize})()",
  "(function(){var c='?',l='?';try{document.cookie='bps5tour=1; max-age=600';c=document.cookie||'(none)'}catch(e){c='error '+e}"
  "try{localStorage.setItem('bps5tour','1');l=localStorage.getItem('bps5tour')}catch(e){l='error '+e}"
  "return 'private tab: '+location.host+', cookie '+c+', storage '+l})()",
  "(function(){try{var n=new Notification('BrowserPS5 RN',{body:'As notifica\\u00e7\\u00f5es dos sites chegam \\u00e0 PS5'});"
  "return 'notify: permission '+Notification.permission+', made '+(n?'yes':'no')}catch(e){return 'notify: error '+e}})()",
  // what the engine has (the "plus" build's switches among it)
  "(function(){function has(f){try{return f()?'yes':'no'}catch(e){return 'no'}}"
  "return 'engine: dark scheme '+has(function(){return matchMedia('(prefers-color-scheme: dark)').matches})+"
  "', color-scheme '+has(function(){return CSS.supports('color-scheme','dark')})+"
  "', variable fonts '+has(function(){return CSS.supports('font-variation-settings','\"wght\" 500')})+"
  "', OffscreenCanvas '+has(function(){return typeof OffscreenCanvas==='function'})+"
  "', AudioContext '+has(function(){return window.AudioContext||window.webkitAudioContext})+"
  "', WebGL '+has(function(){return document.createElement('canvas').getContext('webgl')})+"
  "', gamepads '+has(function(){return navigator.getGamepads})+"
  "', serviceWorker '+has(function(){return navigator.serviceWorker})+"
  "', '+navigator.userAgent.slice(-60)})()",
  // the link menu (ext/linkmenu.js): cross going down on a link in view, and not coming up
  "(function(){var a=null;document.querySelectorAll('#mw-content-text p a[href],a[href]').forEach(function(l){if(a)return;var r=l.getBoundingClientRect();"
  "if(/^https?:/.test(l.href)&&r.width>8&&r.top>120&&r.bottom<innerHeight-120&&r.left>0&&r.right<innerWidth)a=l});if(!a)return 'link menu: no link in view';"
  "var r=a.getBoundingClientRect();a.dispatchEvent(new MouseEvent('mousedown',{bubbles:true,cancelable:true,button:0,clientX:r.left+4,clientY:r.top+4,view:window}));"
  "return 'link menu: holding on '+a.href.slice(0,70)+', its script '+(window.__bps5link?'in the page':'NOT in the page')})()",
  "(function(){var h=document.getElementById('bps5-linkmenu');var b=[];if(h)h.shadowRoot.querySelectorAll('button').forEach(function(e){b.push(e.textContent)});"
  "return 'link menu: '+(h?'open: '+b.join(' | '):'closed')+', page '+location.pathname.slice(0,40)})()",
  // a file for the page (main.cpp runOpenPanel): a file field of the test's own, pressed; then what it got
  "(function(){var i=document.createElement('input');i.type='file';i.id='bps5up';document.body.appendChild(i);window.__up='upload: the page got nothing yet';"
  "i.addEventListener('change',function(){var f=i.files[0];if(!f){window.__up='upload: changed, no file';return}"
  "window.__up='upload: the page got '+f.name+', '+f.size+' bytes, type '+(f.type||'none');var r=new FileReader();"
  "r.onload=function(){window.__up+=', read '+r.result.byteLength+' bytes of it'};r.onerror=function(){window.__up+=', reading FAILED: '+r.error};"
  "r.readAsArrayBuffer(f.slice(0,65536))});i.click();return 'upload: the page asks for a file'})()",
  "(function(){return window.__up||'upload: no test in this page'})()",
  // a page's confirm() (main.cpp ask_*; a window only with the interface in a WebProcess of its own: without, "false" at once)
  "(function(){window.__ask='ask: confirm() has not returned yet';setTimeout(function(){var t=performance.now();"
  "var said=confirm('Teste do BrowserPS5 RN: esta janela fecha-se sozinha com OK.');window.__ask='ask: confirm() said '+said+' after '+Math.round(performance.now()-t)+' ms'},0);"
  "return 'ask: the page calls confirm()'})()",
  "(function(){return window.__ask||'ask: no test in this page'})()",
  // the sites' databases (idb_plan.h, 2026-11-11 12:10): how long a write takes, and how long the page's thread waits
  // for its first localStorage/sessionStorage behind a queue of them (the 4th tab's stall with WhatsApp syncing)
  "(function(){window.__idb='idb: running';var t0=performance.now();var q=indexedDB.open('bps5bench',1);"
  "q.onupgradeneeded=function(){q.result.createObjectStore('s')};q.onerror=function(){window.__idb='idb: open failed'};"
  "q.onsuccess=function(){var db=q.result,tOpen=performance.now()-t0,N=300,done=0,t1=performance.now(),val=new Array(2001).join('x'),tL=-1,tS=-1,got='';"
  "function one(i){var tx=db.transaction('s','readwrite');tx.objectStore('s').put(val+i,i);tx.oncomplete=function(){if(++done===N)end(performance.now()-t1)};"
  "tx.onabort=function(){window.__idb='idb: a transaction failed'}}"
  "for(var i=0;i<N;i++)one(i);"
  "setTimeout(function(){var a=performance.now();try{localStorage.setItem('bps5bench','1');got=localStorage.getItem('bps5bench')}catch(e){got='error'}tL=performance.now()-a;"
  "a=performance.now();try{sessionStorage.setItem('bps5bench','1')}catch(e){}tS=performance.now()-a;"
  "window.__idb='idb: running, storage answered after '+Math.round(tL)+' and '+Math.round(tS)+' ms with '+done+' of '+N+' written'},30);"
  "function end(tAll){var t3=performance.now(),tx=db.transaction('s','readwrite'),st=tx.objectStore('s');for(var j=0;j<N;j++)st.put(val+j,1000+j);"
  "tx.oncomplete=function(){var tOne=performance.now()-t3;window.__idb='idb: open '+Math.round(tOpen)+' ms; '+N+' transactions of one write: '+Math.round(tAll)+' ms ('+(tAll/N).toFixed(1)+"
  "' ms each); first localStorage behind them: '+Math.round(tL)+' ms ('+got+'), sessionStorage: '+Math.round(tS)+' ms; one transaction of '+N+' writes: '+Math.round(tOne)+' ms';"
  "db.close();indexedDB.deleteDatabase('bps5bench')}}};return 'idb: started'})()",
  "(function(){return window.__idb||'idb: no test in this page'})()",
  // a site that asks the console's own services (local_plan.h, 2026-11-11; webkit/security): each must be refused
  // (the network process's log says which connections were made and given up: those services are there)
  "(function(){window.__loc='local: running';window.__ws='ws: nothing yet';var out=[];"
  "function t(name,url){return fetch(url,{mode:'no-cors'}).then(function(r){out.push(name+' REACHED ('+r.type+')')},function(){out.push(name+' refused')})}"
  "Promise.all([t('ftp 2121','http://127.0.0.1:2121/'),t('websrv 8080','http://127.0.0.1:8080/'),t('service 8099','http://127.0.0.1:8099/'),t('localhost 8099','http://localhost:8099/'),"
  "t('its own address 8099','http://192.168.1.50:8099/')]).then(function(){window.__loc='local: '+out.join('; ')});"
  "var i=new Image();i.onload=function(){out.push('image REACHED')};i.onerror=function(){out.push('image refused')};i.src='http://127.0.0.1:8099/favicon.ico';"
  "try{var ws=new WebSocket('ws://127.0.0.1:8099/');ws.onopen=function(){window.__ws='ws: REACHED'};ws.onerror=function(){window.__ws='ws: refused'}}catch(e){window.__ws='ws: threw'}"
  "return 'local: started from '+location.origin})()",
  "(function(){return (window.__loc||'local: no test in this page')+'; '+(window.__ws||'')})()",
  "(function(){return 'local page: '+location.href+', title '+document.title+', '+(document.body?document.body.innerText.length:0)+' chars'})()",
  // how fast the engine receives (dl_plan.h, 2026-11-11): a test file read by the page and thrown away, nothing kept,
  // on one connection and then on four at once (BrowserPS5 NK got 1,3 MB/s on one: its HTTP library's 64 KiB window)
  "(function(){window.__dl='dl: running';var here=location.hostname==='127.0.0.1';function run(n,secs){var got=0,t0=performance.now(),cs=[],proto='';"
  "for(var i=0;i<n;i++){(function(){var c=new AbortController();cs.push(c);var u=/belwue/.test(location.hostname)?'/1G':(here?'big?':'1Gb.dat?')+Math.random();"
  "fetch(u,{signal:c.signal,cache:'no-store'}).then(function(r){if(!r.ok)proto+=' HTTP '+r.status;var rd=r.body.getReader();"
  "function pump(){return rd.read().then(function(x){if(x.done)return;got+=x.value.length;return pump()})}return pump()}).catch(function(){})})()}"
  "return new Promise(function(res){setTimeout(function(){var dt=(performance.now()-t0)/1000;cs.forEach(function(c){c.abort()});"
  "var e=performance.getEntriesByType('resource');if(e.length)proto+=' '+(e[e.length-1].nextHopProtocol||'?');"
  "res((got/1048576/dt).toFixed(2)+' MB/s with '+n+' at once ('+Math.round(got/1048576)+' MiB in '+dt.toFixed(1)+' s,'+proto+')')},secs*1000)})}"
  "if(here)run(1,10).then(function(a){window.__dl='dl from the console itself: '+a});"
  "else run(1,12).then(function(a){window.__dl='dl: '+a+'; four running';return run(4,12).then(function(b){window.__dl='dl: '+a+'; '+b})});"
  "return 'dl: started at '+location.href})()",
  "(function(){return window.__dl||'dl: no test in this page'})()",
  // 2026-11-14 (local_plan.h): a site asks the APP for a tab on the console itself, as window.open() does after a
  // press (the app's own loads may connect there: main.cpp site_may_open()). No tab, and nothing at the service.
  "(function(){var f=document.createElement('iframe');f.style.cssText='display:none';"
  "f.src='browserps5:newtab?'+encodeURIComponent('http://127.0.0.1:8099/opened-by-a-site');"
  "(document.body||document.documentElement).appendChild(f);return 'local: a site asked the app for a tab on the console itself'})()",
};

// SP_MENU_JS: in the menu's page.
enum { M_QR, M_STATE };
static const char* const SPEED_MENU_JS[] = {
  "(function(){var t=document.getElementById('qr-tile');if(!t)return 'menu: no QR tile';t.click();"
  "var q=document.getElementById('qr');return 'menu: QR '+(q&&getComputedStyle(q).display!=='none'?'shown':'not shown')+', '+(q?q.querySelectorAll('svg *').length:0)+' shapes'})()",
  "(function(){var n=0,v=0;document.querySelectorAll('.tile').forEach(function(t){n++;if(t.offsetWidth)v++});return 'menu: '+v+' of '+n+' tiles shown'})()",
};

// SP_DIALOG_JS: in the dialog's page (dialog.html, the files mode).
enum { D_STATE, D_GO, D_PICK, D_ASK_STATE, D_ASK_OK };
static const char* const SPEED_DIALOG_JS[] = {
  "(function(){return 'dialog: mode '+st.mode+', folder '+st.dir+', '+document.querySelectorAll('#list .dir').length+' entries ('+"
  "document.querySelectorAll('#list .file').length+' files), places: '+st.places.map(function(p){return p.label}).join(' | ')})()",
  "(function(){browse('/data/homebrew/PPSA97024');return 'dialog: to the folder of the browser'})()",
  // a file that does not change while the page reads it (WebKit refuses one changed after it was chosen: the
  // browser's own logs grow all the time, 2026-11-16 12:27): web-env.txt, else another .txt that is not a log
  "(function(){var f=null,g=null,log=/browser-run|browser-title|web-stderr/;document.querySelectorAll('#list .file').forEach(function(b){"
  "var t=b.textContent;if(!f&&/web-env/.test(t))f=b;if(!g&&/[.]txt/.test(t)&&!log.test(t))g=b});f=f||g;"
  "if(!f)f=document.querySelector('#list .file');if(!f)return 'dialog: no file in '+st.dir;var n=f.textContent;f.click();return 'dialog: picked '+n})()",
  "(function(){return 'dialog: mode '+st.mode+(st.mode==='ask'?', kind '+st.kind+', text '+document.getElementById('ask-text').textContent.slice(0,40):'')})()",
  "(function(){if(st.mode!=='ask')return 'dialog: no question open';document.getElementById('ok').click();return 'dialog: OK pressed'})()",
};

#ifdef DL_PLAN
#define SPEED_PLAN(X) \
  /* with no TLS (belwue), then with it (OVH); from the console itself (U_LOCAL, /big) it was 129 MB/s at 16:51 */ \
  X(0, SP_LOAD, 5) \
  X(8, SP_PAGE_JS, J_DL_RUN) \
  X(34, SP_PAGE_JS, J_DL_READ) \
  X(35, SP_LOAD, 4) \
  X(43, SP_PAGE_JS, J_DL_RUN) \
  X(69, SP_PAGE_JS, J_DL_READ) \
  X(70, SP_SHOT, 0) \
  X(71, SP_DONE, 0)
#elif defined(LOCAL_PLAN)
#define SPEED_PLAN(X) \
  X(0, SP_LOAD, 3) \
  X(13, SP_PAGE_JS, J_LOCAL_RUN) \
  X(19, SP_PAGE_JS, J_LOCAL_READ) \
  X(20, SP_PAGE_JS, J_LOCAL_OPEN) \
  X(24, SP_UI, U_LOCAL) \
  X(32, SP_PAGE_JS, J_LOCAL_PAGE) \
  X(33, SP_SHOT, 0) \
  X(35, SP_DONE, 0)
#elif defined(IDB_PLAN)
#define SPEED_PLAN(X) \
  X(0, SP_LOAD, 1) \
  X(8, SP_PAGE_JS, J_IDB_RUN) \
  X(15, SP_PAGE_JS, J_IDB_READ) \
  X(30, SP_PAGE_JS, J_IDB_READ) \
  X(60, SP_PAGE_JS, J_IDB_READ) \
  X(62, SP_DONE, 0)
#else
#define SPEED_PLAN(X) \
  X(0, SP_LOAD, 0) \
  X(14, SP_FEEL, 0) \
  X(14, SP_PAGE_JS, J_ENGINE) \
  X(15, SP_SHOT, 0) \
  /* the link menu: cross held on a link; circle closes it (and goes back no page) */ \
  X(16, SP_PAGE_JS, J_LINK_DOWN) \
  X(18, SP_PAGE_JS, J_LINK_STATE) \
  X(18, SP_SHOT, 0) \
  X(19, SP_PAD, P_CIRCLE) \
  X(20, SP_PAGE_JS, J_LINK_STATE) \
  /* the reading mode; circle closes it too */ \
  X(21, SP_UI, U_READER) \
  X(24, SP_PAGE_JS, J_READER) \
  X(25, SP_SHOT, 0) \
  X(26, SP_PAD, P_CIRCLE) \
  X(27, SP_PAGE_JS, J_READER) \
  /* find in the page: the keyboard, the text, R1 for the next, circle ends */ \
  X(28, SP_UI, U_FIND) \
  X(30, SP_KEYS_JS, K_TYPE_FIND) \
  X(31, SP_SHOT, 0) \
  X(32, SP_KEYS_JS, K_DONE) \
  X(35, SP_SHOT, 0) \
  X(36, SP_PAD, P_R1) \
  X(38, SP_SHOT, 0) \
  X(39, SP_PAD, P_CIRCLE) \
  /* the keyboard for the address: suggestions, symbols, accents, shift */ \
  X(41, SP_BAR_KEYS, 0) \
  X(43, SP_KEYS_JS, K_STATE) \
  X(43, SP_SHOT, 0) \
  X(44, SP_KEYS_JS, K_TYPE_WI) \
  X(45, SP_KEYS_JS, K_STATE) \
  X(46, SP_SHOT, 0) \
  X(47, SP_PAD, P_L3) \
  X(48, SP_KEYS_JS, K_STATE) \
  X(48, SP_SHOT, 0) \
  X(49, SP_PAD, P_R3) \
  X(50, SP_SHOT, 0) \
  X(51, SP_PAD, P_L3) \
  X(51, SP_PAD, P_L3) \
  X(52, SP_PAD, P_L2) \
  X(52, SP_PAD, P_SQUARE) \
  X(53, SP_KEYS_JS, K_HOLD_E) \
  X(54, SP_KEYS_JS, K_STATE) \
  X(54, SP_SHOT, 0) \
  X(55, SP_KEYS_JS, K_CANCEL) \
  /* the menu's second row and the QR code */ \
  X(57, SP_MENU, 0) \
  X(59, SP_MENU_JS, M_STATE) \
  X(59, SP_SHOT, 0) \
  X(60, SP_MENU_JS, M_QR) \
  X(62, SP_SHOT, 0) \
  X(63, SP_MENU, 0) \
  /* a private tab */ \
  X(65, SP_UI, U_PRIVATE) \
  X(68, SP_LOAD, 1) \
  X(75, SP_PAGE_JS, J_PRIVATE) \
  X(76, SP_SHOT, 0) \
  /* a file for a page: the chooser, a file of the browser's own folder, what the page read */ \
  X(77, SP_PAGE_JS, J_UPLOAD_ASK) \
  X(79, SP_DIALOG_JS, D_STATE) \
  X(79, SP_SHOT, 0) \
  X(80, SP_DIALOG_JS, D_GO) \
  X(82, SP_DIALOG_JS, D_STATE) \
  X(82, SP_SHOT, 0) \
  X(83, SP_DIALOG_JS, D_PICK) \
  X(86, SP_PAGE_JS, J_UPLOAD_READ) \
  /* a page's confirm(): a window with the interface in its own WebProcess (the split test), "false" at once without */ \
  X(87, SP_PAGE_JS, J_ASK) \
  X(89, SP_DIALOG_JS, D_ASK_STATE) \
  X(89, SP_SHOT, 0) \
  X(90, SP_PAD, P_CROSS_DOWN) \
  X(90, SP_PAD, P_CROSS_UP) \
  X(92, SP_PAGE_JS, J_ASK_READ) \
  /* the first tab's page through the translator */ \
  X(93, SP_TAB_GO, 0) \
  X(95, SP_UI, U_TRANSLATE) \
  X(113, SP_FEEL, 0) \
  X(114, SP_SHOT, 0) \
  /* a site's notification (Telegram is one of notify_sites) */ \
  X(115, SP_TAB_NEW, 2) \
  X(139, SP_PAGE_JS, J_NOTIFY) \
  X(142, SP_FEEL, 2) \
  X(143, SP_SHOT, 0) \
  X(145, SP_DONE, 0) \
// end of the plan
#endif
