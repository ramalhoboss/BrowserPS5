// 2026-11-11: no page connects to the console's own services (webkit/security:
// the jailbreak's FTP, ELF loader and websrv take commands from whoever
// connects), and the user still opens them (make SPEED_TEST=1
// SPEED_PLAN=local_plan.h; browser-rn/run-feel-rn.sh menu-rn-plus-local.tar 60).
// tour_plan.h has the scripts (J_LOCAL_*).
#pragma once
#define LOCAL_PLAN 1
#include "tour_plan.h"
