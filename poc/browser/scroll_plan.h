// 2026-11-11 01:00: where a scroll's time goes (make SPEED_TEST=1
// SPEED_PLAN=scroll_plan.h): Wikipedia's PlayStation 5 article scrolled 12 px a
// frame for 40 s (the feel test got 29 fps there against 57 on Google's
// results, with under one core busy). With BROWSERPS5_STACK_DUMPS=24,28,...
// in /app0/web-env.txt (out/web-env-scroll.tar) the title writes the page
// thread's stack 8 times while it scrolls: tests-v1/sym-stall.sh names them.
#pragma once
#define FEEL_CUSTOM 1
#include "feel_plan.h"
#undef SPEED_SCROLL_JS
#define SPEED_SCROLL_JS "(function(){window.__sc=0;var n=0,t0=performance.now(),last=t0,worst=0;function f(now){n++;if(now-last>worst)worst=now-last;last=now;window.scrollBy(0,12);if(now-t0<40000)requestAnimationFrame(f);else window.__sc='scroll '+location.host+': '+n+' frames in '+Math.round(now-t0)+' ms = '+(n*1000/(now-t0)).toFixed(1)+' fps, worst gap '+Math.round(worst)+' ms, y '+Math.round(window.scrollY)}requestAnimationFrame(f);return 'scroll started'})()"
static const char* const SPEED_URLS[] = {"https://en.wikipedia.org/wiki/PlayStation_5"};
#define SPEED_PLAN(X) X(0, SP_LOAD, 0) X(12, SP_FEEL, 0) X(13, SP_SCROLL, 0) X(55, SP_SCROLL_READ, 0) X(56, SP_FEEL, 0) X(57, SP_SHOT, 0) X(59, SP_DONE, 0)
