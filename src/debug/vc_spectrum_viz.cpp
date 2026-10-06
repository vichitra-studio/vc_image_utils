// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/debug/vc_spectrum_viz.h"

#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/math/vc_dft.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace vc::debug {

namespace {

constexpr float k_pi = std::numbers::pi_v<float>;

// A bin is lost on a linear dump when the PNG writer rounds it to grey 0.
// That path is std::clamp(v * 255 + 0.5, 0, 255), so the threshold is
// v * 255 + 0.5 < 1, i.e. v < 1/510. Counted against what will actually be
// shown rather than against the float.
constexpr float k_grey_floor = 1.0F / 510.0F;

} // namespace

spectrum_dump spectrum_viz(vc::math::vc_complex_view spectrum,
                           vc::math::grid2d extent) {
    const vc::image_dim w = extent.width();
    const vc::image_dim h = extent.height();

    // dc_magnitude is bin 0 of the UNSHIFTED input, per the header. Read it
    // before the shift moves it, and note grid2d cannot be built with a zero
    // dimension, so there is always a bin 0 to read.
    const float dc_magnitude = std::abs(spectrum[0]);

    const vc::math::vc_complex_signal shifted =
        vc::math::fftshift2d(spectrum, extent);

    vc_image_writer log_magnitude{w, h, 1, vc::buf_f32{0.0F}};
    vc_image_writer linear_magnitude{w, h, 1, vc::buf_f32{0.0F}};
    vc_image_writer phase{w, h, 1, vc::buf_f32{0.0F}};

    float max_mag = 0.0F;
    float max_log = 0.0F;

    std::vector<float> mags;
    mags.reserve(shifted.size());

    // Nested x/y rather than one flat loop with i / w and i % w. Same work,
    // but the coordinates are the loop variables instead of being recovered
    // by division -- which is also the shape every later 2-D pass wants.
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            const vc::math::vc_complex z =
                shifted[static_cast<std::size_t>(y) * w + x];

            const float mag = std::abs(z);
            const float log_mag = std::log1p(mag);

            max_mag = std::max(max_mag, mag);
            max_log = std::max(max_log, log_mag);
            mags.push_back(mag);

            // CHANNEL 0. These are single-channel images, so 0 is the only
            // valid index -- at(x, y, 1) resolves to (y*w + x)*1 + 1, which
            // is the NEXT pixel's slot, and one past the buffer on the last
            // pixel. An out-of-bounds write that compiles and does not warn.
            log_magnitude.at<buf_f32>(x, y, 0) = log_mag;
            linear_magnitude.at<buf_f32>(x, y, 0) = mag;

            // atan2 returns (-pi, pi]; a pixel must be 0..1. This is the
            // linear rescale: -pi -> 0, 0 -> 0.5, +pi -> 1. Without it every
            // negative phase would clamp to black and half the information
            // would be gone.
            phase.at<buf_f32>(x, y, 0) = (std::atan2(z.imag(), z.real()) + k_pi) /
                                         (2.0F * k_pi);
        }
    }

    // An all-zero spectrum -- the transform of a black image -- has max 0.
    // Legitimate input, so no throw: leave both magnitude images at the 0.0F
    // they were constructed with rather than dividing by it.
    if (max_mag > 0.0F) {
        for (vc::image_dim y = 0; y < h; ++y) {
            for (vc::image_dim x = 0; x < w; ++x) {
                log_magnitude.at<buf_f32>(x, y, 0) /= max_log;
                linear_magnitude.at<buf_f32>(x, y, 0) /= max_mag;
            }
        }
    }

    // The element at index count/2 of the ascending sort -- the UPPER middle
    // for an even count, no averaging, all bins included. nth_element rather
    // than a full sort: it is linear, and only that one position is wanted.
    const std::size_t mid = mags.size() / 2;
    std::nth_element(mags.begin(), mags.begin() + static_cast<std::ptrdiff_t>(mid),
                     mags.end());
    const float median_magnitude = mags[mid];

    // Zero when the median is zero, which means UNDEFINED and not small. Any
    // sparse synthetic spectrum lands here -- a cosine on an 8x4 grid has 30
    // of its 32 bins exactly zero.
    const float dc_to_median =
        median_magnitude > 0.0F ? dc_magnitude / median_magnitude : 0.0F;

    // Counted on the NORMALISED values, because that is what the PNG holds.
    // With max_mag == 0 every normalised value is 0, so this comes out 1.0
    // without a special case: a dump of nothing loses every bin.
    std::size_t lost = 0;
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            if (linear_magnitude.at<buf_f32>(x, y, 0) < k_grey_floor) {
                ++lost;
            }
        }
    }
    const float linear_zero_fraction =
        static_cast<float>(lost) / static_cast<float>(mags.size());

    return spectrum_dump{
        .log_magnitude = std::move(log_magnitude).seal(),
        .linear_magnitude = std::move(linear_magnitude).seal(),
        .phase = std::move(phase).seal(),
        .dc_magnitude = dc_magnitude,
        .median_magnitude = median_magnitude,
        .dc_to_median = dc_to_median,
        .linear_zero_fraction = linear_zero_fraction,
    };
}

} // namespace vc::debug
