// 2026-11-10 21:00: the worker probe's plan for the speed test's code
// (make SPEED_TEST=1 SPEED_PLAN=worker_plan.h): opens card_test/wprobe.html
// from worker_probe.elf on 127.0.0.1:8120, reads its results now and then,
// quits after 130 s. Telegram Web's MTProto worker timed out in BrowserPS5
// RN's page process (the title's) and WhatsApp Web stopped at its splash.
#pragma once
enum { SP_LOAD, SP_TIMING, SP_SCROLL, SP_SCROLL_READ, SP_BENCH_SET, SP_BENCH_RUN, SP_DONE, SP_SHOT };
static const char* const SPEED_URLS[] = {"http://127.0.0.1:8120/"};
#define SPEED_TIMING_JS "(function(){return 'timing'})()"
#define SPEED_SCROLL_JS "(function(){return 'scroll'})()"
#define SPEED_SCROLL_READ_JS "(function(){return 'scroll'})()"
#define SPEED_BENCH_RUN_JS "(function(){return 'wprobe: '+(window.__wres||'nothing yet')})()"
#define SPEED_BENCH_JS "(function(){return 'set'})()"
#define SPEED_START_JS ""
#define SPEED_PLAN(X) \
  X(0, SP_LOAD, 0) \
  X(30, SP_BENCH_RUN, 0) \
  X(60, SP_BENCH_RUN, 0) \
  X(90, SP_BENCH_RUN, 0) \
  X(120, SP_BENCH_RUN, 0) \
  X(121, SP_SHOT, 0) \
  X(130, SP_DONE, 0) \
// end of the plan
