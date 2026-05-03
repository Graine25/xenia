/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_H_
#define XENIA_UI_VULKAN_VULKAN_H_

#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {

const VulkanDevice* GetLegacyVulkanDevice();
void RegisterLegacyVulkanDevice(const VulkanDevice* device);

inline const VulkanDevice& LegacyVulkanDevice() {
  const VulkanDevice* device = GetLegacyVulkanDevice();
  assert_not_null(device);
  return *device;
}

inline void CheckResult(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    XELOGE("{} failed with Vulkan result {}", operation, int32_t(result));
  }
}

inline std::string to_string(VkResult result) {
  return std::to_string(int32_t(result));
}

inline std::string to_string(VkFormat format) {
  return std::to_string(int32_t(format));
}

inline std::string to_flags_string(VkFormatFeatureFlagBits flags) {
  return std::to_string(uint32_t(flags));
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#ifndef VK_SAFE_DESTROY
#define VK_SAFE_DESTROY(func, device, object, ...) \
  do {                                             \
    if (object) {                                  \
      func(device, object, __VA_ARGS__);           \
      object = nullptr;                            \
    }                                              \
  } while (false)
#endif

#define XE_VK_LEGACY_DEVICE_FUNCTION(name)                                  \
  template <typename... Args>                                               \
  inline auto name(VkDevice, Args... args) {                                \
    const auto& legacy_device = xe::ui::vulkan::LegacyVulkanDevice();       \
    return legacy_device.functions().name(legacy_device.device(), args...); \
  }

#define XE_VK_LEGACY_DISPATCH_FUNCTION(name)                               \
  template <typename... Args>                                              \
  inline auto name(Args... args) {                                         \
    return xe::ui::vulkan::LegacyVulkanDevice().functions().name(args...); \
  }

XE_VK_LEGACY_DEVICE_FUNCTION(vkAllocateCommandBuffers)
XE_VK_LEGACY_DEVICE_FUNCTION(vkAllocateDescriptorSets)
XE_VK_LEGACY_DEVICE_FUNCTION(vkBeginCommandBuffer)
XE_VK_LEGACY_DEVICE_FUNCTION(vkBindBufferMemory)
XE_VK_LEGACY_DEVICE_FUNCTION(vkBindImageMemory)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateBuffer)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateCommandPool)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateDescriptorPool)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateDescriptorSetLayout)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateFramebuffer)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateGraphicsPipelines)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateImage)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateImageView)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreatePipelineCache)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreatePipelineLayout)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateRenderPass)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateSampler)
XE_VK_LEGACY_DEVICE_FUNCTION(vkCreateShaderModule)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyBuffer)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyCommandPool)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyDescriptorPool)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyDescriptorSetLayout)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyFramebuffer)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyImage)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyImageView)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyPipeline)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyPipelineCache)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyPipelineLayout)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyRenderPass)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroySampler)
XE_VK_LEGACY_DEVICE_FUNCTION(vkDestroyShaderModule)
XE_VK_LEGACY_DEVICE_FUNCTION(vkEndCommandBuffer)
XE_VK_LEGACY_DEVICE_FUNCTION(vkFlushMappedMemoryRanges)
XE_VK_LEGACY_DEVICE_FUNCTION(vkFreeCommandBuffers)
XE_VK_LEGACY_DEVICE_FUNCTION(vkFreeDescriptorSets)
XE_VK_LEGACY_DEVICE_FUNCTION(vkFreeMemory)
XE_VK_LEGACY_DEVICE_FUNCTION(vkGetBufferMemoryRequirements)
XE_VK_LEGACY_DEVICE_FUNCTION(vkGetFenceStatus)
XE_VK_LEGACY_DEVICE_FUNCTION(vkGetImageMemoryRequirements)
XE_VK_LEGACY_DEVICE_FUNCTION(vkGetPipelineCacheData)
XE_VK_LEGACY_DEVICE_FUNCTION(vkMapMemory)
XE_VK_LEGACY_DEVICE_FUNCTION(vkResetCommandBuffer)
XE_VK_LEGACY_DEVICE_FUNCTION(vkResetFences)
XE_VK_LEGACY_DEVICE_FUNCTION(vkUnmapMemory)
XE_VK_LEGACY_DEVICE_FUNCTION(vkUpdateDescriptorSets)
XE_VK_LEGACY_DEVICE_FUNCTION(vkWaitForFences)

inline VkResult vkBeginCommandBuffer(
    VkCommandBuffer command_buffer,
    const VkCommandBufferBeginInfo* begin_info) {
  return xe::ui::vulkan::LegacyVulkanDevice().functions().vkBeginCommandBuffer(
      command_buffer, begin_info);
}

inline VkResult vkEndCommandBuffer(VkCommandBuffer command_buffer) {
  return xe::ui::vulkan::LegacyVulkanDevice().functions().vkEndCommandBuffer(
      command_buffer);
}

inline VkResult vkResetCommandBuffer(VkCommandBuffer command_buffer,
                                     VkCommandBufferResetFlags flags) {
  return xe::ui::vulkan::LegacyVulkanDevice().functions().vkResetCommandBuffer(
      command_buffer, flags);
}

XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdBeginRenderPass)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdBindDescriptorSets)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdBindIndexBuffer)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdBindPipeline)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdBindVertexBuffers)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdBlitImage)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdClearColorImage)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdClearDepthStencilImage)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdCopyBufferToImage)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdCopyImageToBuffer)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdDraw)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdDrawIndexed)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdEndRenderPass)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdFillBuffer)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdPipelineBarrier)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdPushConstants)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdResolveImage)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetBlendConstants)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetDepthBias)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetDepthBounds)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetLineWidth)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetScissor)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetStencilCompareMask)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetStencilReference)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetStencilWriteMask)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkCmdSetViewport)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkQueueSubmit)
XE_VK_LEGACY_DISPATCH_FUNCTION(vkQueueWaitIdle)

#undef XE_VK_LEGACY_DISPATCH_FUNCTION
#undef XE_VK_LEGACY_DEVICE_FUNCTION

inline PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice, const char* name) {
  const auto& device = xe::ui::vulkan::LegacyVulkanDevice();
  return device.vulkan_instance()->functions().vkGetDeviceProcAddr(
      device.device(), name);
}

inline void vkGetPhysicalDeviceFormatProperties(
    const xe::ui::vulkan::VulkanDevice& device, VkFormat format,
    VkFormatProperties* properties) {
  device.vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
      device.physical_device(), format, properties);
}

inline VkResult vkGetPhysicalDeviceImageFormatProperties(
    const xe::ui::vulkan::VulkanDevice& device, VkFormat format,
    VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage,
    VkImageCreateFlags flags, VkImageFormatProperties* properties) {
  return device.vulkan_instance()
      ->functions()
      .vkGetPhysicalDeviceImageFormatProperties(device.physical_device(),
                                                format, type, tiling, usage,
                                                flags, properties);
}

#endif  // XENIA_UI_VULKAN_VULKAN_H_
