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
#include <string>
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

std::filesystem::path TextureDumpSubresourcePath(
    const std::filesystem::path& path, const char* label, uint32_t index) {
  std::filesystem::path filename = path.stem().string() + "_" + label +
                                   std::to_string(index) +
                                   path.extension().string();
  return path.parent_path() / filename;
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
static void DecodeBc1Block(const uint8_t* src, uint8_t* out, uint32_t stride,
                           bool allow_1bit_alpha = true) {
  uint16_t c0 =
      static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
  uint16_t c1 =
      static_cast<uint16_t>(src[2]) | (static_cast<uint16_t>(src[3]) << 8);

  uint8_t r[4], g[4], b[4], a[4];
  Rgb565Expand(c0, r[0], g[0], b[0]);
  a[0] = 255u;
  Rgb565Expand(c1, r[1], g[1], b[1]);
  a[1] = 255u;

  if (c0 > c1 || !allow_1bit_alpha) {
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
    r[3] = 0;
    g[3] = 0;
    b[3] = 0;
    a[3] = 0u;  // transparent black
  }

  uint32_t indices = static_cast<uint32_t>(src[4]) |
                     (static_cast<uint32_t>(src[5]) << 8) |
                     (static_cast<uint32_t>(src[6]) << 16) |
                     (static_cast<uint32_t>(src[7]) << 24);
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      uint32_t idx = (indices >> (2u * (row * 4u + col))) & 0x3u;
      uint8_t* p = out + row * stride + col * 4u;
      p[0] = r[idx];
      p[1] = g[idx];
      p[2] = b[idx];
      p[3] = a[idx];
    }
  }
}

// Decodes a single BC2 (DXT3) 4x4 block: explicit 4-bit alpha + BC1 color.
static void DecodeBc2Block(const uint8_t* src, uint8_t* out, uint32_t stride) {
  DecodeBc1Block(src + 8, out, stride, false);
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
static void DecodeBc3Block(const uint8_t* src, uint8_t* out, uint32_t stride) {
  uint8_t a0 = src[0], a1 = src[1];
  uint8_t alphas[8];
  alphas[0] = a0;
  alphas[1] = a1;
  if (a0 > a1) {
    for (uint32_t i = 0; i < 6u; ++i)
      alphas[2u + i] =
          static_cast<uint8_t>((a0 * (6u - i) + a1 * (i + 1u)) / 7u);
  } else {
    for (uint32_t i = 0; i < 4u; ++i)
      alphas[2u + i] =
          static_cast<uint8_t>((a0 * (4u - i) + a1 * (i + 1u)) / 5u);
    alphas[6] = 0u;
    alphas[7] = 255u;
  }
  uint64_t alpha_bits = static_cast<uint64_t>(src[2]) |
                        (static_cast<uint64_t>(src[3]) << 8) |
                        (static_cast<uint64_t>(src[4]) << 16) |
                        (static_cast<uint64_t>(src[5]) << 24) |
                        (static_cast<uint64_t>(src[6]) << 32) |
                        (static_cast<uint64_t>(src[7]) << 40);

  DecodeBc1Block(src + 8, out, stride, false);
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      uint32_t bit_pos = (row * 4u + col) * 3u;
      uint32_t ai = static_cast<uint32_t>(alpha_bits >> bit_pos) & 0x7u;
      out[row * stride + col * 4u + 3u] = alphas[ai];
    }
  }
}

