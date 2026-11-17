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
 * What a Vulkan loader gives zink, for the WebProcess inside the browser
 * title (2c-2, docs/05-webprocess-no-titulo.md): the title's own RADV, the
 * GPU owner's, with no broker in between. The payloads' libvulkan.so.1
 * (../../gl_zink/vulkan_client.c) does the same over the broker; here there
 * is nothing to connect to. Mesa's object (../../gl_zink/title-gl.sh) leaves
 * these two names undefined.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <string.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_common_GetDeviceProcAddr(VkDevice device, const char *name);

/* A loader's answer: there are no layers (zink logged
 * "vkEnumerateInstanceLayerProperties failed" without it on the payloads). */
static VKAPI_ATTR VkResult VKAPI_CALL enumerate_instance_layers(uint32_t *count, VkLayerProperties *properties)
{
   (void)properties;
   *count = 0;
   return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
   if (name && !strcmp(name, "vkGetInstanceProcAddr"))
      return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
   if (name && !strcmp(name, "vkEnumerateInstanceLayerProperties"))
      return (PFN_vkVoidFunction)enumerate_instance_layers;
   return vk_icdGetInstanceProcAddr(instance, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name)
{
   return vk_common_GetDeviceProcAddr(device, name);
}
