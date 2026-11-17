/*   Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
 *
 * dl_rate.c on the build machine (make test): a steady speed, a speed that
 * changes, pieces that come in bursts, a stall and what comes after it,
 * the time left, and a download WebKit starts over.
 */
#include <stdio.h>

#include "dl_rate.h"

static int failed;

#define CHECK(what, cond)                                   \
  do {                                                      \
    if(cond) {                                              \
      printf("PASS %s\n", what);                            \
    } else {                                                \
      printf("FAIL %s\n", what);                            \
      failed = 1;                                           \
    }                                                       \
  } while(0)

static int
near(long long a, long long b, double part) {
  const double d = (double)(a > b ? a - b : b - a);
  return d <= part * (double)b;
}

int
main(void) {
  dl_rate_t r;
  double t = 1000;
  long long got = 0;

  /* Nothing known at first. */
  dl_rate_start(&r, 0, t);
  CHECK("no speed before a sample", dl_rate_speed(&r, t) == 0);
  CHECK("no time left before a sample", dl_rate_left(&r, 0, 1000000, t) == -1);

  /* 2 MB/s in pieces of 64 KiB every ~32 ms, for 10 s. */
  for(int i = 0; i < 312; i++) {
    t += 32;
    got += 65536;
    dl_rate_add(&r, got, t);
  }
  long long speed = dl_rate_speed(&r, t);
  CHECK("a steady 2 MB/s", near(speed, 2048000, 0.03));
  long long left = dl_rate_left(&r, got, got + 20480000, t);
  CHECK("20 MB left at 2 MB/s: ~10 s", left >= 9 && left <= 11);
  CHECK("no total: no time left", dl_rate_left(&r, got, 0, t) == -1);
  CHECK("all in: 0 s left", dl_rate_left(&r, got, got, t) == 0);

  /* It drops to 500 KB/s: the speed follows it within ~15 s. */
  for(int i = 0; i < 150; i++) {
    t += 100;
    got += 51200;
    dl_rate_add(&r, got, t);
  }
  speed = dl_rate_speed(&r, t);
  CHECK("down to 500 KB/s", near(speed, 512000, 0.05));

  /* Bursts: 1 MB every 2 s is still 500 KB/s, not 0 then a lot. */
  dl_rate_start(&r, 0, t);
  got = 0;
  for(int i = 0; i < 10; i++) {
    t += 2000;
    got += 1024000;
    dl_rate_add(&r, got, t);
  }
  CHECK("1 MB every 2 s: 500 KB/s", near(dl_rate_speed(&r, t), 512000, 0.05));

  /* A stall: nothing for 5 s is speed 0 and no time left. */
  CHECK("stalled after 5 s", dl_rate_speed(&r, t + 5000) == 0);
  CHECK("stalled: no time left", dl_rate_left(&r, got, got * 2, t + 5000) == -1);

  /* After the stall, the 5 s with nothing do not pull the speed down. */
  t += 5000;
  for(int i = 0; i < 30; i++) {
    t += 100;
    got += 51200;
    dl_rate_add(&r, got, t);
  }
  CHECK("after the stall, 500 KB/s again", near(dl_rate_speed(&r, t), 512000, 0.15));

  /* Slow and big: the time left stops at 99 h. */
  dl_rate_start(&r, 0, 0);
  dl_rate_add(&r, 10, 1000);
  dl_rate_add(&r, 20, 2000);
  CHECK("10 B/s", dl_rate_speed(&r, 2000) == 10);
  CHECK("1 TB at 10 B/s: 99 h", dl_rate_left(&r, 20, 1LL << 40, 2000) == DL_RATE_LEFT_MAX);

  /* WebKit starts it over (fewer bytes than before): a new start. */
  dl_rate_add(&r, 5, 2500);
  CHECK("started over: no speed yet", dl_rate_speed(&r, 2500) == 0);

  /* Over 4 GiB. */
  dl_rate_start(&r, 5LL << 30, 0);
  dl_rate_add(&r, (5LL << 30) + 104857600, 1000);
  CHECK("100 MB/s past 5 GiB", dl_rate_speed(&r, 1000) == 104857600);

  /* Never started: nothing. */
  dl_rate_t blank = {0};
  CHECK("a blank one says 0", dl_rate_speed(&blank, 1000) == 0 && dl_rate_left(&blank, 0, 10, 1000) == -1);

  printf(failed ? "dl_rate_test: FAIL\n" : "dl_rate_test: all passed\n");
  return failed;
}