// Decodes a BC4 block into a single-channel temp[16] array (stride=4).
static void DecodeBc4Block(const uint8_t* src, uint8_t* out, uint32_t stride) {
  uint8_t a0 = src[0], a1 = src[1];
  uint8_t alphas[8];
  alphas[0] = a0;
  alphas[1] = a1;
  if (a0 > a1) {
    for (uint32_t i = 0; i < 6u; ++i)
      alphas[2u + i] =
          static_cast<uint8_t>((a0 * (6u - i) + a1 * (i + 1u)) / 7u);
  } else {
    for (uint32_t i = 0; i < 4u; ++i)
      alphas[2u + i] =
          static_cast<uint8_t>((a0 * (4u - i) + a1 * (i + 1u)) / 5u);
    alphas[6] = 0u;
    alphas[7] = 255u;
  }
  uint64_t bits = static_cast<uint64_t>(src[2]) |
                  (static_cast<uint64_t>(src[3]) << 8) |
                  (static_cast<uint64_t>(src[4]) << 16) |
                  (static_cast<uint64_t>(src[5]) << 24) |
                  (static_cast<uint64_t>(src[6]) << 32) |
                  (static_cast<uint64_t>(src[7]) << 40);
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      uint32_t bit_pos = (row * 4u + col) * 3u;
      uint32_t ai = static_cast<uint32_t>(bits >> bit_pos) & 0x7u;
      out[row * stride + col] = alphas[ai];
    }
  }
}

// Decodes BC4 to RGBA8, replicating the single channel into R/G/B.
static void DecodeBc4BlockRgba(const uint8_t* src, uint8_t* out,
                               uint32_t stride) {
  uint8_t temp[16];
  DecodeBc4Block(src, temp, 4u);
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      uint8_t v = temp[row * 4u + col];
      uint8_t* p = out + row * stride + col * 4u;
      p[0] = v;
      p[1] = v;
      p[2] = v;
      p[3] = 255u;
    }
  }
}

// Decodes BC5 to RGBA8: R from block 0, G from block 1, B=0, A=255.
static void DecodeBc5BlockRgba(const uint8_t* src, uint8_t* out,
                               uint32_t stride) {
  uint8_t r_temp[16], g_temp[16];
  DecodeBc4Block(src, r_temp, 4u);
  DecodeBc4Block(src + 8u, g_temp, 4u);
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      uint8_t* p = out + row * stride + col * 4u;
      p[0] = r_temp[row * 4u + col];
      p[1] = g_temp[row * 4u + col];
      p[2] = 0u;
      p[3] = 255u;
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
        uint8_t* block_dst = rgba.data() + by * 4u * rgba_stride + bx * 16u;
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
          uint8_t* dst = rgba.data() + (by * 4u + row) * rgba_stride + bx * 16u;
          std::memcpy(dst, temp + row * 16u, px * 4u);
        }
      }
    }
  }

  return TextureDumpWritePng(path, rgba.data(), width, height);
}

// ---------------------------------------------------------------------------
// Format utilities
// ---------------------------------------------------------------------------

uint32_t TextureDumpBytesPerBlockOrPixel(DumpPixelFormat fmt) {
  switch (fmt) {
    case DumpPixelFormat::kRGBA8:
      return 4u;
    case DumpPixelFormat::kBC1:
      return 8u;
    case DumpPixelFormat::kBC2:
      return 16u;
    case DumpPixelFormat::kBC3:
      return 16u;
    case DumpPixelFormat::kBC4:
      return 8u;
    case DumpPixelFormat::kBC5:
      return 16u;
    case DumpPixelFormat::kR8:
      return 1u;
    case DumpPixelFormat::kRG8:
      return 2u;
    case DumpPixelFormat::kB5G6R5:
      return 2u;
    case DumpPixelFormat::kB5G5R5A1:
      return 2u;
    case DumpPixelFormat::kB4G4R4A4:
      return 2u;
    case DumpPixelFormat::kR10G10B10A2:
      return 4u;
    default:
      return 0u;
  }
}

