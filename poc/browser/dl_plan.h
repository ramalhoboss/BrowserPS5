// 2026-11-11: how fast the engine receives, on one connection and on four at
// once (make SPEED_TEST=1 SPEED_PLAN=dl_plan.h; browser-rn/run-feel-rn.sh
// menu-rn-plus-dl.tar 70): are downloads in several parts (BrowserPS5 NK's)
// worth bringing to RN? The page reads a test file and throws it away:
// nothing is kept on the console. tour_plan.h has the scripts (J_DL_*).
#pragma once
#define DL_PLAN 1
#include "tour_plan.h"
