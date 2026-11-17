// 2026-11-10 21:35: wa_plan.h with WhatsApp's page (COOP+COEP) moved to a
// WebProcess of its own as in v1 (crossOriginIsolated, SharedArrayBuffer):
// main.cpp leaves BROWSERPS5_ISOLATED_STAYS unset when this is defined.
#pragma once
#define SPEED_ISOLATED_MOVES 1
#include "wa_plan.h"
