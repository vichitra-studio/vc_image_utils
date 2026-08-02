// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <filesystem>

#include "vc/core/vc_types.h"

namespace vc::io {

using path = std::filesystem::path;

enum class vc_image_format : std::uint8_t {
    png,  // PNG (lossless, supports all channel counts)
    jpeg, // JPEG (lossy, RGB only — alpha stripped)
    // future: tiff, exr, dng, raw, ...
};

struct read_config {
    // no fields yet — stb always loads the file's native channel count
    // (desired_channels = 0), see docs/coding_guidelines.md Sec 7.4
    vc::pixel_dtype dtype =
        vc::pixel_dtype::u8; // future: allow caller to request a dtype
};

struct write_config {
    vc_image_format format = vc_image_format::jpeg;
    int jpeg_quality = 100; // JPEG quality, 1-100 (ignored for PNG)
};

} // namespace vc::io
