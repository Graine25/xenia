/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_DUMP_H_
#define XENIA_GPU_TEXTURE_DUMP_H_

#include <cstdint>
#include <filesystem>
#include <vector>

namespace xe {
namespace gpu {

// Describes the raw pixel layout of GPU readback data for dump/load dispatch.
enum class DumpPixelFormat : uint8_t {
  kRGBA8 = 0,    // R8G8B8A8_UNORM - 4 bytes/pixel
  kBC1,          // BC1/DXT1 - 8 bytes/block
  kBC2,          // BC2/DXT3 - 16 bytes/block
  kBC3,          // BC3/DXT5 - 16 bytes/block
  kBC4,          // BC4/DXT5A - 8 bytes/block (single-channel)
  kBC5,          // BC5/DXN - 16 bytes/block (two-channel RG)
  kR8,           // R8_UNORM - 1 byte/pixel
  kRG8,          // R8G8_UNORM - 2 bytes/pixel
  kB5G6R5,       // B5G6R5_UNORM / R5G6B5_UNORM - 2 bytes/pixel
  kB5G5R5A1,     // B5G5R5A1_UNORM / A1R5G5B5_UNORM - 2 bytes/pixel
  kB4G4R4A4,     // B4G4R4A4_UNORM - 2 bytes/pixel
  kR10G10B10A2,  // R10G10B10A2/A2B10G10R10 - 4 bytes/pixel
};

// Returns bytes per 4x4 block for BC formats, or bytes per pixel for others.
uint32_t TextureDumpBytesPerBlockOrPixel(DumpPixelFormat fmt);

// Builds a sibling path for one layer/face of a multi-subresource texture.
// Example: ABC.png + ("face", 2) -> ABC_face2.png.
std::filesystem::path TextureDumpSubresourcePath(
    const std::filesystem::path& path, const char* label, uint32_t index);

// Writes RGBA8 pixel data to a PNG file at the given path.
// stride_bytes is the row stride in bytes; pass 0 to use width * 4 (packed).
// Returns true on success.
bool TextureDumpWritePng(const std::filesystem::path& path, const uint8_t* rgba,
                         uint32_t width, uint32_t height,
                         uint32_t stride_bytes = 0);

// Decodes raw GPU readback data (any DumpPixelFormat) to RGBA8 and writes PNG.
// row_pitch: bytes per row of blocks (BC) or bytes per row of pixels (others).
// Returns true on success.
bool TextureDumpRawToPng(const std::filesystem::path& path, const uint8_t* data,
                         uint32_t row_pitch, uint32_t width, uint32_t height,
                         DumpPixelFormat fmt);

// Decodes block-compressed (BC1/BC2/BC3) data to RGBA8 and writes as PNG.
// bc_row_pitch: bytes per row of blocks in bc_data (may include GPU alignment).
// bc_bytes_per_block: 8 for BC1, 16 for BC2/BC3.
// is_bc3: true = BC3 (DXT5 interpolated alpha), false = BC2 (DXT3 explicit
//         alpha). Only meaningful when bc_bytes_per_block == 16.
// Returns true on success.
bool TextureDumpBcToPng(const std::filesystem::path& path,
                        const uint8_t* bc_data, uint32_t bc_row_pitch,
                        uint32_t width, uint32_t height,
                        uint32_t bc_bytes_per_block, bool is_bc3);

// Converts tightly-packed RGBA8 pixel data to the requested dump/load format.
// Returns tightly-packed rows or blocks with no GPU row-pitch alignment.
// Returns an empty vector if the format is unsupported, or on failure.
std::vector<uint8_t> TextureDumpRgbaToRaw(const uint8_t* rgba, uint32_t width,
                                          uint32_t height, DumpPixelFormat fmt);

// Compresses tightly-packed RGBA8 pixel data to BC1, BC2, BC3, BC4, or BC5.
// Returns tightly-packed block data (no GPU row-pitch alignment).
// Returns an empty vector if bc_format is not a BC format, or on failure.
std::vector<uint8_t> TextureDumpCompressRgbaToBC(const uint8_t* rgba,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 DumpPixelFormat bc_format);

// Generates a box-filtered RGBA8 mip chain starting from mip 1.
// Returns a vector of `count` mip levels; mip[0] is the first downsampled
// level (half of base), mip[count-1] is the smallest.
// If count == 0, returns an empty vector.
std::vector<std::vector<uint8_t>> TextureDumpGenerateMips(
    const uint8_t* base_rgba, uint32_t base_width, uint32_t base_height,
    uint32_t count);

// Bilinearly resamples an RGBA8 image to a new size.
// Returns an empty vector on invalid inputs.
std::vector<uint8_t> TextureDumpResizeRgba(const uint8_t* src,
                                           uint32_t src_width,
                                           uint32_t src_height,
                                           uint32_t dst_width,
                                           uint32_t dst_height);

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_DUMP_H_
