// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_gradients.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"

// IMPLEMENTED 2026-11-16. This file was delivered as stubs -- the header, the
// declarations and examples/17_gradients.cpp's acceptance checks written ahead
// of the algorithms, per P2 week 5's division of labour -- and the bodies below
// are the learner's. Reviewed on request; two defects found and fixed, both
// recorded here because both are instructive.
//
// DEFECT 1 -- make_sobel_y HAD ITS OPERANDS REVERSED, and the reason is a trap
// worth keeping. The two APIs that consume these factors want them in
// MIRROR-IMAGE ORDER:
//
//     operator*(column, row)                 -- COLUMN first
//     convolve_separable(src, row, column)   -- ROW first
//
// so make_sobel_y() is derivative_1d_y * smooth_1d_x, while the separable call
// computing the same thing is (smooth_1d_x, derivative_1d_y). Same two factors,
// opposite order. NEITHER CAN BE GOT WRONG SILENTLY -- operator* rejects it by
// ALGEBRA (a row on the left is a 1x1 inner product) and convolve_separable by
// SHAPE -- but being caught is not the same as being documented, so the two
// orders are now stated side by side in vc_gradients.h beside the product
// identities, which is where this was missing.
//
// DEFECT 2 -- THE PAIRING VALIDATION WAS ABSENT, and its dtype half passed
// VACUOUSLY. with_pixels<buf_f32>() already throws invalid_argument out of
// vc_pixel_buffer::as<T>(), so the suite's dtype probe went green with the
// right error code and the wrong function's name on it -- the same vacuous-pass
// failure mode the stub comment was written to prevent, arriving from a
// direction it did not anticipate. The GEOMETRY half did not throw at all: with
// the larger image passed FIRST it was a heap-buffer overflow, confirmed under
// AddressSanitizer. require_matched_pair() below is the fix and checks dtype
// EXPLICITLY so the message names this file.
//
// ON GROUP 3's +50 -- DONE 2026-11-16 (self-reported). Note that a green suite
// was never what discharged it. Box
// 2's number, and box 2 was worked by the scaffolder on request, so that
// assertion is checking someone else's arithmetic until you have landed on 50
// yourself. A green suite says the test and the code agree with each other, not
// that either is right. What is left is a check: work the patch by hand and
// confirm BOTH routes agree -- the 3x3 kernel on 0 0 100 with the /8 applied,
// and the central difference (100 - 0) / 2h at h = 1. If they disagree, the
// test is wrong, not your arithmetic.

namespace vc::pixelops {

namespace {

[[noreturn]] void reject(const char* who, const char* why) {
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           std::string(who) + ": " + why);
}

// gradient_magnitude and gradient_direction take dx and dy as two SEPARATE
// images -- the price of not recomputing them, argued in vc_gradients.h -- so
// the two arguments can disagree. Every way they can is checked here, and the
// header promises exactly that.
//
// DTYPE IS CHECKED EXPLICITLY even though with_pixels<buf_f32>() would throw
// invalid_argument by itself. Leaning on that was the original bug: the check
// was satisfied by a message naming vc_pixel_buffer::as<T>(), which tells a
// caller nothing about which of their two arguments was wrong.
//
// GEOMETRY IS NOT A TIDINESS CHECK. The output writer is sized from dx and the
// loop runs to its length, so a dy SMALLER in any dimension is read past the
// end -- a heap-buffer overflow, not a wrong number.
void require_matched_pair(const vc::vc_image& dx,
                          const vc::vc_image& dy,
                          const char* who) {
    if (dx.pixels()->dtype() != vc::pixel_dtype::f32) {
        reject(who, "dx must be of type buf_f32");
    }
    if (dy.pixels()->dtype() != vc::pixel_dtype::f32) {
        reject(who, "dy must be of type buf_f32");
    }
    if (dx.width() != dy.width()) {
        reject(who, "dx and dy must have the same width");
    }
    if (dx.height() != dy.height()) {
        reject(who, "dx and dy must have the same height");
    }
    if (dx.channels() != dy.channels()) {
        reject(who, "dx and dy must have the same channel count");
    }
}

// SOBEL'S SMOOTHING FACTOR, ONCE. [1,2,1]/4, the binomial approximation to a
// Gaussian of variance 0.5 that gives Sobel its perpendicular blur. It was
// written out in both make_sobel_smooth_1d_x and _y; PRIVATE rather than in
// vc_convolve.h beside k_central_difference_taps, because unlike the central
// difference nothing outside this file needs it -- the fused
// make_gaussian_derivative_1d_* use a real Gaussian, not this.
constexpr std::array<vc::float32, 3> k_sobel_smooth_taps = {0.25F, 0.5F, 0.25F};

} // namespace

// Both read k_central_difference_taps from vc_convolve.h rather than spelling
// the three numbers out. They were literals here until the fused
// make_gaussian_derivative_1d_* needed the same taps; a second copy of a sign
// convention that no test can catch is worth deleting. See that constant's
// comment for why it lives in the lower header.
vc_kernel make_sobel_derivative_1d_x() {
    return vc_kernel{3, 1,
                     kernel_weights{k_central_difference_taps.begin(),
                                    k_central_difference_taps.end()}};
}

