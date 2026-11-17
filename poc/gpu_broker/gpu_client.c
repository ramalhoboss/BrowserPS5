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
 * A payload that draws with the console's GPU through the owner title
 * (owner.cpp, PPSA97010): RADV in this process (PS5_Vulkan's broker client
 * archive), its memory and submissions the owner's. It reserves the
 * broker's host ranges, connects to 127.0.0.1:9040, and then is an ordinary
 * Vulkan program: a VK_KHR_display swapchain, a turning triangle for
 * DRAW_SECONDS, and one frame's centre and corner read back.
 *
 * Report: stdout, klog and /user/homebrew/browserps5/gpu_client.txt.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "client.h"
#include "protocol.h"
#include "triangle_shaders.h"

#include <ps5platform/fp.h>
#include <ps5platform/kernel.h>
#include <ps5platform/klog.h>

#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef WAIT_SECONDS
#define WAIT_SECONDS 600
#endif
#define DRAW_SECONDS 20
#define OWNER_PORT 9040
#define REPORT_DIR "/user/homebrew/browserps5"
#define REPORT_PATH REPORT_DIR "/gpu_client.txt"

static FILE *report_file;
static unsigned passed, failed;

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *format, ...)
{
   char line[512];
   va_list arguments;
   va_start(arguments, format);
   int n = snprintf(line, sizeof(line), "[gpu_client] ");
   vsnprintf(line + n, sizeof(line) - n - 1, format, arguments);
   va_end(arguments);
   strcat(line, "\n");
   fputs(line, stdout);
   fflush(stdout);
   if (report_file)
      fputs(line, report_file);
   sceKernelDebugOutText(0, line);
}

static bool check(bool ok, const char *what)
{
   say("%s %s", ok ? "PASS" : "FAIL", what);
   ok ? ++passed : ++failed;
   return ok;
}

static uint64_t now_ns(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

#define GLOBAL_COMMANDS(X) X(CreateInstance) X(EnumerateInstanceExtensionProperties)
#define INSTANCE_COMMANDS(X)                                                                       \
   X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties)                   \
   X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties)                  \
   X(CreateDevice) X(GetDeviceProcAddr) X(GetPhysicalDeviceDisplayPropertiesKHR)                   \
   X(GetDisplayModePropertiesKHR) X(CreateDisplayPlaneSurfaceKHR) X(DestroySurfaceKHR)             \
   X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR)
#define DEVICE_COMMANDS(X)                                                                         \
   X(GetDeviceQueue) X(DestroyDevice) X(DeviceWaitIdle) X(CreateSwapchainKHR)                      \
   X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR)       \
   X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass)                 \
   X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateShaderModule) X(DestroyShaderModule)         \
   X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline)  \
   X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer)      \
   X(EndCommandBuffer) X(ResetCommandBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass)             \
   X(CmdBindPipeline) X(CmdPushConstants) X(CmdDraw) X(CmdPipelineBarrier)                         \
   X(CmdCopyImageToBuffer) X(QueueSubmit) X(CreateFence) X(DestroyFence) X(WaitForFences)          \
   X(ResetFences) X(CreateSemaphore) X(DestroySemaphore) X(CreateBuffer) X(DestroyBuffer)          \
   X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory)              \
   X(MapMemory) X(UnmapMemory)
#define DECLARE(name) static PFN_vk##name vk##name;
GLOBAL_COMMANDS(DECLARE)
INSTANCE_COMMANDS(DECLARE)
DEVICE_COMMANDS(DECLARE)
#undef DECLARE

/* The owner's port, tried every second for up to WAIT_SECONDS (the title
 * may be opened after this payload is sent). */
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
         say("waiting up to %u s for the owner title (PPSA97010) on 127.0.0.1:%d", WAIT_SECONDS, OWNER_PORT);
      sleep(1);
   }
   return -1;
}