// Decodes a generic block-compressed or packed-pixel buffer to RGBA8 and PNG.
bool TextureDumpRawToPng(const std::filesystem::path& path, const uint8_t* data,
                         uint32_t row_pitch, uint32_t width, uint32_t height,
                         DumpPixelFormat fmt) {
  if (!data || !width || !height) return false;

  // BC formats: use block decoder helpers.
  bool is_bc = (fmt == DumpPixelFormat::kBC1 || fmt == DumpPixelFormat::kBC2 ||
                fmt == DumpPixelFormat::kBC3 || fmt == DumpPixelFormat::kBC4 ||
                fmt == DumpPixelFormat::kBC5);
  if (is_bc) {
    uint32_t bytes_per_block = TextureDumpBytesPerBlockOrPixel(fmt);
    uint32_t blocks_x = (width + 3u) / 4u;
    uint32_t blocks_y = (height + 3u) / 4u;
    uint32_t rgba_stride = width * 4u;
    std::vector<uint8_t> rgba(rgba_stride * height, 0u);

    for (uint32_t by = 0; by < blocks_y; ++by) {
      for (uint32_t bx = 0; bx < blocks_x; ++bx) {
        const uint8_t* block_src = data + by * row_pitch + bx * bytes_per_block;
        uint32_t px = std::min(4u, width - bx * 4u);
        uint32_t py = std::min(4u, height - by * 4u);

        if (px == 4u && py == 4u) {
          uint8_t* block_dst = rgba.data() + by * 4u * rgba_stride + bx * 16u;
          switch (fmt) {
            case DumpPixelFormat::kBC1:
              DecodeBc1Block(block_src, block_dst, rgba_stride);
              break;
            case DumpPixelFormat::kBC2:
              DecodeBc2Block(block_src, block_dst, rgba_stride);
              break;
            case DumpPixelFormat::kBC3:
              DecodeBc3Block(block_src, block_dst, rgba_stride);
              break;
            case DumpPixelFormat::kBC4:
              DecodeBc4BlockRgba(block_src, block_dst, rgba_stride);
              break;
            case DumpPixelFormat::kBC5:
              DecodeBc5BlockRgba(block_src, block_dst, rgba_stride);
              break;
            default:
              break;
          }
        } else {
          uint8_t temp[4u * 4u * 4u] = {};
          switch (fmt) {
            case DumpPixelFormat::kBC1:
              DecodeBc1Block(block_src, temp, 16u);
              break;
            case DumpPixelFormat::kBC2:
              DecodeBc2Block(block_src, temp, 16u);
              break;
            case DumpPixelFormat::kBC3:
              DecodeBc3Block(block_src, temp, 16u);
              break;
            case DumpPixelFormat::kBC4:
              DecodeBc4BlockRgba(block_src, temp, 16u);
              break;
            case DumpPixelFormat::kBC5:
              DecodeBc5BlockRgba(block_src, temp, 16u);
              break;
            default:
              break;
          }
          for (uint32_t row = 0; row < py; ++row) {
            uint8_t* dst =
                rgba.data() + (by * 4u + row) * rgba_stride + bx * 16u;
            std::memcpy(dst, temp + row * 16u, px * 4u);
          }
        }
      }
    }
    return TextureDumpWritePng(path, rgba.data(), width, height);
  }

  // Uncompressed packed-pixel formats: decode row by row.
  uint32_t rgba_stride = width * 4u;
  std::vector<uint8_t> rgba(rgba_stride * height);

  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* src_row = data + y * row_pitch;
    uint8_t* dst_row = rgba.data() + y * rgba_stride;

    switch (fmt) {
      case DumpPixelFormat::kRGBA8:
        std::memcpy(dst_row, src_row, width * 4u);
        break;
      case DumpPixelFormat::kR8:
        for (uint32_t x = 0; x < width; ++x) {
          uint8_t v = src_row[x];
          dst_row[x * 4u + 0u] = v;
          dst_row[x * 4u + 1u] = v;
          dst_row[x * 4u + 2u] = v;
          dst_row[x * 4u + 3u] = 255u;
        }
        break;
      case DumpPixelFormat::kRG8:
        for (uint32_t x = 0; x < width; ++x) {
          dst_row[x * 4u + 0u] = src_row[x * 2u + 0u];
          dst_row[x * 4u + 1u] = src_row[x * 2u + 1u];
          dst_row[x * 4u + 2u] = 0u;
          dst_row[x * 4u + 3u] = 255u;
        }
        break;
      case DumpPixelFormat::kB5G6R5:
        for (uint32_t x = 0; x < width; ++x) {
          uint16_t p = static_cast<uint16_t>(src_row[x * 2u]) |
                       (static_cast<uint16_t>(src_row[x * 2u + 1u]) << 8);
          // B5G6R5/R5G6B5 host layouts store R in the high 5 bits.
          uint8_t r = static_cast<uint8_t>(((p >> 11) & 0x1Fu) * 255u / 31u);
          uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x3Fu) * 255u / 63u);
          uint8_t b = static_cast<uint8_t>((p & 0x1Fu) * 255u / 31u);
          dst_row[x * 4u + 0u] = r;
          dst_row[x * 4u + 1u] = g;
          dst_row[x * 4u + 2u] = b;
          dst_row[x * 4u + 3u] = 255u;
        }
        break;
      case DumpPixelFormat::kB5G5R5A1:
        for (uint32_t x = 0; x < width; ++x) {
          uint16_t p = static_cast<uint16_t>(src_row[x * 2u]) |
                       (static_cast<uint16_t>(src_row[x * 2u + 1u]) << 8);
          // A1R5G5B5/B5G5R5A1 host layouts store R in bits 14:10.
          uint8_t a = static_cast<uint8_t>((p >> 15) ? 255u : 0u);
          uint8_t r = static_cast<uint8_t>(((p >> 10) & 0x1Fu) * 255u / 31u);
          uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x1Fu) * 255u / 31u);
          uint8_t b = static_cast<uint8_t>((p & 0x1Fu) * 255u / 31u);
          dst_row[x * 4u + 0u] = r;
          dst_row[x * 4u + 1u] = g;
          dst_row[x * 4u + 2u] = b;
          dst_row[x * 4u + 3u] = a;
        }
        break;
      case DumpPixelFormat::kB4G4R4A4:
        for (uint32_t x = 0; x < width; ++x) {
          uint16_t p = static_cast<uint16_t>(src_row[x * 2u]) |
                       (static_cast<uint16_t>(src_row[x * 2u + 1u]) << 8);
          // B4G4R4A4 stores B in bits 3:0, G in 7:4, R in 11:8, A in 15:12.
          uint8_t b = static_cast<uint8_t>((p & 0xFu) * 255u / 15u);
          uint8_t g = static_cast<uint8_t>(((p >> 4) & 0xFu) * 255u / 15u);
          uint8_t r = static_cast<uint8_t>(((p >> 8) & 0xFu) * 255u / 15u);
          uint8_t a = static_cast<uint8_t>(((p >> 12) & 0xFu) * 255u / 15u);
          dst_row[x * 4u + 0u] = r;
          dst_row[x * 4u + 1u] = g;
          dst_row[x * 4u + 2u] = b;
          dst_row[x * 4u + 3u] = a;
        }
        break;
      case DumpPixelFormat::kR10G10B10A2:
        for (uint32_t x = 0; x < width; ++x) {
          uint32_t p = static_cast<uint32_t>(src_row[x * 4u]) |
                       (static_cast<uint32_t>(src_row[x * 4u + 1u]) << 8) |
                       (static_cast<uint32_t>(src_row[x * 4u + 2u]) << 16) |
                       (static_cast<uint32_t>(src_row[x * 4u + 3u]) << 24);
          // R10G10B10A2: bits[9:0]=R, bits[19:10]=G, bits[29:20]=B,
          // bits[31:30]=A
          uint8_t r = static_cast<uint8_t>((p & 0x3FFu) >> 2u);
          uint8_t g = static_cast<uint8_t>(((p >> 10) & 0x3FFu) >> 2u);
          uint8_t b = static_cast<uint8_t>(((p >> 20) & 0x3FFu) >> 2u);
          uint8_t a = static_cast<uint8_t>(((p >> 30) & 0x3u) * 85u);
          dst_row[x * 4u + 0u] = r;
          dst_row[x * 4u + 1u] = g;
          dst_row[x * 4u + 2u] = b;
          dst_row[x * 4u + 3u] = a;
        }
        break;
      default:
        return false;
    }
  }
  return TextureDumpWritePng(path, rgba.data(), width, height);
}

