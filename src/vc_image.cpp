// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/vc_image.h"

// TODO(you): you will need these two includes once you implement the
// constructor's validation step (throwing vc::vc_exception on bad input).
// #include "vc/vc_error_code.h"
// #include "vc/vc_exception.h"

namespace vc {

// clang-format off
vc_image::vc_image(vc::image_dim width,
                   vc::image_dim height,
                   vc::channel_count channels) {
    // clang-format on
    // TODO(you): implement — see the TODO comment on this constructor's
    // declaration in include/vc/vc_image.h for the exact steps.
    //
    // Left unimplemented for now: width_/height_/channels_ stay at their
    // in-class default of 0, and pixels_ stays null. This means
    // pixel_count()/pixels() below will look "wrong" (not a compile error)
    // until this constructor is finished — that's intentional; the test
    // suite (tests/test_vc_image.cpp) will fail loudly until you implement
    // this properly.
}

std::size_t vc_image::pixel_count() const noexcept {
    return 0; // TODO(you): width_ * height_ * channels_ (overflow-safe — see Sec 6.3)
}

vc::const_pixel_buffer_ptr vc_image::pixels() const noexcept {
    return nullptr; // TODO(you): return pixels_ (implicit shared_ptr<T> -> shared_ptr<const T>)
}

vc::pixel_buffer_ptr vc_image::mutable_pixels() noexcept {
    return nullptr; // TODO(you): return pixels_
}

} // namespace vc