struct gpu {
   VkInstance instance;
   VkPhysicalDevice physical;
   VkDevice device;
   VkQueue queue;
   uint32_t family;
   VkSurfaceKHR surface;
   VkSwapchainKHR swapchain;
   VkExtent2D extent;
   uint32_t image_count;
   VkImage images[8];
   VkImageView views[8];
   VkFramebuffer framebuffers[8];
   VkRenderPass pass;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkCommandPool pool;
   VkCommandBuffer cmd;
   VkFence fence;
   VkSemaphore acquired, rendered;
   VkBuffer readback;
   VkDeviceMemory readback_memory;
   const uint8_t *readback_map;
};

static bool create_instance(struct gpu *g)
{
   vkCreateInstance = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
   vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vk_icdGetInstanceProcAddr(
      VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
   if (!check(vkCreateInstance && vkEnumerateInstanceExtensionProperties, "vk_icdGetInstanceProcAddr"))
      return false;
   const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
   const VkApplicationInfo application = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "BrowserPS5 gpu_client",
      .apiVersion = VK_API_VERSION_1_1,
   };
   const VkInstanceCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application,
      .enabledExtensionCount = 2,
      .ppEnabledExtensionNames = extensions,
   };
   VkResult result = vkCreateInstance(&info, NULL, &g->instance);
   if (!check(result == VK_SUCCESS, "vkCreateInstance with VK_KHR_display")) {
      say("vkCreateInstance: %d", result);
      return false;
   }
   bool ok = true;
#define LOAD(name)                                                                                 \
   vk##name = (PFN_vk##name)vk_icdGetInstanceProcAddr(g->instance, "vk" #name);                    \
   ok &= vk##name != NULL;
   INSTANCE_COMMANDS(LOAD)
#undef LOAD
   return check(ok, "the instance's commands");
}

static bool create_device(struct gpu *g)
{
   uint32_t count = 1;
   VkResult result = vkEnumeratePhysicalDevices(g->instance, &count, &g->physical);
   if (!check((result == VK_SUCCESS || result == VK_INCOMPLETE) && count == 1, "one physical device"))
      return false;
   VkPhysicalDeviceProperties properties;
   vkGetPhysicalDeviceProperties(g->physical, &properties);
   say("device: %s, Vulkan %u.%u.%u", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
       VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
   VkQueueFamilyProperties families[8];
   count = 8;
   vkGetPhysicalDeviceQueueFamilyProperties(g->physical, &count, families);
   g->family = UINT32_MAX;
   for (uint32_t i = 0; i < count && g->family == UINT32_MAX; ++i)
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
         g->family = i;
   if (!check(g->family != UINT32_MAX, "a graphics queue"))
      return false;
   const float priority = 1.0f;
   const VkDeviceQueueCreateInfo queue = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = g->family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
   };
   const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
   const VkDeviceCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue,
      .enabledExtensionCount = 1,
      .ppEnabledExtensionNames = extensions,
   };
   result = vkCreateDevice(g->physical, &info, NULL, &g->device);
   if (!check(result == VK_SUCCESS, "vkCreateDevice with VK_KHR_swapchain")) {
      say("vkCreateDevice: %d", result);
      return false;
   }
   bool ok = true;
#define LOAD(name)                                                                                 \
   vk##name = (PFN_vk##name)vkGetDeviceProcAddr(g->device, "vk" #name);                            \
   ok &= vk##name != NULL;
   DEVICE_COMMANDS(LOAD)
#undef LOAD
   if (!check(ok, "the device's commands"))
      return false;
   vkGetDeviceQueue(g->device, g->family, 0, &g->queue);
   return true;
}

