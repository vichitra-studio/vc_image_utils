// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_convolve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"

namespace vc::pixelops {
namespace {

// The one place a Gaussian is evaluated. Returns normalised 1-D taps, which
// make_gaussian_1d_x/y wrap in a row/column and make_gaussian() takes the
// outer product of.
//
// Sharing at the TAPS level rather than at the kernel level is what makes the
// 2-D kernel exactly the outer product of the two 1-D ones. The 2-D
// normalising sum factorises, S_2d = Sx * Sy, so normalising each axis once is
// the same as normalising the product -- the separable and full paths then
// agree by construction, which is what the separability test asserts.
//
// RADIUS IS ceil(3 * sigma), AND THE REASON IS THE OUTPUT'S PRECISION, NOT
// THE FLOAT'S. An earlier version of this comment claimed the tail beyond 3
// sigma is below what a float can represent against the centre weight. That
// is wrong by a wide margin: at sigma = 1 the first DISCARDED tap is
// exp(-8) = 3.35e-04 of the centre, where float epsilon is 1.19e-07, and
// taps only vanish against the centre at about 5.66 sigma. Truncating at 3
// sigma throws away mass that a float represents perfectly well.
//
// The real criterion is that the discarded mass stay below one quantisation
// step of the output -- AND THE BIT DEPTH HAS TO BE NAMED, because the answer
// is not the same at 8 and 12 bits, and this project's data is raw:
//
//     radius   width   mass dropped    8-bit (255)    12-bit (4095)
//       2 s      5      9.13e-03        2.33 steps     37.4 steps
//       3 s      7      2.71e-04        0.069 steps     1.11 steps
//       4 s      9      2.99e-06        0.001 steps     0.012 steps
//
// 3 sigma is the knee AT 8 BITS, with a factor of 14 to spare. At 12 bits it
// is marginal -- 1.1 steps, so just over one LSB -- and at 16 it is 17.7
// steps and plainly not enough. Dropping to 2 sigma is visible banding at any
// depth. So 3 is the right default for display-range work and the number to
// revisit if a 12- or 16-bit path ever cares about the last code value;
// 4 sigma costs two more taps per axis and buys a factor of 90.
//
// A SUBTLETY THE TEXTBOOK "99.7%" FIGURE HIDES: that figure is the
// CONTINUOUS integral beyond 3 sigma, so it predicts dropping 0.27%. The
// table says 0.027%, ten times less, because the taps sit at INTEGER
// offsets -- with radius 3 the outermost tap KEPT is at 3 sigma and the first
// one DROPPED is at 4 sigma. The continuous rule charges for the tail from 3
// sigma out; discrete sampling only charges from the next sample out. So
// 99.7% is the right instinct and a pessimistic bound.
//
// WHAT THE DISCARDED MASS ACTUALLY COSTS, given the renormalisation below:
// nothing in brightness. Dividing by the SAMPLED sum redistributes it over
// the taps that were kept, so the error is not "0.027% darker" but a slight
// SHAPE error -- a truncated-then-renormalised Gaussian whose effective sigma
// is a hair under the one requested. Which is the better kind of error, and
// is the second reason the sampled-sum normalisation matters.
//
// CEIL, NOT ROUND. It guarantees radius >= 1 for every sigma > 0, so the
// kernel is never a single tap and always has a centre. At sigma = 0.1 the
// radius is 1 and the two outer taps are exp(-50) ~ 2e-22, which normalises
// to [0, 1, 0] -- an identity, and correct: a Gaussian that narrow IS a
// delta. Width is 2*radius + 1, so always odd, which is what vc_kernel's
// constructor requires and what makes "the centre tap is the origin"
// expressible at all.
kernel_weights gaussian_taps(float sigma) {
    // FINITE, NOT JUST POSITIVE. `!(sigma > 0)` catches 0, negatives and NaN,
    // but +INFINITY sails through it -- and then ceil(3 * inf) is inf, the
    // cast to an integer radius is undefined, and the vector sizing throws
    // std::length_error("vector"). That is not an invalid_argument rejection,
    // the message names nothing the caller passed, and every contract in this
    // library that says "not finite is rejected" was quietly untrue for that
    // one value. Found while writing the S3 Harris contract, which needs this
    // guarantee for both of its scales.
    if (!std::isfinite(sigma) || !(sigma > 0.0F)) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "gaussian_taps: sigma must be finite and "
                               "positive");
    }

