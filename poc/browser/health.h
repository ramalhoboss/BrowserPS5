/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   BrowserPS5 RN, 2026-11-15 (docs/32, "the console's health"): how the console is doing, for the start page's
   panel: the temperatures of the processor and of the chip, the chip's power by parts, the processor's
   frequency, the temperature the fan aims for, how long the console has been on, the room on its drives and its
   address on the network. All of it only READ, the way PlayBridge reads it (health.c). */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define HEALTH_NONE  (-1)  // no reading
#define HEALTH_DISKS 8

typedef struct health_disk {
  char      path[24];
  long long total, free;  // bytes
} health_disk_t;

typedef struct console_health {
  int           cpu_c, soc_c;            // degrees Celsius, or HEALTH_NONE
  int           gpu_mw, cpu_mw, mem_mw;  // milliwatts, or HEALTH_NONE in all three
  int           cpu_mhz;                 // or HEALTH_NONE
  int           fan_c;                   // the temperature the fan aims for, or HEALTH_NONE
  long long     uptime_s;
  health_disk_t disks[HEALTH_DISKS];
  int           disk_count;
  char          ip[48];                  // "" when there is none
  char          net[16];                 // its interface's name
} console_health_t;

void console_health_read(console_health_t* h);

#ifdef __cplusplus
}
#endif