static bool create_swapchain(struct gpu *g)
{
   VkDisplayPropertiesKHR display;
   uint32_t count = 1;
   VkResult result = vkGetPhysicalDeviceDisplayPropertiesKHR(g->physical, &count, &display);
   if (!check((result == VK_SUCCESS || result == VK_INCOMPLETE) && count == 1, "one display"))
      return false;
   VkDisplayModePropertiesKHR modes[4];
   count = 4;
   result = vkGetDisplayModePropertiesKHR(g->physical, display.display, &count, modes);
   if (!check((result == VK_SUCCESS || result == VK_INCOMPLETE) && count >= 1, "the display's modes"))
      return false;
   const VkExtent2D mode = modes[0].parameters.visibleRegion;
   say("display: %s, %ux%u at %.3f Hz", display.displayName ? display.displayName : "?", mode.width, mode.height,
       modes[0].parameters.refreshRate / 1000.0);
   const VkDisplaySurfaceCreateInfoKHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
      .displayMode = modes[0].displayMode,
      .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
      .imageExtent = mode,
   };
   result = vkCreateDisplayPlaneSurfaceKHR(g->instance, &surface_info, NULL, &g->surface);
   if (!check(result == VK_SUCCESS, "vkCreateDisplayPlaneSurfaceKHR"))
      return false;
   VkBool32 supported = VK_FALSE;
   vkGetPhysicalDeviceSurfaceSupportKHR(g->physical, g->family, g->surface, &supported);
   VkSurfaceCapabilitiesKHR caps;
   vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g->physical, g->surface, &caps);
   say("surface: supported %u, %ux%u, %u to %u images, usage %#x", supported, caps.currentExtent.width,
       caps.currentExtent.height, caps.minImageCount, caps.maxImageCount, caps.supportedUsageFlags);
   g->extent = caps.currentExtent;
   uint32_t images = caps.minImageCount + 1;
   if (caps.maxImageCount && images > caps.maxImageCount)
      images = caps.maxImageCount;
   const VkSwapchainCreateInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = g->surface,
      .minImageCount = images,
      .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = g->extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .clipped = VK_TRUE,
   };
   result = vkCreateSwapchainKHR(g->device, &info, NULL, &g->swapchain);
   if (!check(result == VK_SUCCESS, "vkCreateSwapchainKHR")) {
      say("vkCreateSwapchainKHR: %d", result);
      return false;
   }
   g->image_count = 8;
   result = vkGetSwapchainImagesKHR(g->device, g->swapchain, &g->image_count, g->images);
   return check(result == VK_SUCCESS && g->image_count >= 2, "the swapchain's images");
}

static VkShaderModule shader(struct gpu *g, const uint32_t *code, size_t bytes)
{
   const VkShaderModuleCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = bytes,
      .pCode = code,
   };
   VkShaderModule module = VK_NULL_HANDLE;
   vkCreateShaderModule(g->device, &info, NULL, &module);
   return module;
}

struct push { float angle, aspect; };