    // SIGNED, deliberately. A loop from -radius written with an unsigned type
    // wraps to about 1.8e19 and the body never runs -- no warning, no crash,
    // just an all-zero kernel that then divides by a zero sum. This is the
    // entire reason kernel_offset exists.
    const auto radius = static_cast<kernel_offset>(std::ceil(3.0F * sigma));
    const auto width = static_cast<std::size_t>(2 * radius + 1);

    kernel_weights taps(width);
    const float two_sigma_sq = 2.0F * sigma * sigma;
    for (kernel_offset k = -radius; k <= radius; ++k) {
        const auto kf = static_cast<float>(k);
        taps[static_cast<std::size_t>(k + radius)] =
            std::exp(-(kf * kf) / two_sigma_sq);
    }

    // Normalise by the SAMPLED sum, not the analytic constant. A truncated,
    // sampled Gaussian does not integrate to 1, so dividing by
    // sqrt(2*pi)*sigma leaves the weights summing to slightly under 1 and
    // darkens the image by a fraction of a percent on every blur. Sum what was
    // actually built.
    //
    // Accumulated in double: a 19-tap kernel is 19 additions of small numbers,
    // and the same argument applies here as to the DFT's inner sum.
    const auto total =
        static_cast<float>(std::accumulate(taps.begin(), taps.end(), 0.0));
    for (auto& w : taps) {
        w /= total;
    }
    return taps;
}

} // namespace

vc_kernel::vc_kernel(vc::image_dim width,
                     vc::image_dim height,
                     std::vector<float> weights)
    : width_(width), height_(height), weights_(std::move(weights)) {
    if (width_ == 0 || height_ == 0 || width_ % 2 == 0 || height_ % 2 == 0) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_kernel: width and height must be odd and "
                               "non-zero");
    }
    // Checked HERE rather than left to weights_.at() to report later: a
    // mismatched size means the caller built the kernel wrong, which is a
    // different fault from "this tap is outside the support", and at()'s
    // message would name the wrong cause.
    if (weights_.size() !=
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_kernel: weights.size() must equal "
                               "width * height");
    }
}

float vc_kernel::at(kernel_offset kx, kernel_offset ky) const {
    const kernel_offset x_coord = kx + radius_x();
    const kernel_offset y_coord = ky + radius_y();

    if (x_coord < 0 || x_coord >= static_cast<kernel_offset>(width_) ||
        y_coord < 0 || y_coord >= static_cast<kernel_offset>(height_)) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_kernel::at: tap outside support");
    }

    // Bounds already established above, so the cast to an unsigned index is
    // safe here and only here.
    const auto index = static_cast<std::size_t>(
        y_coord * static_cast<kernel_offset>(width_) + x_coord);
    return weights_[index];
}

vc_kernel vc_kernel::reversed() const {
    kernel_weights reversed_weights(weights_.size());
    std::ranges::reverse_copy(weights_, reversed_weights.begin());
    return {width_, height_, reversed_weights};
}

float vc_kernel::sum() const noexcept {
    // 0.0 rather than 0.0F: the seed's type IS the accumulator's type, so this
    // one character is the difference between summing 441 small numbers in
    // float and in double. The cast back is explicit because -Wconversion is
    // on and a silent double->float narrowing is exactly what that flag is for.
    return static_cast<float>(
        std::accumulate(weights_.begin(), weights_.end(), 0.0));
}

vc_kernel make_box(vc::image_dim width, vc::image_dim height) {
    const auto count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return {width, height,
            kernel_weights(count, 1.0F / static_cast<float>(count))};
}

