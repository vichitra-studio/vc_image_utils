// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <memory>

namespace vc {

class vc_pixel_buffer;

// A shared, read-only handle onto a vc_pixel_buffer — vc_image::pixels()'s
// return type. There is deliberately no non-const `pixel_buffer_ptr` sibling:
// the only place that ever held a mutable buffer exclusively
// (vc_image_writer::pixels_) is a std::unique_ptr, not a shared_ptr — nothing
// in this library shares a MUTABLE buffer, so a shared-ownership alias for
// one would have no user and would misstate the ownership model to anyone
// who found it.
using const_pixel_buffer_ptr = std::shared_ptr<const vc_pixel_buffer>;

using image_dim = std::uint32_t;     // width or height in pixels
using channel_count = std::uint32_t; // 1=grey 2=greyA 3=RGB 4=RGBA

// Which concrete type a vc_pixel_buffer currently holds. Declaration order
// must match vc_pixel_buffer's variant alternative order (vc_pixel_buffer.h)
// — dtype() casts data_.index() straight to this enum.
enum class pixel_dtype : std::uint8_t {
    f32, // normalised [0.0, 1.0]
    u8,  // [0, 255]
    u16, // [0, 65535]
};

} // namespace vc
