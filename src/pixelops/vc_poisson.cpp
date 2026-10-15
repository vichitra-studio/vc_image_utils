// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_poisson.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_types.h"
#include "vc/math/vc_dft.h"
#include "vc/math/vc_grid2d.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace vc::pixelops {

namespace {

// ---- TOLERANCES ----
//
// All three are RELATIVE and scaled against the channel's own RMS, never
// absolute. An absolute threshold is wrong at both ends: it rejects a
// legitimately large f and accepts a badly wrong small one. Same reasoning, and
// the same numbers, as vc_conv_frequency.cpp -- so the two do not drift apart.
//
// k_mean_rel_tol compares the MEAN against the RMS. Both are per-sample
// quantities, so the comparison is dimensionless and does not scale with the
// image. A first version compared the mean against tol x rms x COUNT, which is
// n times too loose -- 32x at 8x4, 2048x at 64x32, where it would have accepted
// a 2% DC offset. Every test was green, because the suite only ever fed it gross
// violations; 14_poisson group 20 now pins the scaling itself.
//
// k_imag_rel_tol applies to the worst |imag| of the inverse transform, scaled
// against the OUTPUT's RMS.
//
// NOT the input's alone, which is what vc_conv_frequency.cpp does. THAT
// REASONING DOES NOT TRANSFER, and a first version of this file copied it and
// shipped a bug. The two operators fail in OPPOSITE directions:
//
//     convolve_frequency   the output can COLLAPSE -- a sum-to-zero kernel on a
//                          flat region gives an exactly-zero real part -- so an
//                          output-scaled threshold collapses with it. Scale
//                          against the input.
//
//     poisson_fft          the output can be ENORMOUSLY LARGER than the input.
//                          Near DC the scale is tiny (about -0.0006 at width
//                          256) so dividing amplifies by up to ~1660x, and the
//                          residue grows with the OUTPUT. An input-scaled
//                          threshold is then far too tight.
//
// Measured at 64x32 on a valid zero-mean f with its one-cycle component varied:
// amplitude 1.0 passed, while 3.0 and 10.0 both THREW "the inverse transform is
// not real" on perfectly good input. The suite missed it because its only 64x32
// field happens to use amplitude 1.0; 14_poisson group 21 now uses 10.0.
//
// So: scale by the OUTPUT. convolve_frequency scales by the input for the
// mirror-image reason. Neither rule is "the" rule -- each belongs to the
// direction its operator can degenerate in.
//
// k_rms_floor IS INERT TODAY, IN BOTH CHECKS, and it is worth saying so rather
// than implying a hazard it prevents. Two earlier comments here claimed it
// guarded the mean threshold, then the imaginary one; mutation refuted both --
// removing it from either leaves all 64 checks green.
//
// The reason is the same each time. An RMS of exactly zero means every sample is
// zero, so the quantity being compared against it is zero too, and `0 > 0` is
// false. The all-zero case accepts itself with or without a floor. In the one
// scenario where an output RMS of zero could coexist with a non-zero residue --
// a purely imaginary output, which only a bug produces -- the floor makes the
// check marginally LESS sensitive, not more protective.
//
// It is kept for a refactor that has not happened: the moment the comparison
// becomes a ratio (`residue / out_rms > tol`) rather than a product, an output
// RMS of zero divides by zero. That is a real hazard and a cheap guard, but it
// is a FUTURE one, and the comment should not pretend otherwise.
//
// MEASURED, NOT CHOSEN. Worst observed ratios over a zero-mean round trip,
// f = laplacian(phi, wrap) then poisson_fft, on a multi-frequency field:
//
//     grid         worst |mean|/RMS     worst max|imag| / OUTPUT RMS
//       8 x 4        --                   3.2e-07
//      64 x 32       --                   1.5e-05
//      64 x 64       1.9e-10              1.8e-05
//     128 x 128      1.3e-09              9.0e-06
//     256 x 256      3.3e-10              3.6e-12
//
// k_mean_rel_tol has five orders of headroom and does not grow with the grid.
//
// k_imag_rel_tol IS 1e-3 AND THAT IS A DELIBERATE CHANGE FROM 1e-4. An earlier
// version matched vc_conv_frequency.cpp's 1e-4 on the grounds that copying a
// sibling beat guessing. Measuring settled it instead: against the correct
// denominator the worst observed residue is 1.8e-05, so 1e-4 leaves only about
// 6x of headroom -- far too tight for a tripwire. A GENUINE symmetry break
// produces a residue of order 1 relative to the output, not 1e-5, so there are
// five orders of separation to sit in. 1e-3 puts roughly 60x below the
// threshold and 1000x above it, which is centred; 1e-4 was not.
//
// Note the column is NOT monotone in size -- it tracks the field's content, not
// the grid.
//
// ---- A SEPARATE MEASUREMENT, AND IT LIMITS THE ACCEPTANCE GATE ----
//
// The ROUND-TRIP recovery error, |phi_recovered - phi| worst case over RMS, on
// the same multi-frequency field:
//
//     64 x 64     3.3e-06        128 x 128   4.8e-05        256 x 256   2.0e-04
//
// IT GROWS WITH THE GRID, and at 256 squared it is twenty times the 1e-5 that
// P2's acceptance criterion states. The criterion is therefore size-dependent
// and the plan does not say so.
//
// The cause is the near-DC amplification the header describes, confirmed by
// removing the one-cycle component from the same field: 256 squared then drops
// from 2.0e-04 to 1.3e-05, a factor of 16. Bin 1 at width 256 has a scale of
// about -0.0006, so dividing by it multiplies that bin -- and the float error in
// it -- by roughly 1660. Nothing is wrong with the solver; this is an f32 direct
// transform meeting a genuinely ill-conditioned bin.
//
// 14_poisson therefore tests at 64x32 and smaller, where 1e-5 holds with margin.
constexpr float k_mean_rel_tol = 1e-5F;
constexpr float k_imag_rel_tol = 1e-3F;
constexpr float k_rms_floor = 1e-12F;

// Shared by poisson_fft and poisson_iter so the two CANNOT DRIFT APART: one
// tolerance, one scaling rule, one message shape, and only the caller's name
// differs. 15_poisson_iter asserts the message names poisson_iter, so a
// copy-paste that forgot to change the string fails rather than passing.
//
// Takes the accumulated sum and rms rather than the image, because both callers
// already compute them for other purposes and a second pass would be waste.
void require_zero_mean(double sum,
                       double rms,
                       double count,
                       vc::channel_count ch,
                       const char* caller) {
    const double scale_floor = std::max(rms, static_cast<double>(k_rms_floor));
    if (std::abs(sum / count) >
        static_cast<double>(k_mean_rel_tol) * scale_floor) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            std::string{caller} +
                ": f must have zero mean per channel, but channel " +
                std::to_string(ch) + " has mean " +
                std::to_string(sum / count) + " against RMS " +
                std::to_string(rms));
    }
}

} // namespace

