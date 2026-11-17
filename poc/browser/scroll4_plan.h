// 2026-11-11 01:50 (second round: 5 no left column, 6 no right column, 7 no headers, 8 no overflow in the
// sticky containers, 9 no will-change/contain/transitions; the first round: 0 32 fps, 1 35, 2 39, 3 59, 4 33).
// 2026-11-11 01:40: what in Wikipedia's default skin halves the scroll's
// frames (make SPEED_TEST=1 SPEED_PLAN=scroll3_plan.h)? The sticky patch gave
// 31 fps against 29. The same article five times, each with one thing taken
// away at the page's start (bps5v in the address): 0 nothing, 1 no sticky or
// fixed positions, 2 scroll-behavior auto, 3 the side columns and the sticky
// header not shown, 4 no IntersectionObserver.
#pragma once
#define FEEL_CUSTOM 1
#include "feel_plan.h"
#undef SPEED_START_JS
static const char scroll3_start_js[] = R"JS((() => {
  if (window !== window.top) return;
  const m = /bps5v=(\d)/.exec(location.search);
  const v = m ? +m[1] : 0;
  const css = {
    5: '.vector-column-start { display: none !important; }',
    6: '.vector-column-end { display: none !important; }',
    7: '.mw-header, .vector-sticky-header-container, .vector-sticky-header, .vector-page-toolbar { display: none !important; }',
    8: '.vector-sticky-pinned-container, .vector-toc, .vector-pinned-container { overflow: visible !important; max-height: none !important; }',
    9: '* { will-change: auto !important; contain: none !important; transition: none !important; animation: none !important; }',
  }[v];
  if (css) {
    const s = document.createElement('style');
    s.textContent = css;
    (document.head || document.documentElement).appendChild(s);
    document.addEventListener('DOMContentLoaded', () => document.head.appendChild(s));
  }
  window.__feel = () => 'variant ' + v;
})())JS";
#define SPEED_START_JS scroll3_start_js
#undef SPEED_SCROLL_JS
#define SPEED_SCROLL_JS "(function(){window.__sc=0;var n=0,t0=performance.now(),last=t0,worst=0;function f(now){n++;if(now-last>worst)worst=now-last;last=now;window.scrollBy(0,12);if(now-t0<5000)requestAnimationFrame(f);else window.__sc='scroll '+location.search+': '+n+' frames in '+Math.round(now-t0)+' ms = '+(n*1000/(now-t0)).toFixed(1)+' fps, worst gap '+Math.round(worst)+' ms, y '+Math.round(window.scrollY)}requestAnimationFrame(f);return 'scroll started'})()"
static const char* const SPEED_URLS[] = {
  "https://en.wikipedia.org/wiki/PlayStation_5?bps5v=5", "https://en.wikipedia.org/wiki/PlayStation_5?bps5v=6",
  "https://en.wikipedia.org/wiki/PlayStation_5?bps5v=7", "https://en.wikipedia.org/wiki/PlayStation_5?bps5v=8",
  "https://en.wikipedia.org/wiki/PlayStation_5?bps5v=9"};
#define SPEED_PLAN(X) X(0, SP_LOAD, 0) X(10, SP_SCROLL, 0) X(16, SP_SCROLL_READ, 0) X(17, SP_LOAD, 1) X(27, SP_SCROLL, 1) X(33, SP_SCROLL_READ, 1) X(34, SP_SHOT, 1) X(35, SP_LOAD, 2) X(45, SP_SCROLL, 2) X(51, SP_SCROLL_READ, 2) X(52, SP_LOAD, 3) X(62, SP_SCROLL, 3) X(68, SP_SCROLL_READ, 3) X(69, SP_SHOT, 3) X(70, SP_LOAD, 4) X(80, SP_SCROLL, 4) X(86, SP_SCROLL_READ, 4) X(88, SP_DONE, 0)