vc_kernel operator*(const vc_kernel& column, const vc_kernel& row) {
    // (N x 1) * (1 x M) only -- RANK-1 OPERANDS OF ANY SIZE. N and M are
    // independent and need not match; 7x1 * 1x3 -> 7x3 is fine. What is
    // rejected is a 2-D OPERAND, not a mismatched size.
    //
    // Why a 2-D operand is rejected rather than handled: relaxing this to the
    // conformable condition (column.width() == row.height()) would be general
    // matrix multiplication, and matrix multiplication IS NOT COMPOSITION.
    // Check it against the 3x3 delta kernel D, a single 1 in the centre, which
    // any composition must leave its partner unchanged by:
    //
    //   sobel_x . D   keeps only sobel_x's middle COLUMN, which is {0,0,0}.
    //                 So "Sobel, then do nothing" gives the ZERO KERNEL.
    //   D . sobel_x   keeps only the middle ROW -- {0,0,0; .25,0,-.25; 0,0,0}.
    //                 The smoothing is gone.
    //
    // Composing two kernels means CONVOLVING them, and that grows the support
    // to (H1+H2-1) x (W1+W2-1). For a column and a row that is N x M with no
    // flip, because each factor extends along one axis only -- which is why
    // the outer product is exactly where the two readings agree, and why this
    // operator looks like a special case of something larger. It is; the
    // larger thing is convolution, and it needs a name (see the header).
    //
    // The reverse ORDER is rejected for a second, independent reason: it is a
    // 1x1 inner product. Row [1,2,1] times column [1,2,1] is the 1x1 kernel
    // [6], which merely multiplies the image by 6.
    if (column.width() != 1 || row.height() != 1) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "operator*(vc_kernel, vc_kernel): expects COLUMN * ROW -- the "
            "left operand must be 1 tap wide and the right 1 tap tall");
    }

    const vc::image_dim width = row.width();
    const vc::image_dim height = column.height();

    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);

    kernel_weights weights(w * h);
    for (std::size_t j = 0; j < h; ++j) {
        for (std::size_t i = 0; i < w; ++i) {
            weights[(j * w) + i] = column.weights()[j] * row.weights()[i];
        }
    }
    return {width, height, std::move(weights)};
}

vc_kernel make_gaussian(float sigma_x, float sigma_y) {
    // The OUTER PRODUCT of the two 1-D factors, not a fresh 2-D evaluation --
    // and now written as one instead of open-coded. Mathematically identical
    // to a 2-D evaluation -- exp of a sum is a product of exps, and the
    // normalising sums factorise as S_2d = Sx * Sy -- but built this way the
    // full and separable paths agree BY CONSTRUCTION rather than by
    // coincidence. 05_convolve's separability test therefore becomes an
    // independent pin on the result rather than a guard against two
    // hand-written loops drifting apart.
    //
    // COLUMN FIRST, so sigma_y -- the vertical factor -- is the LEFT operand.
    // Swap the operands and the kernel comes out transposed; that test uses
    // INDEPENDENT sigmas (2.0 and 0.8) precisely so a transpose changes the
    // shape and fails loudly rather than cancelling out.
    //
    // No second normalisation: each 1-D factor already sums to 1, so their
    // outer product does too.
    return make_gaussian_1d_y(sigma_y) * make_gaussian_1d_x(sigma_x);
}

vc_kernel make_gaussian_1d_x(float sigma) {
    kernel_weights taps = gaussian_taps(sigma);
    const auto width = static_cast<vc::image_dim>(taps.size());
    return {width, 1, std::move(taps)}; // a ROW
}

vc_kernel make_gaussian_1d_y(float sigma) {
    kernel_weights taps = gaussian_taps(sigma);
    const auto height = static_cast<vc::image_dim>(taps.size());
    return {1, height, std::move(taps)}; // a COLUMN
}

namespace {

// 1-D FULL DISCRETE CONVOLUTION of two tap arrays along ONE axis, which is
// the operation vc_convolve.h's operator* is explicitly NOT -- see that
// declaration's "ONE THING THIS SYMBOL IS NOW NOT AVAILABLE FOR" note, which
// predicted this helper and asked for it to be given a name.
//
// THE TWO OPERATIONS DIFFER IN THE RANK OF THEIR OUTPUT, so neither can stand
// in for the other. operator* is the OUTER product: a 7-tap row times a
// 3-tap column is a 3x7 GRID, 21 weights, a 2-D kernel. This composes along
// a SINGLE axis: 7 taps convolved with 3 gives 9 taps, still 1-D. Handing
// two rows to operator* does not even compile into a wrong answer -- it
// throws, because the left operand must be 1 tap wide.
//
// out[i + j] += a[i] * b[j] IS the convolution sum, not a shortcut for it:
// substitute j = n - i into sum_k a[k] * b[n - k] and this is what is left.
// It is NOT correlation, and the difference is load-bearing here -- reverse
// b and {0.5, 0, -0.5} becomes {-0.5, 0, 0.5}, which negates every gradient
// the composed kernel goes on to produce. The hand-check from the operator*
// note is the symmetric case, where a flip could hide:
//
//     [1,2,1] (x) [1,2,1] = [1,4,6,4,1]        (this operation)
//     [1,2,1]  *  [1,2,1] = a 3x3 grid         (operator*)
//
// Width is a.size() + b.size() - 1, so odd stays odd when both operands are
// odd and the composed kernel keeps its centre tap.
kernel_weights convolve_taps(std::span<const vc::float32> a,
                             std::span<const vc::float32> b) {
    if (a.empty() || b.empty()) {
        // a.size() + b.size() - 1 would wrap to SIZE_MAX and size a vector
        // from it. No caller does this; an empty result fails vc_kernel's
        // odd-and-non-zero width check loudly instead.
        return {};
    }

    kernel_weights out(a.size() + b.size() - 1, 0.0F);
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < b.size(); ++j) {
            out[i + j] += a[i] * b[j];
        }
    }
    return out;
}

