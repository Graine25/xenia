/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// stb_image_write.h implementation is compiled in trace_dump.cc; here we only
// need the declarations.
#include "third_party/stb/stb_image_write.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

#include "xenia/base/filesystem.h"
#include "xenia/gpu/texture_dump.h"

namespace xe {
namespace gpu {

bool TextureDumpWritePng(const std::filesystem::path& path, const uint8_t* rgba,
                         uint32_t width, uint32_t height,
                         uint32_t stride_bytes) {
  FILE* handle = filesystem::OpenFile(path, "wb");
  if (!handle) {
    return false;
  }
  auto write_fn = [](void* context, void* data, int size) {
    fwrite(data, 1, size, reinterpret_cast<FILE*>(context));
  };
  int stride = stride_bytes ? static_cast<int>(stride_bytes)
                            : static_cast<int>(width * 4);
  int result =
      stbi_write_png_to_func(write_fn, handle, static_cast<int>(width),
                             static_cast<int>(height), 4, rgba, stride);
  fclose(handle);
  return result != 0;
}

// ---------------------------------------------------------------------------
// BC block decoders
// ---------------------------------------------------------------------------

// Expands an RGB565 value to 8-bit R, G, B components.
static inline void Rgb565Expand(uint16_t c, uint8_t& r, uint8_t& g,
                                uint8_t& b) {
  r = static_cast<uint8_t>(((c >> 11) & 0x1Fu) * 255u / 31u);
  g = static_cast<uint8_t>(((c >> 5) & 0x3Fu) * 255u / 63u);
  b = static_cast<uint8_t>((c & 0x1Fu) * 255u / 31u);
}

// Decodes a single BC1 (DXT1) 4x4 block.
// out points to the top-left pixel; stride is the RGBA8 row stride in bytes.
static void DecodeBc1Block(const uint8_t* src, uint8_t* out,
                           uint32_t stride) {
  uint16_t c0 = static_cast<uint16_t>(src[0]) |
                (static_cast<uint16_t>(src[1]) << 8);
  uint16_t c1 = static_cast<uint16_t>(src[2]) |
                (static_cast<uint16_t>(src[3]) << 8);

  uint8_t r[4], g[4], b[4], a[4];
  Rgb565Expand(c0, r[0], g[0], b[0]);
  a[0] = 255u;
  Rgb565Expand(c1, r[1], g[1], b[1]);
  a[1] = 255u;

  if (c0 > c1) {
    r[2] = static_cast<uint8_t>((2u * r[0] + r[1] + 1u) / 3u);
    g[2] = static_cast<uint8_t>((2u * g[0] + g[1] + 1u) / 3u);
    b[2] = static_cast<uint8_t>((2u * b[0] + b[1] + 1u) / 3u);
    a[2] = 255u;
    r[3] = static_cast<uint8_t>((r[0] + 2u * r[1] + 1u) / 3u);
    g[3] = static_cast<uint8_t>((g[0] + 2u * g[1] + 1u) / 3u);
    b[3] = static_cast<uint8_t>((b[0] + 2u * b[1] + 1u) / 3u);
    a[3] = 255u;
  } else {
    r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2u);
    g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2u);
    b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2u);
    a[2] = 255u;
    r[3] = 0; g[3] = 0; b[3] = 0; a[3] = 0u;  // transparent black
  }

  uint32_t indices = static_cast<uint32_t>(src[4]) |
                     (static_cast<uint32_t>(src[5]) << 8) |
                     (static_cast<uint32_t>(src[6]) << 16) |
                     (static_cast<uint32_t>(src[7]) << 24);
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      uint32_t idx = (indices >> (2u * (row * 4u + col))) & 0x3u;
      uint8_t* p = out + row * stride + col * 4u;
      p[0] = r[idx]; p[1] = g[idx]; p[2] = b[idx]; p[3] = a[idx];
    }
  }
}

