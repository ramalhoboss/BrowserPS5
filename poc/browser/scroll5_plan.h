// 2026-11-11 02:20: publico.pt's scroll (25 fps after the sticky fix that took
// Wikipedia from 29 to 57): 20 s of it, for BROWSERPS5_PAINT_STATS=1
// (out/web-env-paint.tar) to say what is painted and who asks for layouts.
// Then Wikipedia's default skin scrolled 3 s and a screen: are the sticky
// columns still in place?
#pragma once
#define FEEL_CUSTOM 1
#include "feel_plan.h"
#undef SPEED_SCROLL_JS
#define SPEED_SCROLL_JS "(function(){window.__sc=0;var n=0,t0=performance.now(),last=t0,worst=0,ms=location.host.indexOf('wikipedia')<0?20000:3000;function f(now){n++;if(now-last>worst)worst=now-last;last=now;window.scrollBy(0,12);if(now-t0<ms)requestAnimationFrame(f);else window.__sc='scroll '+location.host+': '+n+' frames in '+Math.round(now-t0)+' ms = '+(n*1000/(now-t0)).toFixed(1)+' fps, worst gap '+Math.round(worst)+' ms, y '+Math.round(window.scrollY)}requestAnimationFrame(f);return 'scroll started'})()"
static const char* const SPEED_URLS[] = {"https://www.publico.pt/", "https://en.wikipedia.org/wiki/PlayStation_5"};
#define SPEED_PLAN(X) X(0, SP_LOAD, 0) X(18, SP_SCROLL, 0) X(40, SP_SCROLL_READ, 0) X(41, SP_SHOT, 0) X(42, SP_LOAD, 1) X(52, SP_SCROLL, 1) X(57, SP_SCROLL_READ, 1) X(58, SP_SHOT, 1) X(60, SP_DONE, 0)