// ---------------------------------------------------------------------------
// BC block encoders
// ---------------------------------------------------------------------------

// Encodes a 4x4 RGBA8 block to BC1. rgba_stride is bytes per row.
// Uses 3-color mode if any pixel is transparent, otherwise 4-color mode.
static void EncodeBC1Block(const uint8_t* rgba, uint32_t rgba_stride,
                           uint8_t* out, bool allow_1bit_alpha = true) {
  // Find bounding-box endpoints in RGB.
  uint8_t min_r = 255u, min_g = 255u, min_b = 255u;
  uint8_t max_r = 0u, max_g = 0u, max_b = 0u;
  bool has_alpha = false;
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      const uint8_t* p = rgba + row * rgba_stride + col * 4u;
      has_alpha |= allow_1bit_alpha && p[3] < 128u;
      if (p[0] < min_r) min_r = p[0];
      if (p[0] > max_r) max_r = p[0];
      if (p[1] < min_g) min_g = p[1];
      if (p[1] > max_g) max_g = p[1];
      if (p[2] < min_b) min_b = p[2];
      if (p[2] > max_b) max_b = p[2];
    }
  }

  // Quantize to RGB565.
  uint16_t c0 = static_cast<uint16_t>(((max_r >> 3u) << 11u) |
                                      ((max_g >> 2u) << 5u) | (max_b >> 3u));
  uint16_t c1 = static_cast<uint16_t>(((min_r >> 3u) << 11u) |
                                      ((min_g >> 2u) << 5u) | (min_b >> 3u));
  if (has_alpha) {
    // BC1 transparency is selected by c0 <= c1, with palette index 3
    // transparent.
    if (c0 > c1) {
      uint16_t tmp = c0;
      c0 = c1;
      c1 = tmp;
    }
    if (c0 == c1 && c1 < 0xFFFFu) {
      ++c1;
    }
  } else {
    // Opaque BC1 uses 4-color mode: c0 must be strictly greater than c1.
    if (c0 < c1) {
      uint16_t tmp = c0;
      c0 = c1;
      c1 = tmp;
    }
    if (c0 == c1 && c0 > 0u) {
      --c1;
    }
  }

  // Expand endpoints back for palette construction.
  uint8_t r[4], g[4], b[4];
  Rgb565Expand(c0, r[0], g[0], b[0]);
  Rgb565Expand(c1, r[1], g[1], b[1]);
  if (c0 > c1) {
    r[2] = static_cast<uint8_t>((2u * r[0] + r[1] + 1u) / 3u);
    g[2] = static_cast<uint8_t>((2u * g[0] + g[1] + 1u) / 3u);
    b[2] = static_cast<uint8_t>((2u * b[0] + b[1] + 1u) / 3u);
    r[3] = static_cast<uint8_t>((r[0] + 2u * r[1] + 1u) / 3u);
    g[3] = static_cast<uint8_t>((g[0] + 2u * g[1] + 1u) / 3u);
    b[3] = static_cast<uint8_t>((b[0] + 2u * b[1] + 1u) / 3u);
  } else {
    r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2u);
    g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2u);
    b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2u);
    r[3] = 0u;
    g[3] = 0u;
    b[3] = 0u;
  }

  // Assign nearest-colour index per pixel.
  uint32_t indices = 0u;
  for (uint32_t row = 0; row < 4u; ++row) {
    for (uint32_t col = 0; col < 4u; ++col) {
      const uint8_t* p = rgba + row * rgba_stride + col * 4u;
      if (has_alpha && p[3] < 128u) {
        indices |= 3u << (2u * (row * 4u + col));
        continue;
      }
      uint32_t best_idx = 0u;
      uint32_t best_dist = ~0u;
      uint32_t color_count = (c0 > c1) ? 4u : 3u;
      for (uint32_t i = 0u; i < color_count; ++i) {
        int dr = static_cast<int>(p[0]) - r[i];
        int dg = static_cast<int>(p[1]) - g[i];
        int db = static_cast<int>(p[2]) - b[i];
        uint32_t dist = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
        if (dist < best_dist) {
          best_dist = dist;
          best_idx = i;
        }
      }
      indices |= best_idx << (2u * (row * 4u + col));
    }
  }

  out[0] = static_cast<uint8_t>(c0 & 0xFFu);
  out[1] = static_cast<uint8_t>(c0 >> 8u);
  out[2] = static_cast<uint8_t>(c1 & 0xFFu);
  out[3] = static_cast<uint8_t>(c1 >> 8u);
  out[4] = static_cast<uint8_t>(indices & 0xFFu);
  out[5] = static_cast<uint8_t>((indices >> 8u) & 0xFFu);
  out[6] = static_cast<uint8_t>((indices >> 16u) & 0xFFu);
  out[7] = static_cast<uint8_t>((indices >> 24u) & 0xFFu);
}

