/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   The console, read for the settings' "Sistema" panel (docs/07, "Painel do
   sistema"): the firmware, the temperatures, the memory, how long it has
   been on, a drive's size. Each is -1 (or "") when the system does not say;
   nothing here changes anything. On a PC (system_test.c) only what FreeBSD
   or Linux have. */

#ifndef SYSTEM_H
#define SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct system_info {
  char firmware[32];         // "13.60"
  int cpu_temp, soc_temp;    // degrees Celsius
  long long mem_total;       // bytes
  long long mem_free;        // free and inactive, bytes
  long long uptime_s;        // since the console started
} system_info_t;

void system_read(system_info_t* info);

// A drive's (or the one a folder is on) size and free space, in bytes:
// 0, or -1 with both -1.
int system_disk(const char* path, long long* total, long long* free_bytes);

#ifdef __cplusplus
}
#endif

#endif