static bool create_pipeline(struct gpu *g)
{
   const VkAttachmentDescription attachment = {
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
   };
   const VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   const VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &reference,
   };
   const VkSubpassDependency dependency = {
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
   };
   const VkRenderPassCreateInfo pass = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency,
   };
   if (!check(vkCreateRenderPass(g->device, &pass, NULL, &g->pass) == VK_SUCCESS, "vkCreateRenderPass"))
      return false;
   for (uint32_t i = 0; i < g->image_count; ++i) {
      const VkImageViewCreateInfo view = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = g->images[i],
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = VK_FORMAT_B8G8R8A8_UNORM,
         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      if (vkCreateImageView(g->device, &view, NULL, &g->views[i]) != VK_SUCCESS)
         return check(false, "the swapchain's image views");
      const VkFramebufferCreateInfo framebuffer = {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = g->pass,
         .attachmentCount = 1,
         .pAttachments = &g->views[i],
         .width = g->extent.width,
         .height = g->extent.height,
         .layers = 1,
      };
      if (vkCreateFramebuffer(g->device, &framebuffer, NULL, &g->framebuffers[i]) != VK_SUCCESS)
         return check(false, "the swapchain's framebuffers");
   }

   const VkPushConstantRange range = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(struct push)};
   const VkPipelineLayoutCreateInfo layout = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &range,
   };
   if (!check(vkCreatePipelineLayout(g->device, &layout, NULL, &g->layout) == VK_SUCCESS, "vkCreatePipelineLayout"))
      return false;
   const VkShaderModule vertex = shader(g, triangle_vert, sizeof(triangle_vert));
   const VkShaderModule fragment = shader(g, triangle_frag, sizeof(triangle_frag));
   if (!check(vertex && fragment, "the shader modules"))
      return false;
   const VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertex,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragment,
       .pName = "main"},
   };
   const VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   const VkPipelineInputAssemblyStateCreateInfo assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };
   const VkViewport viewport = {0, 0, (float)g->extent.width, (float)g->extent.height, 0, 1};
   const VkRect2D scissor = {{0, 0}, g->extent};
   const VkPipelineViewportStateCreateInfo viewports = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor,
   };
   const VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
   };
   const VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   const VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xf};
   const VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment,
   };
   const VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &input,
      .pInputAssemblyState = &assembly,
      .pViewportState = &viewports,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend,
      .layout = g->layout,
      .renderPass = g->pass,
   };
   const uint64_t start = now_ns();
   const VkResult result = vkCreateGraphicsPipelines(g->device, VK_NULL_HANDLE, 1, &info, NULL, &g->pipeline);
   say("pipeline compiled in %.1f ms", (now_ns() - start) / 1e6);
   vkDestroyShaderModule(g->device, vertex, NULL);
   vkDestroyShaderModule(g->device, fragment, NULL);
   return check(result == VK_SUCCESS, "vkCreateGraphicsPipelines");
}

static bool create_frame_objects(struct gpu *g)
{
   const VkCommandPoolCreateInfo pool = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = g->family,
   };
   const VkCommandBufferAllocateInfo cmd = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   const VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   const VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
   if (vkCreateCommandPool(g->device, &pool, NULL, &g->pool) != VK_SUCCESS)
      return check(false, "vkCreateCommandPool");
   VkCommandBufferAllocateInfo allocate = cmd;
   allocate.commandPool = g->pool;
   bool ok = vkAllocateCommandBuffers(g->device, &allocate, &g->cmd) == VK_SUCCESS &&
             vkCreateFence(g->device, &fence, NULL, &g->fence) == VK_SUCCESS &&
             vkCreateSemaphore(g->device, &semaphore, NULL, &g->acquired) == VK_SUCCESS &&
             vkCreateSemaphore(g->device, &semaphore, NULL, &g->rendered) == VK_SUCCESS;
   if (!check(ok, "the command buffer, fence and semaphores"))
      return false;

   /* The readback buffer, host-visible: shared memory the owner made. */
   const VkBufferCreateInfo buffer = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = 64,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
   };
   if (vkCreateBuffer(g->device, &buffer, NULL, &g->readback) != VK_SUCCESS)
      return check(false, "vkCreateBuffer");
   VkMemoryRequirements needs;
   vkGetBufferMemoryRequirements(g->device, g->readback, &needs);
   VkPhysicalDeviceMemoryProperties memory;
   vkGetPhysicalDeviceMemoryProperties(g->physical, &memory);
   const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   uint32_t type = UINT32_MAX;
   for (uint32_t i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; ++i)
      if ((needs.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & wanted) == wanted)
         type = i;
   const VkMemoryAllocateInfo allocation = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = needs.size,
      .memoryTypeIndex = type,
   };
   void *map = NULL;
   ok = type != UINT32_MAX && vkAllocateMemory(g->device, &allocation, NULL, &g->readback_memory) == VK_SUCCESS &&
        vkBindBufferMemory(g->device, g->readback, g->readback_memory, 0) == VK_SUCCESS &&
        vkMapMemory(g->device, g->readback_memory, 0, VK_WHOLE_SIZE, 0, &map) == VK_SUCCESS;
   g->readback_map = map;
   return check(ok, "a host-visible readback buffer (shared memory from the owner)");
}