// Decodes a single BC2 (DXT3) 4x4 block: explicit 4-bit alpha + BC1 color.
static void DecodeBc2Block(const uint8_t* src, uint8_t* out,
                           uint32_t stride) {
  DecodeBc1Block(src + 8, out, stride);
  for (uint32_t row = 0; row < 4u; ++row) {
    uint16_t alpha_row = static_cast<uint16_t>(src[row * 2u]) |
                         (static_cast<uint16_t>(src[row * 2u + 1u]) << 8);
    for (uint32_t col = 0; col < 4u; ++col) {
      uint8_t a4 = static_cast<uint8_t>((alpha_row >> (col * 4u)) & 0xFu);
      out[row * stride + col * 4u + 3u] = static_cast<uint8_t>(a4 * 17u);
    }
  }
}

// Decodes a single BC3 (DXT5) 4x4 block: interpolated alpha + BC1 color.
static void DecodeBc3Block(const uint8_t* src, uint8_t* out,
                           uint32_t stride) {
  uint8_t a0 = src[0], a1 = src[1];
  uint8_t alphas[8];
  alphas[0] = a0; alphas[1] = a1;
  if (a0 > a1) {
    for (uint32_t i = 0; i < 6u; ++i)
      alphas[2u + i] = static_cast<uint8_t>(
          (a0 * (6u - i) + a1 * (i + 1u)) / 7u);
  } else {
    for (uint32_t i = 0; i < 4u; ++i)
      alphas[2u + i] = static_cast<uint8_t>(
          (a0 * (4u - i) + a1 * (i + 1u)) / 5u);
    alphas[6] = 0u; alphas[7] = 255u;
  }
  uint64_t alpha_bits =
      static_cast<uint64_t>(src[2]) | (static_cast<uint64_t>(src[3]) << 8) |
      (static_cast<uint64_t>(src[4]) << 16) |
      (static_cast<uint64_t>(src[5]) << 24) |
      (static_cast<uint64_t>(src[6]) << 32) |
      (static_cast<uint64_t>(src[7]) << 40);

  DecodeBc1Block(src + 8, out, stride);
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      uint32_t bit_pos = (row * 4u + col) * 3u;
      uint32_t ai = static_cast<uint32_t>(alpha_bits >> bit_pos) & 0x7u;
      out[row * stride + col * 4u + 3u] = alphas[ai];
    }
  }
}

bool TextureDumpBcToPng(const std::filesystem::path& path,
                        const uint8_t* bc_data, uint32_t bc_row_pitch,
                        uint32_t width, uint32_t height,
                        uint32_t bc_bytes_per_block, bool is_bc3) {
  if (!width || !height || !bc_bytes_per_block) {
    return false;
  }

  uint32_t blocks_x = (width + 3u) / 4u;
  uint32_t blocks_y = (height + 3u) / 4u;
  uint32_t rgba_stride = width * 4u;

  std::vector<uint8_t> rgba(rgba_stride * height, 0u);

  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      const uint8_t* block_src =
          bc_data + by * bc_row_pitch + bx * bc_bytes_per_block;

      uint32_t px = std::min(4u, width - bx * 4u);
      uint32_t py = std::min(4u, height - by * 4u);

      if (px == 4u && py == 4u) {
        uint8_t* block_dst =
            rgba.data() + by * 4u * rgba_stride + bx * 16u;
        if (bc_bytes_per_block == 8u) {
          DecodeBc1Block(block_src, block_dst, rgba_stride);
        } else if (is_bc3) {
          DecodeBc3Block(block_src, block_dst, rgba_stride);
        } else {
          DecodeBc2Block(block_src, block_dst, rgba_stride);
        }
      } else {
        // Edge block: decode to 4x4 temp then copy valid pixels.
        uint8_t temp[4u * 4u * 4u] = {};
        if (bc_bytes_per_block == 8u) {
          DecodeBc1Block(block_src, temp, 4u * 4u);
        } else if (is_bc3) {
          DecodeBc3Block(block_src, temp, 4u * 4u);
        } else {
          DecodeBc2Block(block_src, temp, 4u * 4u);
        }
        for (uint32_t row = 0; row < py; ++row) {
          uint8_t* dst = rgba.data() +
                         (by * 4u + row) * rgba_stride + bx * 16u;
          std::memcpy(dst, temp + row * 16u, px * 4u);
        }
      }
    }
  }

  return TextureDumpWritePng(path, rgba.data(), width, height);
}