// The Gaussian taps composed with k_central_difference_taps -- the SAME three
// numbers vc_gradients' Sobel factors are built from, read from the one place
// they are defined rather than written out again here. Same sign convention
// and same 1/2 normalisation, so a derivative-of-Gaussian gradient is
// directly comparable with a Sobel one. Output width is n + 2, odd whenever
// n is.
kernel_weights gaussian_derivative_taps(float sigma) {
    const kernel_weights g = gaussian_taps(sigma); // validates sigma
    return convolve_taps(g, k_central_difference_taps);
}

} // namespace

vc_kernel make_gaussian_derivative_1d_x(float sigma) {
    kernel_weights taps = gaussian_derivative_taps(sigma);
    const auto width = static_cast<vc::image_dim>(taps.size());
    return {width, 1, std::move(taps)}; // a ROW
}

vc_kernel make_gaussian_derivative_1d_y(float sigma) {
    kernel_weights taps = gaussian_derivative_taps(sigma);
    const auto height = static_cast<vc::image_dim>(taps.size());
    return {1, height, std::move(taps)}; // a COLUMN
}

vc::vc_image convolve(const vc::vc_image& src,
                      const vc_kernel& kernel,
                      vc_edge_policy policy) {

    if (src.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "convolve: src must be of type buf_f32");
    }

    vc_image_writer out{src.width(), src.height(), src.channels(),
                        vc::buf_f32{0.0F}};

    // Hoisted: the radii do not change per pixel, and naming them keeps the
    // loop header narrower than the body it controls.
    const kernel_offset rx = kernel.radius_x();
    const kernel_offset ry = kernel.radius_y();

    // The whole thing runs inside one with_pixels<T>(), so the buffer's dtype
    // is checked once per call rather than once per TAP -- at 13x13 that is
    // 168 redundant checks per pixel per channel avoided.
    src.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        // Channel outermost because the buffer is planar: index() is
        // (ch * height + y) * width + x, so a channel's plane is contiguous.
        for (vc::channel_count ch = 0; ch < src.channels(); ++ch) {
            for (vc::image_dim y = 0; y < src.height(); ++y) {
                for (vc::image_dim x = 0; x < src.width(); ++x) {
                    float acc = 0.0F;
                    for (kernel_offset ky = -ry; ky <= ry; ++ky) {
                        for (kernel_offset kx = -rx; kx <= rx; ++kx) {
                            // MINUS. This is the flip, and it is the whole
                            // difference between this function and correlate().
                            acc += fetch(px, src, x - kx, y - ky, ch, policy) *
                                   kernel.at(kx, ky);
                        }
                    }
                    out.at<vc::buf_f32>(x, y, ch) = acc;
                }
            }
        }
    });

    return std::move(out).seal();
}

vc::vc_image correlate(const vc::vc_image& src,
                       const vc_kernel& kernel,
                       vc_edge_policy policy) {
    return convolve(src, kernel.reversed(), policy);
}

vc::vc_image convolve_separable(const vc::vc_image& src,
                                const vc_kernel& kernel_x,
                                const vc_kernel& kernel_y,
                                vc_edge_policy policy) {
    if (kernel_x.height() != 1 || kernel_y.width() != 1) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "convolve_separable: kernel_x must be a row and "
                               "kernel_y must be a column");
    }
    auto conv_x = convolve(src, kernel_x, policy);
    return convolve(conv_x, kernel_y, policy);
}

} // namespace vc::pixelops
