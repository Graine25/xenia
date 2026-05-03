/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_SHADER_H_
#define XENIA_GPU_VULKAN_VULKAN_SHADER_H_

#include <filesystem>
#include <string>

#include "xenia/gpu/shader.h"
#include "xenia/gpu/spirv_shader.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanShader : public SpirvShader {
 public:
  VulkanShader(ui::vulkan::VulkanDevice* device, xenos::ShaderType shader_type,
               uint64_t data_hash, const uint32_t* dword_ptr,
               uint32_t dword_count);
  ~VulkanShader() override;

  // Available only if the shader is_valid and has been prepared.
  VkShaderModule shader_module() const { return shader_module_; }

  bool is_valid() const {
    return prepared_translation_ && prepared_translation_->is_valid() &&
           shader_module_;
  }
  bool is_translated() const {
    return prepared_translation_ && prepared_translation_->is_translated();
  }

  bool Prepare(const Shader::Translation& translation);
  std::pair<std::filesystem::path, std::filesystem::path> Dump(
      const std::filesystem::path& base_path, const char* path_prefix) const;

 private:
  ui::vulkan::VulkanDevice* device_ = nullptr;
  VkShaderModule shader_module_ = nullptr;
  const Shader::Translation* prepared_translation_ = nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_SHADER_H_
