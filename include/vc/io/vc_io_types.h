// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <string>

namespace vc::io {

using path = std::string; // filesystem path — upgrade to std::filesystem::path at P4

enum class vc_image_format : std::uint8_t {
    png,  // PNG (lossless, supports all channel counts)
    jpeg, // JPEG (lossy, RGB only — alpha stripped)
    bmp,  // BMP (uncompressed, no alpha)
    // future: tiff, exr, dng, raw, ...
};

struct read_config {
    // no fields yet — stb always loads the file's native channel count
    // (desired_channels = 0), see docs/coding_guidelines.md Sec 7.4
};

struct write_config {
    vc_image_format format = vc_image_format::png;
};

} // namespace vc::io
