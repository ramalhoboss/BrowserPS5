/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// The console's health, see health.h. How the sensors are read, and what a reading may be, is
// PlayBridge's way (github.com/ramalhoboss/PlayBridge, GPL-3.0: payload/console-control.h, control-rules.h; the
// user, 2026-11-15: "vê a pasta do projeto PlayBridge, eu já fiz essa parte"), written again here and ONLY READ:
// PlayBridge can also move the fan's target, this never does.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "health.h"

int sceKernelGetCpuTemperature(int*);
int sceKernelGetSocSensorTemperature(int, int*);
int sceKernelGetSocPowerConsumption(void*);  // fills a 112-byte block, never a single number
long sceKernelGetCpuFrequency(void);

// (PlayBridge, control-rules.h) A reading is taken only when the getter says it read its sensor and the number
// fits a real one.
#define TEMPERATURE_MIN_C 1
#define TEMPERATURE_MAX_C 150
// The chip's power: 8 rails of {power mW, voltage mV, current mA} as 32-bit numbers; rails 0-1 feed the GPU, 2-3
// the CPU and the rest of the chip, 4-7 the memory. The block given is far larger than what is written.
#define POWER_BLOCK        0x100u
#define POWER_RAILS        8u
#define POWER_RAIL_BYTES   12u
#define POWER_RAIL_MAX_MW  200000u
#define POWER_TOTAL_MAX_MW 500000u
// The fan's target temperature, read from its device (the request that only reads).
#define FAN_DEVICE      "/dev/icc_fan"
#define FAN_GET         0xC01C8F08UL
#define FAN_SELECTOR_AT 2u
#define FAN_TARGET_AT   5u
#define FAN_LOWEST      30
#define FAN_HIGHEST     100


static int
temperature(int result, int value) {
  return result == 0 && value >= TEMPERATURE_MIN_C && value <= TEMPERATURE_MAX_C ? value : HEALTH_NONE;
}


static void
power(int result, const unsigned char* block, console_health_t* h) {
  uint32_t sums[3] = {0, 0, 0}, total = 0;

  h->gpu_mw = h->cpu_mw = h->mem_mw = HEALTH_NONE;
  if(result != 0) {
    return;
  }
  for(uint32_t rail = 0; rail < POWER_RAILS; rail++) {
    uint32_t mw;
    memcpy(&mw, block + rail * POWER_RAIL_BYTES, 4);
    if(mw > POWER_RAIL_MAX_MW) {
      return;
    }
    sums[rail < 2 ? 0 : rail < 4 ? 1 : 2] += mw;
    total += mw;
  }
  if(!total || total > POWER_TOTAL_MAX_MW) {
    return;
  }
  h->gpu_mw = (int)sums[0];
  h->cpu_mw = (int)sums[1];
  h->mem_mw = (int)sums[2];
}


static int
fan_target(void) {
  static _Alignas(16) unsigned char block[256];
  const int fd = open(FAN_DEVICE, O_RDWR);
  int target = HEALTH_NONE;

  if(fd < 0) {
    return HEALTH_NONE;
  }
  memset(block, 0, sizeof(block));
  block[FAN_SELECTOR_AT] = 0;
  if(ioctl(fd, FAN_GET, block) >= 0 && block[FAN_TARGET_AT] >= FAN_LOWEST && block[FAN_TARGET_AT] <= FAN_HIGHEST) {
    target = block[FAN_TARGET_AT];
  }
  close(fd);
  return target;
}


static void
disk(console_health_t* h, const char* path, const char* under) {
  struct statfs fs, parent;

  if(h->disk_count >= HEALTH_DISKS || statfs(path, &fs) || !fs.f_blocks) {
    return;
  }
  // a folder that is no drive of its own (nothing plugged in) is its parent's file system: left out
  if(under && !statfs(under, &parent) && !memcmp(&fs.f_fsid, &parent.f_fsid, sizeof(fs.f_fsid))) {
    return;
  }
  health_disk_t* d = &h->disks[h->disk_count++];
  snprintf(d->path, sizeof(d->path), "%s", path);
  d->total = (long long)fs.f_blocks * (long long)fs.f_bsize;
  d->free = (long long)fs.f_bavail * (long long)fs.f_bsize;
}


void
console_health_read(console_health_t* h) {
  static _Alignas(16) unsigned char power_block[POWER_BLOCK];
  int cpu = HEALTH_NONE, soc = HEALTH_NONE;
  struct timespec up;
  struct ifaddrs* list = 0;

  memset(h, 0, sizeof(*h));
  const int cpu_result = sceKernelGetCpuTemperature(&cpu);
  const int soc_result = sceKernelGetSocSensorTemperature(0, &soc);  // sensor 0 is the chip's own
  h->cpu_c = temperature(cpu_result, cpu);
  h->soc_c = temperature(soc_result, soc);
  memset(power_block, 0, sizeof(power_block));
  power(sceKernelGetSocPowerConsumption(power_block), power_block, h);
  const long hz = sceKernelGetCpuFrequency();
  h->cpu_mhz = hz > 0 ? (int)(hz / 1000000) : HEALTH_NONE;
  h->fan_c = fan_target();
  h->uptime_s = clock_gettime(CLOCK_MONOTONIC, &up) ? 0 : (long long)up.tv_sec;

  disk(h, "/user", 0);
  disk(h, "/data", "/user");
  for(int i = 0; i < 2; i++) {
    char path[24];
    snprintf(path, sizeof(path), "/mnt/ext%d", i);
    disk(h, path, "/mnt");
  }
  for(int i = 0; i < 8; i++) {
    char path[24];
    snprintf(path, sizeof(path), "/mnt/usb%d", i);
    disk(h, path, "/mnt");
  }

  if(!getifaddrs(&list)) {
    for(struct ifaddrs* a = list; a && !h->ip[0]; a = a->ifa_next) {
      if(!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP)) {
        continue;
      }
      inet_ntop(AF_INET, &((struct sockaddr_in*)a->ifa_addr)->sin_addr, h->ip, sizeof(h->ip));
      snprintf(h->net, sizeof(h->net), "%s", a->ifa_name ? a->ifa_name : "");
    }
    freeifaddrs(list);
  }
}