// The DISCRETE scale, which is the stencil's own transform -- never the
// continuous -(u^2 + v^2). That one is this one's small-angle approximation and
// is 2.5x off at the highest frequency, exactly where edges live; the header
// has the table. Real, with no rotation, because the stencil is symmetric.
float laplacian_scale(vc::math::grid_dim k,
                      vc::math::grid_dim l,
                      const vc::math::grid2d& extent) {
    // An out-of-range bin is a caller bug. Wrapping it silently would hide the
    // class of indexing error that cost a heap-buffer-overflow in dft2d.
    if (k >= extent.width() || l >= extent.height()) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "laplacian_scale: k or l out of range");
    }

    const float turn = 2.0F * std::numbers::pi_v<float>;
    const float wx =
        turn * static_cast<float>(k) / static_cast<float>(extent.width());
    const float wy =
        turn * static_cast<float>(l) / static_cast<float>(extent.height());
    return -4.0F + (2.0F * std::cos(wx)) + (2.0F * std::cos(wy));
}

// Solve grad^2 phi = f, periodic, returning the zero-mean solution.
//
// Three steps, and two of them are dft2d and idft2d. Everything else in this
// function is validation, which is where all of its defects have lived so far.
//
// EVERY REJECTION MESSAGE NAMES THIS FUNCTION, and 14_poisson asserts that it
// does. Found by mutation: with the f32 check deleted the test still passed,
// because at<buf_f32>() on a u8 buffer throws invalid_argument by itself.
// "It threw" is not evidence that THIS function rejected anything.
vc::vc_image poisson_fft(const vc::vc_image& f_input) {
    if (f_input.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "poisson_fft: f must be of type buf_f32");
    }

    // One contiguous f32 plane per channel. The equation does not couple
    // channels, so each is solved on its own from here down.
    std::vector<vc_image> channels;
    channels.reserve(f_input.channels());
    for (vc::channel_count ch = 0; ch < f_input.channels(); ++ch) {
        vc_image_writer channel{f_input.width(), f_input.height(), 1,
                                vc::buf_f32{0.0F}};
        for (vc::image_dim y = 0; y < f_input.height(); ++y) {
            for (vc::image_dim x = 0; x < f_input.width(); ++x) {
                channel.at<vc::buf_f32>(x, y, 0) =
                    f_input.at<vc::buf_f32>(x, y, ch);
            }
        }
        channels.emplace_back(std::move(channel).seal());
    }

    vc_image_writer out{f_input.width(), f_input.height(), f_input.channels(),
                        vc::buf_f32{0.0F}};

    // NO PADDING. The periodic problem is posed on exactly this grid -- unlike
    // convolve_frequency, which pads to N+M-1 to escape the wraparound. Here the
    // wraparound IS the problem, and padding would change which wave each bin
    // denotes and so every scale below.
    //
    // The count is formed in element_count, not by multiplying two uint32s:
    // vc_grid2d.h prefers the span overload precisely so a count cannot be
    // supplied wrongly, and a 32-bit product overflows at 65536 square.
    const vc::math::grid2d extent = vc::math::grid2d::checked(
        static_cast<vc::element_count>(f_input.width()) * f_input.height(),
        f_input.width(), f_input.height(), "poisson_fft");

    for (vc::channel_count ch = 0; ch < f_input.channels(); ++ch) {
        const vc::vc_image& plane = channels[ch];
        const auto buffer = plane.pixels()->as<vc::buf_f32>();
        const auto count = static_cast<double>(plane.pixel_count());

        // Accumulated in double on purpose: a float sum over 65k samples loses
        // precision exactly where this check needs it. -Wdouble-promotion is on,
        // so every f32 read is cast explicitly rather than promoted implicitly.
        const double sum = std::accumulate(
            buffer.begin(), buffer.end(), 0.0, [](double acc, vc::buf_f32 v) {
                return acc + static_cast<double>(v);
            });
        const double ssq = std::accumulate(
            buffer.begin(), buffer.end(), 0.0, [](double acc, vc::buf_f32 v) {
                const auto d = static_cast<double>(v);
                return acc + (d * d);
            });
        const double rms = std::sqrt(ssq / count);

        // NOT an equality against zero. f comes out of a float transform and
        // carries noise -- measured, sum(f) is around 2.4e-06 on an 8x4 field
        // where the exact answer is 0. A first version tested `sum != 0.0` and
        // rejected every real input.
        require_zero_mean(sum, rms, count, ch, "poisson_fft");

        vc::math::vc_complex_signal spectrum =
            vc::math::dft2d(vc::math::to_signal(buffer), extent);

        for (vc::math::grid_dim l = 0; l < extent.height(); ++l) {
            for (vc::math::grid_dim k = 0; k < extent.width(); ++k) {
                // Bin 0 is NOT DIVIDED -- its scale is exactly zero, because the
                // stencil annihilates constants. Zeroing it and skipping it are
                // the same operation once the check above has passed, since F[0]
                // is then already zero. What matters is only that no division
                // happens; dividing fills the image with NaN.
                if (k == 0 && l == 0) {
                    continue;
                }
                spectrum[(static_cast<std::size_t>(l) * extent.width()) + k] /=
                    laplacian_scale(k, l, extent);
            }
        }

        const vc::math::vc_complex_signal result =
            vc::math::idft2d(spectrum, extent);

        double out_ssq = 0.0;
        for (const auto& bin : result) {
            const auto re = static_cast<double>(bin.real());
            out_ssq += re * re;
        }
        const double out_rms = std::sqrt(out_ssq / count);

        // A real f has a twin-symmetric spectrum, and dividing by a REAL scale
        // preserves that, so the inverse is real to within float noise. Checking
        // before discarding the imaginary part is free, and calling .real()
        // blind throws away the only evidence that something upstream was not
        // symmetric.
        //
        // RELATIVE, against the INPUT's RMS. A first version compared against an
        // absolute 1e-12 -- far below float noise, so it rejected every
        // non-trivial input.
        double worst_imag = 0.0;
        for (const auto& bin : result) {
            worst_imag =
                std::max(worst_imag, std::abs(static_cast<double>(bin.imag())));
        }
        // The OUTPUT's RMS, not the max of input and output. The residue is
        // produced by the divided spectrum, so it tracks the output and nothing
        // else; taking the max with the input only loosens the threshold, and
        // mutation showed it also MASKS a broken out_rms -- computing it from
        // imag() instead of real() left all 64 checks green, because the max
        // fell back on the input. Scaling by the output alone makes out_rms
        // load-bearing, so that mutation is now caught.
        const double imag_scale =
            std::max(out_rms, static_cast<double>(k_rms_floor));
        if (worst_imag > static_cast<double>(k_imag_rel_tol) * imag_scale) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                "poisson_fft: the inverse transform is not real -- max|imag| " +
                    std::to_string(worst_imag) + " against a scale of " +
                    std::to_string(imag_scale) + " (input RMS " +
                    std::to_string(rms) + ", output RMS " +
                    std::to_string(out_rms) +
                    "), which means the spectrum was not twin-symmetric");
        }

        for (vc::image_dim y = 0; y < plane.height(); ++y) {
            for (vc::image_dim x = 0; x < plane.width(); ++x) {
                const auto idx =
                    (static_cast<std::size_t>(y) * plane.width()) + x;
                out.at<vc::buf_f32>(x, y, ch) =
                    static_cast<vc::buf_f32>(result[idx].real());
            }
        }
    }

    return std::move(out).seal();
}