// Encodes a flat 16-element single-channel array (stride=1) to an 8-byte BC4
// block. Used for BC3 alpha and BC4/BC5 format encoding.
static void EncodeBC4Block(const uint8_t* channel, uint8_t* out) {
  uint8_t min_v = 255u, max_v = 0u;
  for (uint32_t i = 0; i < 16u; ++i) {
    if (channel[i] < min_v) min_v = channel[i];
    if (channel[i] > max_v) max_v = channel[i];
  }

  // a0=max, a1=min: always use 8-value interpolation mode (a0 > a1).
  uint8_t a0 = max_v, a1 = min_v;
  uint8_t alphas[8];
  alphas[0] = a0;
  alphas[1] = a1;
  if (a0 > a1) {
    for (uint32_t i = 0u; i < 6u; ++i)
      alphas[2u + i] =
          static_cast<uint8_t>((a0 * (6u - i) + a1 * (i + 1u)) / 7u);
  } else {
    // a0 == a1: all pixels same value; palette entries beyond index 0 unused.
    for (uint32_t i = 2u; i < 8u; ++i) alphas[i] = a0;
  }

  uint64_t bits = 0u;
  for (uint32_t i = 0u; i < 16u; ++i) {
    uint32_t best_idx = 0u;
    uint32_t best_dist = ~0u;
    for (uint32_t j = 0u; j < 8u; ++j) {
      int d = static_cast<int>(channel[i]) - alphas[j];
      uint32_t dist = static_cast<uint32_t>(d * d);
      if (dist < best_dist) {
        best_dist = dist;
        best_idx = j;
      }
    }
    bits |= static_cast<uint64_t>(best_idx) << (i * 3u);
  }

  out[0] = a0;
  out[1] = a1;
  out[2] = static_cast<uint8_t>(bits & 0xFFu);
  out[3] = static_cast<uint8_t>((bits >> 8u) & 0xFFu);
  out[4] = static_cast<uint8_t>((bits >> 16u) & 0xFFu);
  out[5] = static_cast<uint8_t>((bits >> 24u) & 0xFFu);
  out[6] = static_cast<uint8_t>((bits >> 32u) & 0xFFu);
  out[7] = static_cast<uint8_t>((bits >> 40u) & 0xFFu);
}

