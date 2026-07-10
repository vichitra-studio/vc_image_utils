// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>

#include "vc/vc_types.h"

namespace vc {

class vc_image {
  public:
    // TODO(you): implement in src/vc_image.cpp.
    //   1. Validate: width == 0 || height == 0 || channels == 0 || channels > 4
    //      -> throw vc::vc_exception(vc::vc_error_code::invalid_argument,
    //      "...")
    //   2. Allocate: cast the FIRST operand to std::size_t before multiplying
    //      width * height * channels (uint32_t arithmetic can overflow before
    //      a later cast would help) ->
    //      std::make_shared<vc::vc_pixel_buffer>(count, vc::buf_f32{0.0f})
    //   3. Assign width_, height_, channels_
    //   See docs/coding_guidelines.md Sec 6.3.
    vc_image(vc::image_dim width,
             vc::image_dim height,
             vc::channel_count channels);

    vc::image_dim width() const noexcept {
        return width_;
    }
    vc::image_dim height() const noexcept {
        return height_;
    }
    vc::channel_count channels() const noexcept {
        return channels_;
    }

    // TODO(you): total number of elements in the buffer, independent of
    // dtype (width * height * channels). Mind overflow — see Sec 6.3.
    std::size_t pixel_count() const noexcept;

    // TODO(you): read-only access. Must not allow mutation through the
    // returned handle. See docs/coding_guidelines.md Sec 4.2.
    vc::const_pixel_buffer_ptr pixels() const noexcept;

    // TODO(you): explicit mutation path — caller's responsibility not to
    // break the width*height*channels == size() invariant. See Sec 4.2.
    vc::pixel_buffer_ptr mutable_pixels() noexcept;

  private:
    vc::image_dim width_ = 0;
    vc::image_dim height_ = 0;
    vc::channel_count channels_ = 0;
    vc::pixel_buffer_ptr pixels_;
};

} // namespace vc