// ---------------------------------------------------------------------------
// S8a -- NOT IMPLEMENTED. The body is yours; the contract is in the header.
//
// What the header commits to, restated as a checklist so nothing is inferred:
//
//   1. reject non-f32 f, naming poisson_iter
//   2. reject max_sweeps == 0, and reject !(tolerance >= 0) so NaN is caught
//   3. reject a channel whose mean is not zero -- call require_zero_mean above,
//      do not re-derive the tolerance
//   4. start from phi = 0 everywhere
//   5. apply (L + R + U + D - f) / 4 under WRAP at every size, including a
//      one-row image, where up and down are the pixel itself
//   6. jacobi reads the previous sweep; gauss_seidel reads and writes one
//      buffer in place, raster order, y outer, x inner, ascending
//   7. subtract each channel's mean AFTER every sweep
//   8. record the relative residual once per sweep, worst channel, with
//      residual[0] the value BEFORE any sweep (which is exactly 1.0)
//   9. stop early when every channel is at or under tolerance, and report it
//  10. switch over poisson_sweep with NO default label
// ---------------------------------------------------------------------------
poisson_iter_report poisson_iter(const vc::vc_image& f,
                                 poisson_sweep sweep,
                                 sweep_count max_sweeps,
                                 float tolerance) {
    (void)f;
    (void)sweep;
    (void)max_sweeps;
    (void)tolerance;
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "poisson_iter: not implemented");
}

} // namespace vc::pixelops
