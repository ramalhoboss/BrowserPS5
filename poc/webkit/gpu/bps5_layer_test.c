/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// bps5_layer.h on the PC (Linux, real POSIX shared memory): the sizes each
// view gets (version 2), a writer and a reader as the WebKit patch
// (AcceleratedSurfacePlayStation.cpp.patch) and poc/browser/main.cpp's
// layer_open() use it, version 1 layers still read, and the layers a reader
// must not open. cc -Wall -Wextra -O2 bps5_layer_test.c -o bps5_layer_test
// (-lrt on old glibc); prints "all passed".

#include "bps5_layer.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if(!(cond)) {                                                       \
      printf("FAIL line %d: %s\n", __LINE__, #cond);                    \
      failures++;                                                       \
    }                                                                   \
  } while(0)

// The browser's views (poc/browser/main.cpp): name, width, height.
static const struct { const char* name; int width, height; } views[] = {
  {"bar", 1920, 152},     {"page", 1920, 928},  {"menu", 560, 966},
  {"dialog", 1240, 720},  {"keyboard", 1920, 440}, {"radial", 720, 720},
  {"tabs", 1920, 1080},
};

static char name[64];

// The writer: as RenderTargetSharedMemory::openLayer().
static bps5_layer_t*
writer_open(int width, int height, uint64_t* bytes_out) {
  uint32_t cw, ch;
  bps5_layer_capacity_for(width, height, &cw, &ch);
  uint64_t bytes = bps5_layer_bytes_for(cw, ch);
  shm_unlink(name);
  int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
  if(fd < 0 || ftruncate(fd, (off_t)bytes) < 0) {
    perror("writer");
    exit(1);
  }
  void* memory = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  bps5_layer_t* layer = (bps5_layer_t*)memory;
  bps5_layer_init(layer, getpid(), cw, ch);
  *bytes_out = bytes;
  return layer;
}

// The reader: as main.cpp's layer_open().
static bps5_layer_t*
reader_open(size_t* mapped) {
  int fd = shm_open(name, O_RDWR, 0);
  if(fd < 0) {
    return 0;
  }
  struct stat st;
  if(fstat(fd, &st) || st.st_size < (off_t)BPS5_LAYER_HEADER_BYTES ||
     (uint64_t)st.st_size > BPS5_LAYER_BYTES + BPS5_LAYER_BUFFERS * BPS5_LAYER_PAGE) {
    close(fd);
    return 0;
  }
  size_t bytes = (size_t)st.st_size;
  void* memory = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    return 0;
  }
  bps5_layer_t* layer = (bps5_layer_t*)memory;
  if(!bps5_layer_ready(layer) || !bps5_layer_fits(layer, bytes)) {
    munmap(memory, bytes);
    return 0;
  }
  *mapped = bytes;
  return layer;
}

// A frame of width x height whose pixels say which frame and where.
static unsigned
write_frame(bps5_layer_t* layer, unsigned back, unsigned width, unsigned height,
            uint32_t number) {
  uint32_t* pixels = bps5_layer_pixels(layer, back);
  for(unsigned y = 0; y < height; y++) {
    for(unsigned x = 0; x < width; x++) {
      pixels[(size_t)y * width + x] = number * 0x1000000u + y * 4096 + x;
    }
  }
  layer->frames[back].width = width;
  layer->frames[back].height = height;
  layer->frames[back].flags = 0;
  layer->frames[back].number = number;
  return bps5_layer_publish(layer, back);
}

static int
frame_right(bps5_layer_t* layer, unsigned front, uint32_t number) {
  const bps5_layer_frame_t* frame = &layer->frames[front];
  const uint32_t* pixels = bps5_layer_pixels(layer, front);
  if(frame->number != number || !bps5_layer_frame_fits(layer, frame)) {
    return 0;
  }
  for(unsigned y = 0; y < frame->height; y++) {
    for(unsigned x = 0; x < frame->width; x++) {
      if(pixels[(size_t)y * frame->width + x] != number * 0x1000000u + y * 4096 + x) {
        return 0;
      }
    }
  }
  return 1;
}

