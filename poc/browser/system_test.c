/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   system.c on the PC: what Linux says comes back sane. */

#include <stdio.h>

#include "system.h"

static int failures;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while(0)

int
main(void) {
  system_info_t info;
  system_read(&info);
  printf("firmware '%s', cpu %d C, soc %d C, memory %lld of %lld bytes free, up %lld s\n",
         info.firmware, info.cpu_temp, info.soc_temp, info.mem_free, info.mem_total, info.uptime_s);
  CHECK(info.firmware[0] == 0);  // only on the console
  CHECK(info.cpu_temp == -1 && info.soc_temp == -1);
  CHECK(info.mem_total > 0);
  CHECK(info.mem_free > 0 && info.mem_free <= info.mem_total);
  CHECK(info.uptime_s > 0);

  long long total, free_bytes;
  CHECK(!system_disk("/", &total, &free_bytes));
  CHECK(total > 0 && free_bytes >= 0 && free_bytes <= total);
  CHECK(system_disk("/no/such/place", &total, &free_bytes) == -1);
  CHECK(total == -1 && free_bytes == -1);

  printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
