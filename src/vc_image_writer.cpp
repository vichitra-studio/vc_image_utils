// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/vc_image_writer.h"

#include <cassert>
#include <utility>

#include "vc/vc_image.h"

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc {

vc_image_info vc_image_writer::validated(image_dim width,
                                         image_dim height,
                                         channel_count channels) {
    if (width == 0) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_image_writer::validated: width "
                               "must be positive non-zero");
    }
    if (height == 0) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_image_writer::validated: height "
                               "must be positive non-zero");
    }
    if (channels == 0) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_image_writer::validated: channels "
                               "must be positive non-zero");
    }
    if (channels > 4) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_image_writer::validated: channels "
                               "max of 4 channels supported");
    }
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
