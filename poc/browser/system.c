/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   The console, read (system.h). */

#include "system.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#endif

#ifdef __PROSPERO__
// libkernel's (the SDK has the stubs, not the headers), as the public
// homebrew headers have them.
typedef struct sw_version {
  uint64_t size;
  char text[0x1c];
  uint32_t version;  // 0xMMmm....: "MM.mm" in hexadecimal digits
} sw_version_t;
int sceKernelGetProsperoSystemSwVersion(sw_version_t* version);
int sceKernelGetCpuTemperature(int* celsius);
int sceKernelGetSocSensorTemperature(int sensor, int* celsius);

static int
sane_temperature(int celsius) {
  return celsius > 0 && celsius < 130 ? celsius : -1;
}
#endif


#ifdef __FreeBSD__
static long long
sysctl_number(const char* name) {
  unsigned char buffer[8] = {0};
  size_t size = sizeof(buffer);
  if(sysctlbyname(name, buffer, &size, 0, 0)) {
    return -1;
  }
  if(size == 4) {
    uint32_t v;
    memcpy(&v, buffer, 4);
    return v;
  }
  if(size == 8) {
    uint64_t v;
    memcpy(&v, buffer, 8);
    return (long long)v;
  }
  return -1;
}
#endif

void
system_read(system_info_t* info) {
  memset(info, 0, sizeof(*info));
  info->cpu_temp = info->soc_temp = -1;
  info->mem_total = info->mem_free = info->uptime_s = -1;

#ifdef __PROSPERO__
  sw_version_t v;
  memset(&v, 0, sizeof(v));
  v.size = sizeof(v);
  if(!sceKernelGetProsperoSystemSwVersion(&v)) {
    if(v.version >> 24) {
      snprintf(info->firmware, sizeof(info->firmware), "%x.%02x", v.version >> 24,
               (v.version >> 16) & 0xff);
    } else {
      v.text[sizeof(v.text) - 1] = 0;
      const char* t = v.text;
      while(*t == ' ') {
        t++;
      }
      snprintf(info->firmware, sizeof(info->firmware), "%s", t);
    }
  }
  int celsius = 0;
  if(!sceKernelGetCpuTemperature(&celsius)) {
    info->cpu_temp = sane_temperature(celsius);
  }
  celsius = 0;
  if(!sceKernelGetSocSensorTemperature(0, &celsius)) {
    info->soc_temp = sane_temperature(celsius);
  }
#endif

#ifdef __FreeBSD__
  long long page = sysctl_number("hw.pagesize");
  long long total = sysctl_number("hw.physmem");
  long long free_pages = sysctl_number("vm.stats.vm.v_free_count");
  long long inactive = sysctl_number("vm.stats.vm.v_inactive_count");
  if(total > 0) {
    info->mem_total = total;
  }
  if(page > 0 && free_pages >= 0) {
    info->mem_free = (free_pages + (inactive > 0 ? inactive : 0)) * page;
  }
  struct timeval boot;
  size_t size = sizeof(boot);
  if(!sysctlbyname("kern.boottime", &boot, &size, 0, 0) && size == sizeof(boot) && boot.tv_sec > 0) {
    info->uptime_s = (long long)(time(0) - boot.tv_sec);
  }
#else
  struct sysinfo s;
  if(!sysinfo(&s)) {
    info->mem_total = (long long)s.totalram * s.mem_unit;
    info->mem_free = (long long)(s.freeram + s.bufferram) * s.mem_unit;
    info->uptime_s = s.uptime;
  }
#endif
}

int
system_disk(const char* path, long long* total, long long* free_bytes) {
  struct statvfs v;
  if(statvfs(path, &v)) {
    *total = *free_bytes = -1;
    return -1;
  }
  *total = (long long)v.f_blocks * (long long)v.f_frsize;
  *free_bytes = (long long)v.f_bavail * (long long)v.f_frsize;
  return 0;
}
