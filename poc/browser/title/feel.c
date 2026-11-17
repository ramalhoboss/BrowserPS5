/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   The DualSense, felt (feel.h). A thread that sleeps until a vibration's
   end stops it (no spinning: docs, "no spinning threads"). */

#ifndef POC_NAME
#define POC_NAME "title"
#endif

#include "feel.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "poc_log.h"

// libScePad (the SDK has the stubs, not the headers). Vibration and light
// as the public homebrew headers have them (OpenOrbis).
typedef struct pad_vibration {
  uint8_t large, small;
} pad_vibration_t;
typedef struct pad_light {
  uint8_t r, g, b, a;
} pad_light_t;
int scePadSetVibration(int handle, const pad_vibration_t* param);
int scePadSetLightBar(int handle, const pad_light_t* param);
int scePadResetLightBar(int handle);
int scePadSetTriggerEffect(int handle, const void* param);

// The trigger effect: a mask of the triggers (L2 1, R2 2), 7 bytes of
// padding, then a command per trigger, L2's first: a 32-bit mode, 4 bytes
// of padding and the mode's numbers. The weapon mode's are start (2..7),
// end (start+1..8) and strength (0..8): resistance from start to end, then
// it gives, as a click. How long a command is (how much room its numbers
// have) we only know from homebrew notes, so the lengths below are tried in
// turn and the first the system takes is kept (the log says which). If R2
// does not click while L2 does, the length is wrong: docs/07, "O comando".
#define TRIGGER_MODE_OFF    0
#define TRIGGER_MODE_WEAPON 2
static const int command_bytes[] = {56, 64, 20};
#define LAYOUTS (int)(sizeof(command_bytes) / sizeof(command_bytes[0]))

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int handle = -1;
static int away;
static int quit;
static int vibrating;
static struct timespec deadline;
static int trigger_mode, trigger_strength;
static int light_on, light_r, light_g, light_b;
static int layout = -1;
static unsigned errors;

static int
clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static void
note_error(const char* what, int err) {
  if(err && errors++ < 8) {
    POC_LOG("%s: 0x%x", what, err);
  }
}

static void
vibration_locked(int large, int small) {
  pad_vibration_t v = {(uint8_t)clamp(large, 0, 255), (uint8_t)clamp(small, 0, 255)};
  note_error("scePadSetVibration", scePadSetVibration(handle, &v));
}

static void
triggers_locked(int mode, int strength) {
  int err = 0;
  int first = layout >= 0 ? layout : 0;
  int last = layout >= 0 ? layout : LAYOUTS - 1;

  for(int i = first; i <= last; i++) {
    uint8_t param[8 + 2 * 64];
    memset(param, 0, sizeof(param));
    param[0] = 0x01 | 0x02;
    for(int t = 0; t < 2; t++) {
      uint8_t* command = param + 8 + t * command_bytes[i];
      uint32_t m = mode == FEEL_TRIGGERS_CLICK ? TRIGGER_MODE_WEAPON : TRIGGER_MODE_OFF;
      memcpy(command, &m, sizeof(m));
      if(m == TRIGGER_MODE_WEAPON) {
        command[8] = 2;
        command[9] = 5;
        command[10] = (uint8_t)clamp(strength, 1, 8);
      }
    }
    err = scePadSetTriggerEffect(handle, param);
    if(!err) {
      if(layout != i) {
        POC_LOG("trigger effects: commands of %d bytes taken", command_bytes[i]);
      }
      layout = i;
      return;
    }
    POC_LOG("scePadSetTriggerEffect (commands of %d bytes): 0x%x", command_bytes[i], err);
  }
}

static void
light_locked(void) {
  if(light_on) {
    pad_light_t l = {(uint8_t)light_r, (uint8_t)light_g, (uint8_t)light_b, 0};
    note_error("scePadSetLightBar", scePadSetLightBar(handle, &l));
  } else {
    note_error("scePadResetLightBar", scePadResetLightBar(handle));
  }
}

static int
past(const struct timespec* t) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return now.tv_sec > t->tv_sec || (now.tv_sec == t->tv_sec && now.tv_nsec >= t->tv_nsec);
}

static void*
stopper(void* arg) {
  (void)arg;
  pthread_mutex_lock(&lock);
  while(!quit) {
    if(!vibrating) {
      pthread_cond_wait(&cond, &lock);
      continue;
    }
    pthread_cond_timedwait(&cond, &lock, &deadline);
    if(vibrating && past(&deadline)) {
      vibrating = 0;
      if(handle >= 0) {
        vibration_locked(0, 0);
      }
    }
  }
  pthread_mutex_unlock(&lock);
  return 0;
}

void
feel_start(int h) {
  pthread_t thread;
  pthread_attr_t attr;

  pthread_mutex_lock(&lock);
  handle = h;
  pthread_mutex_unlock(&lock);
  if(h < 0) {
    return;
  }
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 128 * 1024);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&thread, &attr, stopper, 0)) {
    POC_LOG("feel: no thread for the vibration's end");
  }
  pthread_attr_destroy(&attr);
}

void
feel_vibrate(int large, int small, int ms) {
  pthread_mutex_lock(&lock);
  if(handle >= 0 && !away && !quit) {
    vibration_locked(large, small);
    ms = clamp(ms, 10, 1000);
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += (long)ms * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    vibrating = 1;
    pthread_cond_signal(&cond);
  }
  pthread_mutex_unlock(&lock);
}

void
feel_triggers(int mode, int strength) {
  pthread_mutex_lock(&lock);
  trigger_mode = mode;
  trigger_strength = strength;
  if(handle >= 0 && !away && !quit) {
    triggers_locked(mode, strength);
  }
  pthread_mutex_unlock(&lock);
}

void
feel_light(int on, int r, int g, int b) {
  pthread_mutex_lock(&lock);
  light_on = on;
  light_r = clamp(r, 0, 255);
  light_g = clamp(g, 0, 255);
  light_b = clamp(b, 0, 255);
  if(handle >= 0 && !away && !quit) {
    light_locked();
  }
  pthread_mutex_unlock(&lock);
}

void
feel_away(int a) {
  pthread_mutex_lock(&lock);
  if(handle >= 0 && a != away && !quit) {
    away = a;
    if(away) {  // the system's menus get a quiet controller
      vibrating = 0;
      vibration_locked(0, 0);
      if(trigger_mode != FEEL_TRIGGERS_OFF) {
        triggers_locked(FEEL_TRIGGERS_OFF, 0);
      }
    } else {
      if(trigger_mode != FEEL_TRIGGERS_OFF) {
        triggers_locked(trigger_mode, trigger_strength);
      }
      if(light_on) {
        light_locked();
      }
    }
  }
  pthread_mutex_unlock(&lock);
}

void
feel_stop(void) {
  pthread_mutex_lock(&lock);
  if(handle >= 0 && !quit) {
    vibrating = 0;
    vibration_locked(0, 0);
    if(trigger_mode != FEEL_TRIGGERS_OFF) {
      triggers_locked(FEEL_TRIGGERS_OFF, 0);
    }
    if(light_on) {
      light_on = 0;
      light_locked();
    }
  }
  quit = 1;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&lock);
}
