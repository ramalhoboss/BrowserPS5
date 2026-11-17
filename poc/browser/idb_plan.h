// 2026-11-11 12:10: how fast the sites' databases write on the console
// (make SPEED_TEST=1 SPEED_PLAN=idb_plan.h; browser-rn/run-feel-rn.sh
// menu-rn-idb.tar 100). The feel test's 4th tab stalls for 3 s to more than
// a minute with WhatsApp syncing: the page's thread waits for the
// NetworkProcess's storage queue. tour_plan.h has the scripts (J_IDB_*).
#pragma once
#define IDB_PLAN 1
#include "tour_plan.h"
