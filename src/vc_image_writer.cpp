// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/vc_image_writer.h"

#include <cassert>
#include <utility>

#include "vc/vc_image.h"

// TODO(you): once validated() throws, you will want these for the check:
//   #include "vc/vc_error_code.h"
//   #include "vc/vc_exception.h"

namespace vc {

vc_image_info vc_image_writer::validated(image_dim width,
                                         image_dim height,
                                         channel_count channels) {
    // TODO(you): this is the one rep left in the image core — the same
    // validation the original vc_image constructor owned. Reject a degenerate or
    // unsupported geometry BEFORE the descriptor is built:
    //   width == 0 || height == 0 || channels == 0 || channels > 4
    //     -> throw vc::vc_exception(vc::vc_error_code::invalid_argument, "...")
    // Until this throws, vc_image_writer{0, 2, 3, ...} constructs a degenerate
    // image instead of failing — so the "rejects invalid dimensions" test stays
    // red on purpose. See docs/coding_guidelines.md Sec 6.3.
    return vc_image_info{width, height, channels};
}

vc_image vc_image_writer::seal() && {
    // A spent writer (already sealed, or moved-from) holds a null buffer;
    // sealing it would mint a vc_image with null pixels() — catch that misuse
    // loudly in debug (compiled out in release, off the hot path).
    assert(pixels_ && "seal() called on a spent writer (already sealed / moved-from)");
    // std::move(pixels_) is a shared_ptr<vc_pixel_buffer>; vc_image's ctor takes
    // const_pixel_buffer_ptr (shared_ptr<const vc_pixel_buffer>) — the
    // qualifying conversion is implicit and refcount-only. pixels_ is left null:
    // the writer is spent.
    return vc_image{meta_, std::move(pixels_)};
}

} // namespace vc