std::vector<uint8_t> TextureDumpCompressRgbaToBC(const uint8_t* rgba,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 DumpPixelFormat bc_format) {
  if (!rgba || !width || !height) return {};
  if (bc_format != DumpPixelFormat::kBC1 &&
      bc_format != DumpPixelFormat::kBC2 &&
      bc_format != DumpPixelFormat::kBC3 &&
      bc_format != DumpPixelFormat::kBC4 &&
      bc_format != DumpPixelFormat::kBC5) {
    return {};
  }

  uint32_t bytes_per_block = TextureDumpBytesPerBlockOrPixel(bc_format);
  uint32_t blocks_x = (width + 3u) / 4u;
  uint32_t blocks_y = (height + 3u) / 4u;
  uint32_t rgba_stride = width * 4u;
  std::vector<uint8_t> out(blocks_x * blocks_y * bytes_per_block, 0u);

  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      // Build a 4x4 RGBA block, clamping at image edges.
      uint8_t block[4u * 4u * 4u] = {};
      for (uint32_t row = 0; row < 4u; ++row) {
        uint32_t sy = std::min(by * 4u + row, height - 1u);
        for (uint32_t col = 0; col < 4u; ++col) {
          uint32_t sx = std::min(bx * 4u + col, width - 1u);
          const uint8_t* src = rgba + sy * rgba_stride + sx * 4u;
          uint8_t* dst = block + row * 16u + col * 4u;
          dst[0] = src[0];
          dst[1] = src[1];
          dst[2] = src[2];
          dst[3] = src[3];
        }
      }

      uint8_t* dst_block = out.data() + (by * blocks_x + bx) * bytes_per_block;

      if (bc_format == DumpPixelFormat::kBC1) {
        EncodeBC1Block(block, 16u, dst_block);
      } else if (bc_format == DumpPixelFormat::kBC2) {
        // 8-byte explicit 4-bit alpha block.
        for (uint32_t row = 0; row < 4u; ++row) {
          uint16_t alpha_row = 0u;
          for (uint32_t col = 0; col < 4u; ++col) {
            uint8_t a4 = block[row * 16u + col * 4u + 3u] >> 4u;
            alpha_row |= static_cast<uint16_t>(a4 << (col * 4u));
          }
          dst_block[row * 2u] = static_cast<uint8_t>(alpha_row & 0xFFu);
          dst_block[row * 2u + 1u] = static_cast<uint8_t>(alpha_row >> 8u);
        }
        EncodeBC1Block(block, 16u, dst_block + 8u, false);
      } else if (bc_format == DumpPixelFormat::kBC3) {
        // 8-byte BC4 alpha block from alpha channel.
        uint8_t alpha_channel[16u];
        for (uint32_t i = 0u; i < 16u; ++i)
          alpha_channel[i] = block[i * 4u + 3u];
        EncodeBC4Block(alpha_channel, dst_block);
        EncodeBC1Block(block, 16u, dst_block + 8u, false);
      } else if (bc_format == DumpPixelFormat::kBC4) {
        uint8_t red_channel[16u];
        for (uint32_t i = 0u; i < 16u; ++i) {
          red_channel[i] = block[i * 4u];
        }
        EncodeBC4Block(red_channel, dst_block);
      } else {  // kBC5
        uint8_t red_channel[16u];
        uint8_t green_channel[16u];
        for (uint32_t i = 0u; i < 16u; ++i) {
          red_channel[i] = block[i * 4u];
          green_channel[i] = block[i * 4u + 1u];
        }
        EncodeBC4Block(red_channel, dst_block);
        EncodeBC4Block(green_channel, dst_block + 8u);
      }
    }
  }
  return out;
}

