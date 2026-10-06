// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_spectrum_viz.h"

#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vc::pixelops {

spectrum_dump spectrum_viz(vc::math::complex_view spectrum,
                           vc::math::grid2d extent) {
    // TODO(you). The shape of it:
    //
    //   1. shifted = vc::math::fftshift2d(spectrum, extent)
    //   2. walk shifted once:  mag = std::abs(z)
    //                          ph  = std::atan2(z.imag(), z.real())
    //      keeping max of mag, max of log(1+mag), and every mag for the median
    //   3. three vc_image_writer{w, h, 1, vc::buf_f32{0.0F}} grids,
    //      filled with:
    //          log(1+mag) / max_log
    //          mag / max_mag
    //          (ph + pi) / (2 pi)
    //      then std::move(w).seal()
    //   4. the four diagnostic numbers. linear_zero_fraction counts bins where
    //      mag/max_mag * 255 + 0.5 truncates to 0 -- i.e. what the PNG will
    //      actually show, not what the float says.
    //
    // Guard the divisions: an all-zero spectrum has max 0, and the header
    // now pins that case -- three black images, every diagnostic 0. Likewise
    // dc_to_median when the median is 0, which happens on any sparse
    // synthetic spectrum.
    (void)spectrum;
    (void)extent;
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "spectrum_viz: not implemented yet");
}

} // namespace vc::pixelops
