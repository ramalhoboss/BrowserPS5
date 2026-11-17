/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   The DualSense, felt (docs/07, "O comando"): a short vibration at the
   interface's moments, the adaptive triggers (L2 and R2 click like buttons,
   for the radial menus) and the light bar in the theme's colour. The title
   owns the controller (hello_page/input.c); the UIProcess asks over the link
   (LINK_FEEL, LINK_TRIGGERS, LINK_LIGHT). While the system is over the app
   all of it is off; at the end the light goes back to the system's. */

#ifndef FEEL_H
#define FEEL_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FEEL_TRIGGERS_OFF = 0,
  FEEL_TRIGGERS_CLICK = 1,   // a light click partway down, then free
};

// After input_start(): the controller's handle (input_handle()).
void feel_start(int handle);
// A vibration: the large and small motors (0..255) for ms milliseconds.
void feel_vibrate(int large, int small, int ms);
// FEEL_TRIGGERS_*, both triggers; strength 1..8.
void feel_triggers(int mode, int strength);
// The light bar; on 0: the system's own again.
void feel_light(int on, int r, int g, int b);
// The system is over the app (1) or not any more (0).
void feel_away(int away);
// All off, the light back. At the end.
void feel_stop(void);

#ifdef __cplusplus
}
#endif

#endif
