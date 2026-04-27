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

// Writes RGBA8 pixel data to a PNG file at the given path.
// stride_bytes is the row stride in bytes; pass 0 to use width * 4 (packed).
// Returns true on success.
bool TextureDumpWritePng(const std::filesystem::path& path, const uint8_t* rgba,
                         uint32_t width, uint32_t height,
                         uint32_t stride_bytes = 0);

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
