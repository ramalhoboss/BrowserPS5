// 2026-11-11 01:20: is it the sticky elements? (make SPEED_TEST=1
// SPEED_PLAN=scroll2_plan.h) The same Wikipedia article scrolled for 5 s in
// its old skin (no sticky table of contents) and in the default one (sticky
// contents at the left): scroll_plan.h's stacks showed the page thread
// painting in all 8 samples of the default skin's scroll.
#pragma once
#define FEEL_CUSTOM 1
#include "feel_plan.h"
static const char* const SPEED_URLS[] = {"https://en.wikipedia.org/wiki/PlayStation_5?useskin=vector",
                                         "https://en.wikipedia.org/wiki/PlayStation_5",
                                         "https://www.publico.pt/"};
#define SPEED_PLAN(X) X(0, SP_LOAD, 0) X(12, SP_SCROLL, 0) X(18, SP_SCROLL_READ, 0) X(19, SP_FEEL, 0) X(20, SP_SHOT, 0) X(21, SP_LOAD, 1) X(33, SP_SCROLL, 1) X(39, SP_SCROLL_READ, 1) X(40, SP_FEEL, 1) X(41, SP_LOAD, 2) X(59, SP_SCROLL, 2) X(65, SP_SCROLL_READ, 2) X(66, SP_FEEL, 2) X(67, SP_DONE, 0)