std::vector<uint8_t> TextureDumpRgbaToRaw(const uint8_t* rgba, uint32_t width,
                                          uint32_t height,
                                          DumpPixelFormat fmt) {
  if (!rgba || !width || !height) {
    return {};
  }

  switch (fmt) {
    case DumpPixelFormat::kBC1:
    case DumpPixelFormat::kBC2:
    case DumpPixelFormat::kBC3:
    case DumpPixelFormat::kBC4:
    case DumpPixelFormat::kBC5:
      return TextureDumpCompressRgbaToBC(rgba, width, height, fmt);
    default:
      break;
  }

  uint32_t bytes_per_pixel = TextureDumpBytesPerBlockOrPixel(fmt);
  if (!bytes_per_pixel) {
    return {};
  }

  std::vector<uint8_t> out(width * height * bytes_per_pixel);
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* src_row = rgba + y * width * 4u;
    uint8_t* dst_row = out.data() + y * width * bytes_per_pixel;

    switch (fmt) {
      case DumpPixelFormat::kRGBA8:
        std::memcpy(dst_row, src_row, width * 4u);
        break;
      case DumpPixelFormat::kR8:
        for (uint32_t x = 0; x < width; ++x) {
          dst_row[x] = src_row[x * 4u];
        }
        break;
      case DumpPixelFormat::kRG8:
        for (uint32_t x = 0; x < width; ++x) {
          dst_row[x * 2u] = src_row[x * 4u];
          dst_row[x * 2u + 1u] = src_row[x * 4u + 1u];
        }
        break;
      case DumpPixelFormat::kB5G6R5:
        for (uint32_t x = 0; x < width; ++x) {
          const uint8_t* src = src_row + x * 4u;
          uint16_t packed =
              static_cast<uint16_t>(((src[0] >> 3u) << 11u) |
                                    ((src[1] >> 2u) << 5u) | (src[2] >> 3u));
          dst_row[x * 2u] = static_cast<uint8_t>(packed & 0xFFu);
          dst_row[x * 2u + 1u] = static_cast<uint8_t>(packed >> 8u);
        }
        break;
      case DumpPixelFormat::kB5G5R5A1:
        for (uint32_t x = 0; x < width; ++x) {
          const uint8_t* src = src_row + x * 4u;
          uint16_t packed = static_cast<uint16_t>(
              ((src[3] >= 128u ? 1u : 0u) << 15u) | ((src[0] >> 3u) << 10u) |
              ((src[1] >> 3u) << 5u) | (src[2] >> 3u));
          dst_row[x * 2u] = static_cast<uint8_t>(packed & 0xFFu);
          dst_row[x * 2u + 1u] = static_cast<uint8_t>(packed >> 8u);
        }
        break;
      case DumpPixelFormat::kB4G4R4A4:
        for (uint32_t x = 0; x < width; ++x) {
          const uint8_t* src = src_row + x * 4u;
          uint16_t packed = static_cast<uint16_t>(
              ((src[3] >> 4u) << 12u) | ((src[0] >> 4u) << 8u) |
              ((src[1] >> 4u) << 4u) | (src[2] >> 4u));
          dst_row[x * 2u] = static_cast<uint8_t>(packed & 0xFFu);
          dst_row[x * 2u + 1u] = static_cast<uint8_t>(packed >> 8u);
        }
        break;
      case DumpPixelFormat::kR10G10B10A2:
        for (uint32_t x = 0; x < width; ++x) {
          const uint8_t* src = src_row + x * 4u;
          uint32_t r = (uint32_t(src[0]) * 1023u + 127u) / 255u;
          uint32_t g = (uint32_t(src[1]) * 1023u + 127u) / 255u;
          uint32_t b = (uint32_t(src[2]) * 1023u + 127u) / 255u;
          uint32_t a = (uint32_t(src[3]) * 3u + 127u) / 255u;
          uint32_t packed = r | (g << 10u) | (b << 20u) | (a << 30u);
          dst_row[x * 4u] = static_cast<uint8_t>(packed & 0xFFu);
          dst_row[x * 4u + 1u] = static_cast<uint8_t>((packed >> 8u) & 0xFFu);
          dst_row[x * 4u + 2u] = static_cast<uint8_t>((packed >> 16u) & 0xFFu);
          dst_row[x * 4u + 3u] = static_cast<uint8_t>(packed >> 24u);
        }
        break;
      default:
        return {};
    }
  }
  return out;
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
          mip_data[(y * mip_w + x) * 4u + c] = static_cast<uint8_t>(v / 4u);
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
        float v = v00 * (1.0f - tx) * (1.0f - ty) + v10 * tx * (1.0f - ty) +
                  v01 * (1.0f - tx) * ty + v11 * tx * ty;
        dst[(dy * dst_width + dx) * 4u + c] =
            static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
      }
    }
  }
  return dst;
}

}  // namespace gpu
}  // namespace xe
