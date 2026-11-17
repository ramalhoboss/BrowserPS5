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

/*
 * libvulkan.so.1 for payloads: the console's GPU through the owner title
 * (../gpu_broker, PPSA97010). RADV is PS5_Vulkan's broker client archive,
 * linked whole into this library; this file adds what a Vulkan loader gives
 * zink, vkGetInstanceProcAddr and vkGetDeviceProcAddr (pacbrew's zink patch
 * calls those two directly on the PS5), and exports nothing else.
 *
 * The broker's host ranges are reserved when the library loads, before the
 * program maps memory of its own (the owner maps the buffers this process
 * sees at the same addresses in both). The first vkCreateInstance connects
 * to the owner on 127.0.0.1:9040 and binds RADV to it; the connection is the
 * process's GPU session and stays open until the process ends.
 *
 * It says what it does on stderr and in the klog, as "[libvulkan] ...".
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "client.h"
#include "protocol.h"

#include <ps5platform/fp.h>
#include <ps5platform/kernel.h>
#include <ps5platform/klog.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define OWNER_PORT 9040
#define WAIT_SECONDS 30

#define PUBLIC __attribute__((visibility("default")))

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_common_GetDeviceProcAddr(VkDevice device, const char *name);

static pthread_mutex_t bind_lock = PTHREAD_MUTEX_INITIALIZER;
static enum { UNBOUND, BOUND, FAILED } state;
static int reserved;
/* When they could not be: the range that could not, and where in it
 * something else is (to 16 KiB), for the report. */
static uint64_t taken_base, taken_at, taken_end;

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *format, ...)
{
   char line[256];
   va_list arguments;
   va_start(arguments, format);
   int n = snprintf(line, sizeof(line), "[libvulkan] ");
   vsnprintf(line + n, sizeof(line) - n - 1, format, arguments);
   va_end(arguments);
   strcat(line, "\n");
   fputs(line, stderr);
   sceKernelDebugOutText(0, line);
}

/* Whether nothing is mapped in [base, base + bytes): reserved as
 * pw_gpu_reserve_host_ranges does, and let go again. */
static bool range_free(uint64_t base, uint64_t bytes)
{
   void *at = (void *)(uintptr_t)base;
   const int error = sceKernelReserveVirtualRange(&at, bytes,
         PW_GPU_RESERVE_FIXED | PW_GPU_RESERVE_NO_OVERWRITE, 16384);
   if (at && at != (void *)(uintptr_t)base)
      sceKernelMunmap(at, bytes);
   if (error || at != (void *)(uintptr_t)base)
      return false;
   sceKernelMunmap(at, bytes);
   return true;
}

/* How much of the start of [base, base + bytes) is free, by halving. */
static uint64_t free_start(uint64_t base, uint64_t bytes)
{
   if (range_free(base, bytes))
      return bytes;
   uint64_t free = 0, taken = bytes;
   while (taken - free > 16384) {
      const uint64_t length = (free + (taken - free) / 2) & ~UINT64_C(16383);
      if (range_free(base, length))
         free = length;
      else
         taken = length;
   }
   return free;
}

