// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/vc_image.h"

#include "vc/vc_pixel_buffer.h"
#include "vc/vc_types.h"

namespace vc {

vc_image vc_image::zeros(image_dim width, image_dim height,
                         channel_count channels) {
    // zeros() is just with_fill() pinned to buf_f32{0.0f} — funnels through the
    // same single construction path, no separate allocation logic here.
    return with_fill(width, height, channels, vc::buf_f32{0.0f});
}

} // namespace vc