int
main(void) {
  snprintf(name, sizeof(name), "/bps5layer-test-%d", (int)getpid());

  // 1. The sizes.
  uint32_t w, h;
  bps5_layer_capacity_for(1920, 152, &w, &h);
  CHECK(w == 1920 && h == 152);
  bps5_layer_capacity_for(1920, 928, &w, &h);  // the page: the whole screen
  CHECK(w == 1920 && h == 1080);
  bps5_layer_capacity_for(1920, 980, &w, &h);  // without the favorites
  CHECK(w == 1920 && h == 1080);
  bps5_layer_capacity_for(1920, 440, &w, &h);  // the keyboard: its own
  CHECK(w == 1920 && h == 440);
  bps5_layer_capacity_for(560, 966, &w, &h);
  CHECK(w == 560 && h == 966);
  bps5_layer_capacity_for(0, -5, &w, &h);
  CHECK(w == 1 && h == 1);
  bps5_layer_capacity_for(4000, 3000, &w, &h);
  CHECK(w == 1920 && h == 1080);
  CHECK(bps5_layer_frame_bytes_for(1, 1) == BPS5_LAYER_PAGE);
  CHECK(bps5_layer_frame_bytes_for(1920, 1080) % BPS5_LAYER_PAGE == 0);
  CHECK(bps5_layer_frame_bytes_for(1920, 1080) >= BPS5_LAYER_FRAME_BYTES);

  uint64_t all = 0;
  for(size_t i = 0; i < sizeof(views) / sizeof(views[0]); i++) {
    bps5_layer_capacity_for(views[i].width, views[i].height, &w, &h);
    uint64_t bytes = bps5_layer_bytes_for(w, h);
    printf("  %-8s %4dx%-4d -> buffers %4ux%-4u %5.1f MiB\n", views[i].name,
           views[i].width, views[i].height, w, h, bytes / 1048576.0);
    all += bytes;
  }
  uint64_t before = (uint64_t)BPS5_LAYER_BYTES * (sizeof(views) / sizeof(views[0]));
  printf("  all %.1f MiB (version 1: %.1f MiB)\n", all / 1048576.0, before / 1048576.0);
  CHECK(all < 90ull << 20);

  // 2. A writer and a reader, the keyboard's size.
  uint64_t bytes;
  bps5_layer_t* layer = writer_open(1920, 440, &bytes);
  size_t mapped = 0;
  bps5_layer_t* seen = reader_open(&mapped);
  CHECK(seen && mapped == bytes);
  if(seen) {
    CHECK(seen->version == BPS5_LAYER_VERSION);
    unsigned back = 0, front = 2;
    int fresh;
    front = bps5_layer_take(seen, front, &fresh);
    CHECK(!fresh);
    for(uint32_t n = 1; n <= 5; n++) {
      back = write_frame(layer, back, 1920, 440, n);
      front = bps5_layer_take(seen, front, &fresh);
      CHECK(fresh && frame_right(seen, front, n));
    }
    // Two frames between takes: the newer one.
    back = write_frame(layer, back, 1920, 440, 6);
    back = write_frame(layer, back, 1000, 300, 7);
    front = bps5_layer_take(seen, front, &fresh);
    CHECK(fresh && frame_right(seen, front, 7));
    // A frame bigger than the buffers is not one to copy.
    seen->frames[front].height = 441;
    CHECK(!bps5_layer_frame_fits(seen, &seen->frames[front]));
    // The writer goes (or makes it again, bigger): the reader sees it.
    __atomic_store_n(&layer->magic, 0, __ATOMIC_RELEASE);
    CHECK(!bps5_layer_ready(seen));
    munmap(seen, mapped);
  }
  munmap(layer, bytes);

  // 3. A version 1 layer (the TV's WebKit until the next one): still read.
  {
    shm_unlink(name);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    CHECK(fd >= 0 && ftruncate(fd, (off_t)BPS5_LAYER_BYTES) == 0);
    bps5_layer_t* old = (bps5_layer_t*)mmap(0, BPS5_LAYER_BYTES, PROT_READ | PROT_WRITE,
                                            MAP_SHARED, fd, 0);
    close(fd);
    CHECK(old != MAP_FAILED);
    // As the old bps5_layer_init(): no capacity fields.
    old->version = BPS5_LAYER_VERSION_FIXED;
    old->pid = getpid();
    old->middle = 1;
    __atomic_store_n(&old->magic, BPS5_LAYER_MAGIC, __ATOMIC_RELEASE);
    CHECK(bps5_layer_frame_bytes(old) == BPS5_LAYER_FRAME_BYTES);
    unsigned back = write_frame(old, 0, 1920, 1080, 9);
    (void)back;
    seen = reader_open(&mapped);
    CHECK(seen && mapped == BPS5_LAYER_BYTES);
    if(seen) {
      bps5_layer_capacity(seen, &w, &h);
      CHECK(w == 1920 && h == 1080);
      int fresh;
      unsigned front = bps5_layer_take(seen, 2, &fresh);
      CHECK(fresh && frame_right(seen, front, 9));
      munmap(seen, mapped);
    }
    munmap(old, BPS5_LAYER_BYTES);
  }

  // 4. Layers not to open: 0 bytes (2026-11-06), a header only, and one
  // whose header says more than it holds.
  {
    shm_unlink(name);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    CHECK(fd >= 0);
    CHECK(!reader_open(&mapped));
    CHECK(ftruncate(fd, BPS5_LAYER_HEADER_BYTES) == 0);
    CHECK(!reader_open(&mapped));  // no magic
    bps5_layer_t* small = (bps5_layer_t*)mmap(0, BPS5_LAYER_HEADER_BYTES,
                                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(small != MAP_FAILED);
    bps5_layer_init(small, getpid(), 1920, 1080);  // but only 4 KiB long
    CHECK(!reader_open(&mapped));
    small->version = 3;  // a version it does not know
    CHECK(!bps5_layer_ready(small));
    munmap(small, BPS5_LAYER_HEADER_BYTES);
    close(fd);

    uint64_t bytes2;
    bps5_layer_t* odd = writer_open(560, 966, &bytes2);
    odd->capacity_width = 4096;  // bigger than the screen
    CHECK(!reader_open(&mapped));
    odd->capacity_width = 560;
    odd->frame_bytes = 560 * 966 * 4 - 4;  // buffers too small
    CHECK(!reader_open(&mapped));
    odd->frame_bytes = bps5_layer_frame_bytes_for(560, 966);
    seen = reader_open(&mapped);
    CHECK(seen != 0);
    if(seen) {
      munmap(seen, mapped);
    }
    munmap(odd, bytes2);
  }
  shm_unlink(name);

  if(failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