vc_kernel make_sobel_derivative_1d_y() {
    return vc_kernel{1, 3,
                     kernel_weights{k_central_difference_taps.begin(),
                                    k_central_difference_taps.end()}};
}

vc_kernel make_sobel_smooth_1d_x() {
    return vc_kernel{
        3, 1,
        kernel_weights{k_sobel_smooth_taps.begin(), k_sobel_smooth_taps.end()}};
}

vc_kernel make_sobel_smooth_1d_y() {
    return vc_kernel{
        1, 3,
        kernel_weights{k_sobel_smooth_taps.begin(), k_sobel_smooth_taps.end()}};
}

// COLUMN FIRST, both of them. See DEFECT 1 above: convolve_separable() takes
// the same two factors in the opposite order.
vc_kernel make_sobel_x() {
    return make_sobel_smooth_1d_y() * make_sobel_derivative_1d_x();
}

vc_kernel make_sobel_y() {
    return make_sobel_derivative_1d_y() * make_sobel_smooth_1d_x();
}

vc::vc_image gradient_x(const vc::vc_image& src, vc_edge_policy policy) {
    return convolve(src, make_sobel_x(), policy);
}

vc::vc_image gradient_y(const vc::vc_image& src, vc_edge_policy policy) {
    return convolve(src, make_sobel_y(), policy);
}

vc::vc_image gradient_magnitude(const vc::vc_image& grad_x,
                                const vc::vc_image& grad_y) {
    require_matched_pair(grad_x, grad_y, "gradient_magnitude");

    vc_image_writer mag(grad_x.width(), grad_x.height(), grad_x.channels(),
                        vc::buf_f32{0.0F});
    // Nested rather than flat so each buffer's dtype is resolved ONCE for the
    // whole loop instead of once per element -- the split vc_image.h argues
    // for between at<T>() and with_pixels<T>().
    grad_x.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_x) {
        grad_y.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_y) {
            mag.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> px_mag) {
                for (std::size_t i = 0; i < px_mag.size(); ++i) {
                    px_mag[i] =
                        std::sqrt(px_x[i] * px_x[i] + px_y[i] * px_y[i]);
                }
            });
        });
    });
    return std::move(mag).seal();
}

vc::vc_image gradient_direction(const vc::vc_image& grad_x,
                                const vc::vc_image& grad_y) {
    require_matched_pair(grad_x, grad_y, "gradient_direction");

    vc_image_writer dir(grad_x.width(), grad_x.height(), grad_x.channels(),
                        vc::buf_f32{0.0F});
    grad_x.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_x) {
        grad_y.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_y) {
            dir.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> px_dir) {
                for (std::size_t i = 0; i < px_dir.size(); ++i) {
                    px_dir[i] = std::atan2(px_y[i], px_x[i]);
                }
            });
        });
    });
    return std::move(dir).seal();
}

// SCAFFOLD -- S1, AND THIS BODY IS YOURS. Four lines or so: validate, walk the
// buffer, compare, write 1.0F or 0.0F. The conventions it has to honour are in
// the header (strictly greater; finite, non-negative threshold; f32 in, f32
// out) and examples/18_edges.cpp asserts every one of them.
//
// IT THROWS std::logic_error, NOT vc_exception, AND THAT IS LOAD-BEARING --
// the same reason as S0's stubs. Group 5 of 18_edges checks that a bad
// threshold is rejected with vc_error_code::invalid_argument; a stub throwing
// vc_exception would satisfy that check while doing nothing. std::logic_error
// cannot. reject() above is the helper the real body should use.
//
// WHERE THE INTERESTING PART IS, and it is not the comparison: groups 6 and 7
// of the suite are two pictures, and they are the evidence for section B's
// "documented limitation". Build this, run it, then LOOK at
// 18_edges_uniform.png and 18_edges_ramped.png before writing a word of the
// section 23 rewrite.
vc::vc_image edge_map(const vc::vc_image& magnitude, float threshold) {
    if (!std::isfinite(threshold) || threshold < 0.0F) {
        reject("edge_map", "threshold must be finite and >= 0");
    }

    if (magnitude.pixels()->dtype() != vc::pixel_dtype::f32) {
        reject("edge_map", "magnitude must be of type buf_f32");
    }

    vc_image_writer edges(magnitude.width(), magnitude.height(),
                          magnitude.channels(), vc::buf_f32{0.0F});
    magnitude.with_pixels<vc::buf_f32>(
        [&](std::span<const vc::buf_f32> px_mag) {
            edges.with_pixels<vc::buf_f32>(
                [&](std::span<vc::buf_f32> px_edges) {
                    for (std::size_t i = 0; i < px_mag.size(); ++i) {
                        if (px_mag[i] > threshold) {
                            px_edges[i] = 1.0F;
                        }
                    }
                });
        });
    return std::move(edges).seal();
}

} // namespace vc::pixelops
