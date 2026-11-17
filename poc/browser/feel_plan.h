// 2026-11-11 00:30: BrowserPS5 RN's "feel" test (make SPEED_TEST=1
// SPEED_PLAN=feel_plan.h, browser-rn/run-feel-rn.sh), after the user called
// RN "cheio de bugs, lag, freezes": numbers for what a person feels, with no
// one at the TV. Four sites one after the other (load timings, a 5 s scroll,
// the frames the page got and the worst gaps), then Telegram and WhatsApp in
// tabs of their own (the logins are in RN's data) and Wikipedia in a fourth:
// the same numbers with the two messengers behind, tab switches, and the
// JavaScript bench at the end. SP_TAB_NEW/SP_TAB_GO/SP_FEEL are main.cpp's
// SPEED_TABS steps (url = the tab's index for SP_TAB_GO).
#pragma once
#define SPEED_TABS 1
enum { SP_LOAD, SP_TIMING, SP_SCROLL, SP_SCROLL_READ, SP_BENCH_SET, SP_BENCH_RUN, SP_DONE, SP_SHOT,
       SP_TAB_NEW, SP_TAB_GO, SP_FEEL, SP_IME, SP_BAR_FEEL };
#ifndef FEEL_CUSTOM  // ime_plan.h: its own sites and plan, these scripts
static const char* const SPEED_URLS[] = {
  "https://www.google.com/search?q=playstation+5",
  "https://www.youtube.com/",
  "https://en.wikipedia.org/wiki/PlayStation_5",
  "https://www.publico.pt/",
  "https://web.telegram.org/k/",
  "https://web.whatsapp.com/",
};
#endif
// From each page's start: the frames it gets (requestAnimationFrame) and how
// late a 100 ms timer fires (the main thread busy), since the last reading.
static const char feel_start_js[] = R"JS((() => {
  if (window !== window.top || window.__feel)
    return;
  const s = {f: 0, worst: 0, long: 0, lag: 0, lags: 0, t0: performance.now()};
  let last = performance.now();
  const frame = (now) => {
    const d = now - last;
    last = now;
    s.f++;
    if (d > s.worst) s.worst = d;
    if (d > 50) s.long++;
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
  let tick = performance.now();
  setInterval(() => {
    const t = performance.now(), d = t - tick - 100;
    tick = t;
    if (d > s.lag) s.lag = d;
    if (d > 50) s.lags++;
  }, 100);
  window.__feel = () => {
    const now = performance.now(), dt = now - s.t0;
    const r = 'feel ' + location.host + ' (' + (document.hidden ? 'hidden' : 'shown') + '): ' +
      (s.f * 1000 / dt).toFixed(1) + ' frames/s over ' + Math.round(dt / 1000) + ' s, worst frame gap ' +
      Math.round(s.worst) + ' ms, gaps over 50 ms: ' + s.long + ', worst timer lag ' + Math.round(s.lag) +
      ' ms, lags over 50 ms: ' + s.lags + ', nodes ' + document.getElementsByTagName('*').length +
      ', title ' + document.title.slice(0, 30);
    s.f = 0; s.worst = 0; s.long = 0; s.lag = 0; s.lags = 0; s.t0 = now;
    last = now;
    return r;
  };
})())JS";
#define SPEED_START_JS feel_start_js
// The bar's page (SP_BAR_FEEL): how late its own 100 ms timer fired since the
// last reading. With the interface in the pages' WebProcess it waits as they do.
#define SPEED_BAR_FEEL_JS "(function(){var w=window;if(!w.__bf){w.__bf={lag:0,lags:0,t:performance.now(),t0:performance.now()};setInterval(function(){var n=performance.now(),d=n-w.__bf.t-100;w.__bf.t=n;if(d>w.__bf.lag)w.__bf.lag=d;if(d>50)w.__bf.lags++;},100);return 'bar feel: started';}var b=w.__bf,r='bar feel: worst timer lag '+Math.round(b.lag)+' ms, lags over 50 ms: '+b.lags+' in '+Math.round((performance.now()-b.t0)/1000)+' s';b.lag=0;b.lags=0;b.t0=performance.now();return r;})()"
#define SPEED_FEEL_JS "(function(){return window.__feel?window.__feel():'no feel probe in '+location.href})()"
#define SPEED_TIMING_JS "(function(){var t=performance.timing,n=t.navigationStart;function d(x){return x>0?x-n:-1}var f=-1,r=-1;try{var p=performance.getEntriesByType('paint');for(var i=0;i<p.length;i++)if(p[i].name=='first-contentful-paint')f=Math.round(p[i].startTime);r=performance.getEntriesByType('resource').length}catch(e){}return location.host+' ttfb '+d(t.responseStart)+' dcl '+d(t.domContentLoadedEventEnd)+' load '+d(t.loadEventEnd)+' fcp '+f+' resources '+r+' nodes '+document.getElementsByTagName('*').length})()"
#define SPEED_SCROLL_JS "(function(){window.__sc=0;var n=0,t0=performance.now(),last=t0,worst=0;function f(now){n++;if(now-last>worst)worst=now-last;last=now;window.scrollBy(0,12);if(now-t0<5000)requestAnimationFrame(f);else window.__sc='scroll '+location.host+': '+n+' frames in '+Math.round(now-t0)+' ms = '+(n*1000/(now-t0)).toFixed(1)+' fps, worst gap '+Math.round(worst)+' ms, y '+Math.round(window.scrollY)}requestAnimationFrame(f);return 'scroll started'})()"
#define SPEED_SCROLL_READ_JS "(function(){return window.__sc||'scroll not done'})()"
#define SPEED_BENCH_RUN_JS "(function(){try{return window.__bench?window.__bench():'no bench';}catch(e){return 'error '+e;}})()"
#define SPEED_BENCH_JS "window.__bench=function(){var o=[],tot=0;function add(n,f){var a=performance.now();f();var m=performance.now()-a;tot+=m;o.push(n+' '+m.toFixed(1));}add('loop',function(){var s=0;for(var i=0;i<1e7;i++)s+=i;return s;});add('objects',function(){class P{constructor(x,y){this.x=x;this.y=y;}add(o){return new P(this.x+o.x,this.y+o.y);}}var p=new P(0,0),d=new P(1,2);for(var i=0;i<2e6;i++)p=p.add(d);return p.x+p.y;});add('strings',function(){var a=[];for(var i=0;i<2e5;i++)a.push('item-'+i.toString(16));var s=a.join(',');return s.split(',').filter(function(x){return x.indexOf('f')>=0;}).length;});add('json',function(){var obj={list:[]};for(var i=0;i<200;i++)obj.list.push({id:i,name:'n'+i,tags:['a','b','c'],ok:i%2===0});var n=0;for(var j=0;j<2000;j++)n+=JSON.parse(JSON.stringify(obj)).list.length;return n;});add('sort',function(){var a=[];var x=12345;for(var i=0;i<2e5;i++){x=(x*1103515245+12345)&0x7fffffff;a.push(x);}a.sort(function(p,q){return p-q;});return a[0];});return 'bench: '+o.join(', ')+', total '+tot.toFixed(1);};"
#ifndef FEEL_CUSTOM
#define SPEED_PLAN(X) \
  X(0, SP_LOAD, 0) \
  X(1, SP_BAR_FEEL, 0) \
  X(12, SP_TIMING, 0) \
  X(13, SP_SCROLL, 0) \
  X(19, SP_SCROLL_READ, 0) \
  X(20, SP_FEEL, 0) \
  X(20, SP_BAR_FEEL, 0) \
  X(22, SP_LOAD, 1) \
  X(40, SP_TIMING, 1) \
  X(41, SP_SCROLL, 1) \
  X(47, SP_SCROLL_READ, 1) \
  X(48, SP_FEEL, 1) \
  X(48, SP_BAR_FEEL, 0) \
  X(49, SP_SHOT, 1) \
  X(50, SP_LOAD, 2) \
  X(62, SP_TIMING, 2) \
  X(63, SP_SCROLL, 2) \
  X(69, SP_SCROLL_READ, 2) \
  X(70, SP_FEEL, 2) \
  X(70, SP_BAR_FEEL, 0) \
  X(72, SP_LOAD, 3) \
  X(92, SP_TIMING, 3) \
  X(93, SP_SCROLL, 3) \
  X(99, SP_SCROLL_READ, 3) \
  X(100, SP_FEEL, 3) \
  X(100, SP_BAR_FEEL, 0) \
  X(102, SP_TAB_NEW, 4) \
  X(125, SP_FEEL, 4) \
  X(125, SP_BAR_FEEL, 0) \
  X(126, SP_SHOT, 4) \
  X(127, SP_TAB_NEW, 5) \
  X(155, SP_FEEL, 5) \
  X(155, SP_BAR_FEEL, 0) \
  X(156, SP_SHOT, 5) \
  X(158, SP_TAB_NEW, 2) \
  X(172, SP_TIMING, 2) \
  X(173, SP_SCROLL, 2) \
  X(179, SP_SCROLL_READ, 2) \
  X(180, SP_FEEL, 2) \
  X(180, SP_BAR_FEEL, 0) \
  X(182, SP_TAB_GO, 1) \
  X(190, SP_FEEL, 4) \
  X(191, SP_SHOT, 4) \
  X(192, SP_TAB_GO, 2) \
  X(200, SP_FEEL, 5) \
  X(201, SP_SHOT, 5) \
  X(202, SP_TAB_GO, 3) \
  X(206, SP_SCROLL, 2) \
  X(212, SP_SCROLL_READ, 2) \
  X(213, SP_FEEL, 2) \
  X(214, SP_BENCH_SET, 2) \
  X(215, SP_BENCH_RUN, 2) \
  X(222, SP_BENCH_RUN, 2) \
  X(226, SP_TAB_GO, 0) \
  X(230, SP_FEEL, 3) \
  X(231, SP_SHOT, 3) \
  X(233, SP_DONE, 0) \
// end of the plan
#endif
