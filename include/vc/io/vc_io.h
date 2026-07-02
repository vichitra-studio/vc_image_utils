// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/io/vc_io_types.h"
#include "vc/vc_image.h"

namespace vc::io {

class i_image_reader {
  public:
    virtual ~i_image_reader() = default;
    // clang-format off
    virtual vc::vc_image read(const path& p,
                              const read_config& config = {}) = 0;
    // clang-format on
};

class i_image_writer {
  public:
    virtual ~i_image_writer() = default;
    // clang-format off
    virtual void write(const path& p,
                       const vc::vc_image& image,
                       const write_config& config = {}) = 0;
    // clang-format on
};

// TODO(you): implement both in src/io/vc_io_stb.cpp.
//   - read(): stbi_load(..., desired_channels=0), divide each byte by
//     255.0f, construct a vc::vc_image, throw vc::vc_exception on failure
//     (check stbi_failure_reason()).
//   - write(): multiply each float by 255.0f, round with +0.5f before
//     casting to uint8_t, call stbi_write_png with stride = width * channels.
//   See docs/coding_guidelines.md Sec 7.4.
class stb_image_reader : public i_image_reader {
  public:
    // clang-format off
    vc::vc_image read(const path& p,
                      const read_config& config = {}) override;
    // clang-format on
};

class stb_image_writer : public i_image_writer {
  public:
    // clang-format off
    void write(const path& p,
               const vc::vc_image& image,
               const write_config& config = {}) override;
    // clang-format on
};

} // namespace vc::io