__attribute__((constructor)) static void load(void)
{
   /* The floating-point state RADV is built for (ps5platform/fp.h): a
    * title's startup code sets it, and a payload's does not. */
   ps5_fp_ieee();
   /* A process our launcher starts can load this library twice (once under
    * each of libEGL and libGLESv2): the first copy reserves the ranges and
    * says so in the environment, which the whole process shares, and the
    * second uses them. */
   if (getenv("BROWSERPS5_VK_RANGES")) {
      reserved = 1;
      say("pid %d: host ranges already reserved by another copy of this library; this one is at %p",
          getpid(), (void *)load);
      return;
   }
   reserved = pw_gpu_reserve_host_ranges(sceKernelReserveVirtualRange, sceKernelMunmap);
   if (reserved == 1)
      setenv("BROWSERPS5_VK_RANGES", "1", 1);
   if (reserved != 1) {
      const uint64_t bases[2] = {PW_GPU_HOST_WINDOW_BASE, PW_GPU_HOST_HIGH_BASE};
      const uint64_t sizes[2] = {PW_GPU_HOST_WINDOW_BYTES, PW_GPU_HOST_HIGH_BYTES};
      for (int i = 0; i < 2 && !taken_base; ++i) {
         const uint64_t start = free_start(bases[i], sizes[i]);
         if (start < sizes[i]) {
            taken_base = bases[i];
            taken_at = bases[i] + start;
         }
      }
      /* Where what is there ends, to 16 MiB (at most 64 GiB on). */
      for (taken_end = taken_at; taken_end < taken_at + (UINT64_C(64) << 30);
           taken_end += UINT64_C(16) << 20) {
         if (range_free(taken_end, 16384))
            break;
      }
   }
   /* Every process that loads this library says how it went (for now). */
   say("pid %d: host ranges %s (taken at %#llx up to about %#llx); this library is at %p",
       getpid(), reserved == 1 ? "reserved" : "NOT reserved",
       (unsigned long long)taken_at, (unsigned long long)taken_end, (void *)load);
}

/* The owner's port, tried every second for up to WAIT_SECONDS. */
static int connect_owner(void)
{
   for (unsigned second = 0; second < WAIT_SECONDS; ++second) {
      const int s = socket(AF_INET, SOCK_STREAM, 0);
      struct sockaddr_in address;
      memset(&address, 0, sizeof(address));
      address.sin_family = AF_INET;
      address.sin_port = htons(OWNER_PORT);
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (s >= 0 && !connect(s, (struct sockaddr *)&address, sizeof(address))) {
         const int yes = 1;
         setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
         return s;
      }
      if (s >= 0)
         close(s);
      if (second == 0)
         say("waiting up to %u s for the owner title (PPSA97010, or the browser's PPSA97020) on 127.0.0.1:%d", WAIT_SECONDS, OWNER_PORT);
      sleep(1);
   }
   return -1;
}

/* Once per process; a failure stays one, so a second instance does not wait
 * for the owner again. */
static bool bind_owner(void)
{
   pthread_mutex_lock(&bind_lock);
   if (state == UNBOUND) {
      state = FAILED;
      if (reserved != 1)
         say("the broker's host ranges could not be reserved: something was mapped there first "
             "(the range at %#llx, from %#llx; this library is at %p)",
             (unsigned long long)taken_base, (unsigned long long)taken_at, (void *)load);
      const int owner = connect_owner();
      if (owner < 0) {
         say("no owner title on 127.0.0.1:%d", OWNER_PORT);
      } else {
         const int bound = ps5_gpu_client_bind(owner, reserved == 1);
         if (bound == 0) {
            state = BOUND;
            say("bound to the owner title (pid %d)", getpid());
         } else {
            say("ps5_gpu_client_bind: %d", bound);
            close(owner);
         }
      }
   }
   const bool bound = state == BOUND;
   pthread_mutex_unlock(&bind_lock);
   return bound;
}

static VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo *info,
                                                      const VkAllocationCallbacks *allocator,
                                                      VkInstance *instance)
{
   if (!bind_owner())
      return VK_ERROR_INITIALIZATION_FAILED;
   PFN_vkCreateInstance create =
      (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
   return create(info, allocator, instance);
}

/* A loader's answer: there are no layers (a driver does not list them, and
 * zink logged "vkEnumerateInstanceLayerProperties failed" without this). */
static VKAPI_ATTR VkResult VKAPI_CALL enumerate_instance_layers(uint32_t *count, VkLayerProperties *properties)
{
   (void)properties;
   *count = 0;
   return VK_SUCCESS;
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
   if (name && !strcmp(name, "vkGetInstanceProcAddr"))
      return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
   if (name && !strcmp(name, "vkCreateInstance"))
      return (PFN_vkVoidFunction)create_instance;
   if (name && !strcmp(name, "vkEnumerateInstanceLayerProperties"))
      return (PFN_vkVoidFunction)enumerate_instance_layers;
   return vk_icdGetInstanceProcAddr(instance, name);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name)
{
   return vk_common_GetDeviceProcAddr(device, name);
}
