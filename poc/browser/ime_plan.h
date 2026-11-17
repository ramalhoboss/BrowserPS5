// 2026-11-11 01:10: the system keyboard's test in BrowserPS5 RN (make SPEED_TEST=1
// SPEED_PLAN=ime_plan.h; browser-rn/run-feel-rn.sh menu-rn-ime.tar 120 with
// IME=1, which puts /app0/ime_test there: the title then closes the keyboard
// by itself after 4 s). Wikipedia loads; at 14 s and at 36 s the app opens the
// system keyboard for the address as a click would (SP_IME): the pages
// pause, the keyboard opens, closes, the pages are back. The numbers and the
// screens before and after say whether the page lived through it. The
// keyboard drawn under the GPU took the console down on 2026-11-04: this is
// the first run with the pause before it.
#pragma once
#define FEEL_CUSTOM 1
#define SPEED_IME 1
#include "feel_plan.h"
static const char* const SPEED_URLS[] = {"https://en.wikipedia.org/wiki/PlayStation_5"};
#define SPEED_PLAN(X)   X(0, SP_LOAD, 0)   X(12, SP_FEEL, 0)   X(14, SP_IME, 0)   X(26, SP_FEEL, 0)   X(27, SP_SCROLL, 0)   X(33, SP_SCROLL_READ, 0)   X(34, SP_SHOT, 0)   X(36, SP_IME, 0)   X(48, SP_FEEL, 0)   X(49, SP_SHOT, 0)   X(51, SP_DONE, 0) // end of the plan
