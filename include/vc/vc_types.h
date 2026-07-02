// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace vc {

using pixel_value = float; // normalised [0.0, 1.0]
using pixel_buffer = std::vector<pixel_value>;
using pixel_buffer_ptr = std::shared_ptr<pixel_buffer>;
using const_pixel_buffer_ptr = std::shared_ptr<const pixel_buffer>;

using image_dim = std::uint32_t;     // width or height in pixels
using channel_count = std::uint32_t; // 1=grey 2=greyA 3=RGB 4=RGBA

} // namespace vc