// ---------------------------------------------------------------------------
// Mip generation
// ---------------------------------------------------------------------------

std::vector<std::vector<uint8_t>> TextureDumpGenerateMips(
    const uint8_t* base_rgba, uint32_t base_width, uint32_t base_height,
    uint32_t count) {
  std::vector<std::vector<uint8_t>> mips;
  mips.reserve(count);
  const uint8_t* prev = base_rgba;
  uint32_t prev_w = base_width, prev_h = base_height;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t mip_w = std::max(1u, prev_w / 2u);
    uint32_t mip_h = std::max(1u, prev_h / 2u);
    std::vector<uint8_t> mip_data(mip_w * mip_h * 4u);
    for (uint32_t y = 0; y < mip_h; ++y) {
      for (uint32_t x = 0; x < mip_w; ++x) {
        uint32_t sx0 = x * 2u, sy0 = y * 2u;
        uint32_t sx1 = std::min(sx0 + 1u, prev_w - 1u);
        uint32_t sy1 = std::min(sy0 + 1u, prev_h - 1u);
        for (uint32_t c = 0; c < 4u; ++c) {
          uint32_t v = prev[(sy0 * prev_w + sx0) * 4u + c] +
                       prev[(sy0 * prev_w + sx1) * 4u + c] +
                       prev[(sy1 * prev_w + sx0) * 4u + c] +
                       prev[(sy1 * prev_w + sx1) * 4u + c];
          mip_data[(y * mip_w + x) * 4u + c] =
              static_cast<uint8_t>(v / 4u);
        }
      }
    }
    // Push first, then take data() from the stored element so `prev` is valid.
    mips.push_back(std::move(mip_data));
    prev = mips.back().data();
    prev_w = mip_w;
    prev_h = mip_h;
  }
  return mips;
}

std::vector<uint8_t> TextureDumpResizeRgba(const uint8_t* src,
                                           uint32_t src_width,
                                           uint32_t src_height,
                                           uint32_t dst_width,
                                           uint32_t dst_height) {
  if (!src || !src_width || !src_height || !dst_width || !dst_height) {
    return {};
  }
  std::vector<uint8_t> dst(dst_width * dst_height * 4u);
  float sx = static_cast<float>(src_width) / static_cast<float>(dst_width);
  float sy = static_cast<float>(src_height) / static_cast<float>(dst_height);
  for (uint32_t dy = 0; dy < dst_height; ++dy) {
    for (uint32_t dx = 0; dx < dst_width; ++dx) {
      // Map destination pixel centre to source space.
      float fx = (static_cast<float>(dx) + 0.5f) * sx - 0.5f;
      float fy = (static_cast<float>(dy) + 0.5f) * sy - 0.5f;
      int x0 = static_cast<int>(fx);
      int y0 = static_cast<int>(fy);
      float tx = fx - static_cast<float>(x0);
      float ty = fy - static_cast<float>(y0);
      // Clamp sample coordinates.
      int x1 = std::min(x0 + 1, static_cast<int>(src_width) - 1);
      int y1 = std::min(y0 + 1, static_cast<int>(src_height) - 1);
      x0 = std::max(x0, 0);
      y0 = std::max(y0, 0);
      for (uint32_t c = 0; c < 4u; ++c) {
        float v00 = src[(y0 * src_width + x0) * 4u + c];
        float v10 = src[(y0 * src_width + x1) * 4u + c];
        float v01 = src[(y1 * src_width + x0) * 4u + c];
        float v11 = src[(y1 * src_width + x1) * 4u + c];
        float v = v00 * (1.0f - tx) * (1.0f - ty) +
                  v10 * tx * (1.0f - ty) +
                  v01 * (1.0f - tx) * ty +
                  v11 * tx * ty;
        dst[(dy * dst_width + dx) * 4u + c] =
            static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
      }
    }
  }
  return dst;
}

}  // namespace gpu
}  // namespace xe