static void image_barrier(struct gpu *g, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
                          VkAccessFlags dst)
{
   const VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = src,
      .dstAccessMask = dst,
      .oldLayout = from,
      .newLayout = to,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   vkCmdPipelineBarrier(g->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                        NULL, 1, &barrier);
}

/* The clear colour, B8G8R8A8 UNORM: 0.05, 0.05, 0.15 of 255, rounded. */
static const float clear_rgb[3] = {0.05f, 0.05f, 0.15f};

static VkResult frame(struct gpu *g, float angle, bool readback)
{
   uint32_t index;
   VkResult result = vkAcquireNextImageKHR(g->device, g->swapchain, UINT64_MAX, g->acquired, VK_NULL_HANDLE, &index);
   if (result != VK_SUCCESS)
      return result;
   const VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   vkResetCommandBuffer(g->cmd, 0);
   vkBeginCommandBuffer(g->cmd, &begin);
   const VkClearValue clear = {.color = {.float32 = {clear_rgb[0], clear_rgb[1], clear_rgb[2], 1.0f}}};
   const VkRenderPassBeginInfo pass = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = g->pass,
      .framebuffer = g->framebuffers[index],
      .renderArea = {{0, 0}, g->extent},
      .clearValueCount = 1,
      .pClearValues = &clear,
   };
   vkCmdBeginRenderPass(g->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
   vkCmdBindPipeline(g->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipeline);
   const struct push push = {angle, (float)g->extent.width / (float)g->extent.height};
   vkCmdPushConstants(g->cmd, g->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
   vkCmdDraw(g->cmd, 3, 1, 0, 0);
   vkCmdEndRenderPass(g->cmd);
   if (readback) {
      image_barrier(g, g->images[index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
      /* The centre pixel and one near the top-left corner. */
      const VkBufferImageCopy copies[2] = {
         {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
          .imageOffset = {(int32_t)g->extent.width / 2, (int32_t)g->extent.height / 2, 0},
          .imageExtent = {1, 1, 1}},
         {.bufferOffset = 4, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {8, 8, 0},
          .imageExtent = {1, 1, 1}},
      };
      vkCmdCopyImageToBuffer(g->cmd, g->images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g->readback, 2, copies);
      image_barrier(g, g->images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_READ_BIT, 0);
   }
   vkEndCommandBuffer(g->cmd);
   const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   const VkSubmitInfo submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &g->acquired,
      .pWaitDstStageMask = &wait_stage,
      .commandBufferCount = 1,
      .pCommandBuffers = &g->cmd,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &g->rendered,
   };
   vkResetFences(g->device, 1, &g->fence);
   result = vkQueueSubmit(g->queue, 1, &submit, g->fence);
   if (result != VK_SUCCESS)
      return result;
   const VkPresentInfoKHR present = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &g->rendered,
      .swapchainCount = 1,
      .pSwapchains = &g->swapchain,
      .pImageIndices = &index,
   };
   result = vkQueuePresentKHR(g->queue, &present);
   /* The one command buffer is recorded again next frame. */
   vkWaitForFences(g->device, 1, &g->fence, VK_TRUE, UINT64_MAX);
   return result;
}

static bool near(unsigned value, unsigned want, unsigned slack)
{
   return value + slack >= want && value <= want + slack;
}

static void draw(struct gpu *g)
{
   const uint64_t start = now_ns();
   unsigned frames = 0;
   VkResult result = VK_SUCCESS;
   bool read = false;
   while (now_ns() - start < DRAW_SECONDS * 1000000000ull) {
      const float angle = (float)((now_ns() - start) / 1e9 * 1.5);
      result = frame(g, angle, frames == 60);
      if (result != VK_SUCCESS)
         break;
      if (frames == 60)
         read = true;
      if (++frames % 300 == 0)
         say("%u frames in %.1f s", frames, (now_ns() - start) / 1e9);
   }
   const double seconds = (now_ns() - start) / 1e9;
   say("%u frames in %.2f s: %.1f per second (last result %d)", frames, seconds, frames / seconds, result);
   check(result == VK_SUCCESS && frames >= DRAW_SECONDS * 30, "frames drawn and presented through the owner");
   if (read) {
      const uint8_t *p = g->readback_map;
      say("readback: centre bgra %u %u %u %u, corner bgra %u %u %u %u", p[0], p[1], p[2], p[3], p[4], p[5], p[6],
          p[7]);
      /* The centre is the centroid: a third of red, green and blue (85). */
      check(near(p[0], 85, 12) && near(p[1], 85, 12) && near(p[2], 85, 12) && p[3] == 255,
            "the triangle's centre read back");
      check(near(p[4], 38, 2) && near(p[5], 13, 2) && near(p[6], 13, 2) && p[7] == 255,
            "the clear colour read back in the corner");
   }
}

static void destroy(struct gpu *g)
{
   if (!g->device)
      goto instance;
   vkDeviceWaitIdle(g->device);
   if (g->readback_memory)
      vkFreeMemory(g->device, g->readback_memory, NULL);
   if (g->readback)
      vkDestroyBuffer(g->device, g->readback, NULL);
   if (g->acquired)
      vkDestroySemaphore(g->device, g->acquired, NULL);
   if (g->rendered)
      vkDestroySemaphore(g->device, g->rendered, NULL);
   if (g->fence)
      vkDestroyFence(g->device, g->fence, NULL);
   if (g->pool)
      vkDestroyCommandPool(g->device, g->pool, NULL);
   if (g->pipeline)
      vkDestroyPipeline(g->device, g->pipeline, NULL);
   if (g->layout)
      vkDestroyPipelineLayout(g->device, g->layout, NULL);
   for (uint32_t i = 0; i < g->image_count; ++i) {
      if (g->framebuffers[i])
         vkDestroyFramebuffer(g->device, g->framebuffers[i], NULL);
      if (g->views[i])
         vkDestroyImageView(g->device, g->views[i], NULL);
   }
   if (g->pass)
      vkDestroyRenderPass(g->device, g->pass, NULL);
   if (g->swapchain)
      vkDestroySwapchainKHR(g->device, g->swapchain, NULL);
   vkDestroyDevice(g->device, NULL);
instance:
   if (g->surface)
      vkDestroySurfaceKHR(g->instance, g->surface, NULL);
   if (g->instance)
      vkDestroyInstance(g->instance, NULL);
}

int main(void)
{
   ps5_fp_ieee();
   mkdir(REPORT_DIR, 0777);
   report_file = fopen(REPORT_PATH, "w");
   if (report_file)
      setvbuf(report_file, NULL, _IOLBF, 0);
   say("client pid %d", getpid());

   /* Before anything maps memory: where the owner puts the buffers this
    * process maps, at the same addresses. */
   const int reserved = pw_gpu_reserve_host_ranges(sceKernelReserveVirtualRange, sceKernelMunmap);
   check(reserved == 1, "the broker's host ranges reserved (0x240000000, 0x3000000000)");

   const int owner = connect_owner();
   if (!check(owner >= 0, "connected to the owner title on 127.0.0.1:9040"))
      goto done;
   const int bound = ps5_gpu_client_bind(owner, reserved == 1);
   if (!check(bound == 0, "ps5_gpu_client_bind (INIT)")) {
      say("bind: %d", bound);
      goto done;
   }

   struct gpu g;
   memset(&g, 0, sizeof(g));
   if (create_instance(&g) && create_device(&g) && create_swapchain(&g) && create_pipeline(&g) &&
       create_frame_objects(&g))
      draw(&g);
   destroy(&g);
   close(owner);

done:
   say("%s %u checks passed, %u failed", failed || !passed ? "FAIL" : "PASS", passed, failed);
   if (report_file)
      fclose(report_file);
   return failed ? 1 : 0;
}
