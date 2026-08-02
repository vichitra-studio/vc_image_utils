// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/core/vc_image_writer.h"

#include <cassert>
#include <utility>

#include "vc/core/vc_image.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

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
    assert(pixels_ &&
           "seal() called on a spent writer (already sealed / moved-from)");
    // std::move(pixels_) is a std::unique_ptr<vc_pixel_buffer>; vc_image's
    // ctor takes const_pixel_buffer_ptr (shared_ptr<const vc_pixel_buffer>).
    // shared_ptr's unique_ptr-converting constructor ([util.smartptr.shared]
    // — non-explicit, so this braced call resolves it implicitly) takes over
    // the buffer and allocates a NEW control block here: this is the one
    // point where ownership goes from exclusive to shared, so it is also the
    // one point where sharing's bookkeeping is first paid for. pixels_ is
    // left null: the writer is spent.
    return vc_image{std::move(meta_), std::move(pixels_)};
}

} // namespace vc
