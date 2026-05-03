/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_shader.h"

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

VulkanShader::VulkanShader(ui::vulkan::VulkanDevice* device,
                           xenos::ShaderType shader_type, uint64_t data_hash,
                           const uint32_t* dword_ptr, uint32_t dword_count)
    : SpirvShader(shader_type, data_hash, dword_ptr, dword_count),
      device_(device) {}

VulkanShader::~VulkanShader() {
  if (shader_module_) {
    vkDestroyShaderModule(*device_, shader_module_, nullptr);
    shader_module_ = nullptr;
  }
}

bool VulkanShader::Prepare(const Shader::Translation& translation) {
  assert_true(translation.is_valid());

  if (shader_module_) {
    vkDestroyShaderModule(*device_, shader_module_, nullptr);
    shader_module_ = nullptr;
  }

  // Create the shader module.
  VkShaderModuleCreateInfo shader_info;
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.pNext = nullptr;
  shader_info.flags = 0;
  shader_info.codeSize = translation.translated_binary().size();
  shader_info.pCode =
      reinterpret_cast<const uint32_t*>(translation.translated_binary().data());
  auto status =
      vkCreateShaderModule(*device_, &shader_info, nullptr, &shader_module_);
  CheckResult(status, "vkCreateShaderModule");

  char typeChar = shader_type_ == xenos::ShaderType::kPixel
                      ? 'p'
                      : shader_type_ == xenos::ShaderType::kVertex ? 'v' : 'u';
  device_->DbgSetObjectName(
      uint64_t(shader_module_), VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT,
      fmt::format("S({}): {:016X}", typeChar, ucode_data_hash()));
  if (status == VK_SUCCESS) {
    prepared_translation_ = &translation;
    return true;
  }
  prepared_translation_ = nullptr;
  return false;
}

std::pair<std::filesystem::path, std::filesystem::path> VulkanShader::Dump(
    const std::filesystem::path& base_path, const char* path_prefix) const {
  if (!prepared_translation_) {
    return {};
  }
  return prepared_translation_->Dump(base_path, path_prefix);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
