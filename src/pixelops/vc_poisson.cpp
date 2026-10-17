// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_poisson.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_scalar.h"
#include "vc/core/vc_types.h"
#include "vc/math/vc_dft.h"
#include "vc/math/vc_grid2d.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <optional>
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

//TODO: Should be part of vc_image?
double get_rms(const vc_image& plane) {
    const auto buffer = plane.pixels()->as<vc::buf_f32>();
    const auto count = static_cast<double>(plane.pixel_count());
    const double ssq = std::accumulate(
        buffer.begin(), buffer.end(), 0.0, [](double acc, vc::buf_f32 v) {
            const auto d = static_cast<double>(v);
            return acc + (d * d);
        });
    return std::sqrt(ssq / count);
}

//TODO: Should be part of vc_image?
double get_mean(const vc_image& plane) {
    const auto buffer = plane.pixels()->as<vc::buf_f32>();
    const auto count = static_cast<double>(plane.pixel_count());
    const double sum = std::accumulate(buffer.begin(), buffer.end(), 0.0);
    return sum / count;
}

// Shared by poisson_fft and poisson_iter so the two CANNOT DRIFT APART: one
// tolerance, one scaling rule, one message shape, and only the caller's name
// differs. 15_poisson_iter asserts the message names poisson_iter, so a
// copy-paste that forgot to change the string fails rather than passing.
//
// Takes the accumulated sum and rms rather than the image, because both callers
// already compute them for other purposes and a second pass would be waste.
void require_zero_mean(const vc_image& plane, const char* caller) {
    const double mean = get_mean(plane);
    const double rms = get_rms(plane);
    const double scale_floor = std::max(rms, static_cast<double>(k_rms_floor));
    if (std::abs(mean) > static_cast<double>(k_mean_rel_tol) * scale_floor) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            std::string{caller} +
                ": f must have zero mean per channel, but channel has mean " +
                std::to_string(mean) + " against RMS " + std::to_string(rms));
    }
}

// TODO: Should be part of vc_image?
std::vector<vc_image> get_image_planes(const vc_image& f_input) {
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
    return channels;
}

template <typename T>
concept vc_poisson_sweep_req =
    std::same_as<T, vc_image> || std::same_as<T, vc_image_writer>;

// One sweep of phi(x,y) = [ four neighbours - f(x,y) ] / 4, under wrap.
//
// src AND dst MAY BE THE SAME WRITER, and that aliasing is the ENTIRE
// difference between the two methods -- which is why there is one loop body
// here and not two:
//
//     jacobi         distinct buffers  -> every read is the previous sweep's
//     gauss_seidel   the same buffer   -> left and up are already this sweep's
//
// f_plane is a SINGLE-CHANNEL plane, so its channel index is 0. An earlier
// version passed the outer loop's `ch` here, which throws for ch > 0 because
// the plane has one channel -- invisible at one channel, which is the bug class
// conv_theorem shipped.
// src is taken by NON-const reference although nothing is written through it:
// vc_image_writer::at() has no const overload, so a const reference cannot read
// a pixel. Adding one is a library change and is logged in
// docs/follow_up_p4.txt rather than made here.
void do_poisson_sweep(const vc_image& f_plane,
                      vc_image_writer& src,
                      vc_image_writer& dst) {
    const auto w = f_plane.width();
    const auto h = f_plane.height();
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            // TODO(perf): the four `%` per pixel dominate at high sweep counts.
            // Interior pixels never wrap -- split into an interior fast path
            // plus a border pass, or precompute index tables once per call.
            // TODO(perf): at<>() bounds-checks three dimensions on every call,
            // six calls per pixel per sweep. Hoist pixels()->as<buf_f32>() and
            // index the span directly. Measure first; the suite is the net.
            const auto neighbour_sum =
                src.at<vc::buf_f32>((x + 1) % w, y, 0) +
                src.at<vc::buf_f32>((x - 1 + w) % w, y, 0) +
                src.at<vc::buf_f32>(x, (y + 1) % h, 0) +
                src.at<vc::buf_f32>(x, (y - 1 + h) % h, 0);
            dst.at<vc::buf_f32>(x, y, 0) =
                (neighbour_sum - f_plane.at<vc::buf_f32>(x, y, 0)) / 4.0F;
        }
    }
}

// The relative residual of ONE channel: how badly the current phi fails the
// equation, as an RMS, scaled by f's own RMS so the number is dimensionless.
//
// f_rms is passed in rather than recomputed because it never changes, and this
// runs once per sweep -- thousands of times.
//
// Accumulated in double for the same reason require_zero_mean is: an f32 sum
// over many samples loses precision exactly where this needs it.
double
relative_residual(const vc_image& f_plane,
                  vc_image_writer& phi, // non-const: see do_poisson_sweep
                  double f_rms) {
    const auto w = f_plane.width();
    const auto h = f_plane.height();
    double ssq = 0.0;
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            const auto neighbour_sum =
                phi.at<vc::buf_f32>((x + 1) % w, y, 0) +
                phi.at<vc::buf_f32>((x - 1 + w) % w, y, 0) +
                phi.at<vc::buf_f32>(x, (y + 1) % h, 0) +
                phi.at<vc::buf_f32>(x, (y - 1 + h) % h, 0);
            const auto lap =
                neighbour_sum - (4.0F * phi.at<vc::buf_f32>(x, y, 0));
            const auto d =
                static_cast<double>(f_plane.at<vc::buf_f32>(x, y, 0) - lap);
            ssq += d * d;
        }
    }
    const auto count = static_cast<double>(f_plane.pixel_count());
    return std::sqrt(ssq / count) /
           std::max(f_rms, static_cast<double>(k_rms_floor));
}

// Subtract this plane's own mean, in place.
//
// WHY EVERY SWEEP, FOR BOTH METHODS. The periodic problem fixes phi only up to
// an added constant -- phi and phi + c give an identical f, because the
// stencil's weights sum to zero -- so the iteration has a free parameter and
// something must pin it. The convention is the zero-mean solution, and this is
// how it is picked.
//
// Jacobi provably preserves the mean: summing its update over every pixel
// counts each pixel as a neighbour exactly four times, giving
// sum(phi_new) = sum(phi) - sum(f)/4, and sum(f) is zero by the precondition.
// Gauss-Seidel does NOT -- it writes as it goes, so that accounting mixes old
// and new values and the total drifts. Measured on f = [-10,4,2,4] from zero,
// the Gauss-Seidel mean runs 0.252, 0.347, 0.386, 0.404, ... while Jacobi's
// stays exactly 0.
//
// ** DONE FOR BOTH ANYWAY, DELIBERATELY. ** The Jacobi proof assumes sum(f) is
// EXACTLY zero, and the precondition only guarantees it to k_mean_rel_tol times
// the RMS. A residual mean compounds at sum(f)/4 per sweep, and S8b runs
// thousands. One extra pass removes the assumption.
void subtract_mean(vc_image_writer& plane, vc::element_count count) {
    double sum = 0.0;
    plane.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> pixels) {
        sum = std::accumulate(pixels.begin(), pixels.end(), 0.0,
                              [](double acc, vc::buf_f32 v) {
                                  return acc + static_cast<double>(v);
                              });
    });
    const auto mean =
        static_cast<vc::buf_f32>(sum / static_cast<double>(count));
    plane.with_pixels<vc::buf_f32>([mean](std::span<vc::buf_f32> pixels) {
        for (auto& v : pixels) {
            v -= mean;
        }
    });
}

} // namespace

// The DISCRETE scale, which is the stencil's own transform -- never the
// continuous -(u^2 + v^2). That one is this one's small-angle approximation and
// is 2.5x off at the highest frequency, exactly where edges live; the header
// has the table. Real, with no rotation, because the stencil is symmetric.
vc::float32 laplacian_scale(vc::math::grid_dim k,
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

    auto channels = get_image_planes(f_input);

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

        // NOT an equality against zero. f comes out of a float transform and
        // carries noise -- measured, sum(f) is around 2.4e-06 on an 8x4 field
        // where the exact answer is 0. A first version tested `sum != 0.0` and
        // rejected every real input.
        require_zero_mean(plane, "poisson_fft");

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

        // TODO: create a common utility function for rms? Should work for vc_image and signal?
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
                    std::to_string(imag_scale) +
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

poisson_iter_report poisson_iter(const vc::vc_image& f,
                                 poisson_sweep sweep,
                                 sweep_count max_sweeps,
                                 vc::float32 tolerance) {
    if (f.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "poisson_iter: f must be of type buf_f32");
    }
    if (max_sweeps == 0) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "poisson_iter: max_sweeps must be > 0");
    }

    if (!(tolerance >= 0.0F)) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "poisson_iter: tolerance must be >= 0");
    }

    // ---- SWEEP-DEPENDENT PRECONDITIONS ----
    //
    // A SWITCH WITH NO `default`, DELIBERATELY. The two branches further down
    // test `sweep == poisson_sweep::jacobi`, so a newly added enumerator
    // compiles silently and falls into the gauss_seidel path -- which is a
    // WORKING SOLVER, so it returns a correct answer by the wrong method and
    // every end-state check passes. That is exactly what happened when
    // red_black was added: zero warnings. This switch is what the header
    // promised and the `==` tests did not provide; S8b should convert those two
    // as well, and then this block can fold into them.
    switch (sweep) {
    case poisson_sweep::jacobi:
    case poisson_sweep::gauss_seidel:
        break;
    case poisson_sweep::red_black:
        // Under wrap, column w-1 neighbours column 0 and their parities differ
        // by w-1, so an ODD axis joins two same-coloured pixels and the
        // independence that is this method's whole purpose does not hold. The
        // answer would still be correct sequentially; the guarantee would not.
        if (f.width() % 2 != 0) {
            throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                                   "poisson_iter: red_black requires an even "
                                   "width, but width is " +
                                       std::to_string(f.width()));
        }
        if (f.height() % 2 != 0) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                "poisson_iter: red_black requires an even height, but height "
                "is " +
                    std::to_string(f.height()));
        }
        // ** S8b: REMOVE THIS THROW AND IMPLEMENT THE TWO PARITY PASSES. **
        // It is here so the unimplemented enumerator fails LOUDLY instead of
        // silently running gauss_seidel and reporting a right answer by the
        // wrong route -- 16_poisson_crossval group 3 would be the only check to
        // notice, and only once it is green enough to reach.
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "poisson_iter: poisson_sweep::red_black is not implemented yet "
            "(S8b). Scaffolded in include/vc/pixelops/vc_poisson.h; checks in "
            "examples/16_poisson_crossval.cpp");
    }

    const auto planes = get_image_planes(f);
    vc_image_writer result{f.width(), f.height(), f.channels(),
                           vc::buf_f32{0.0F}};

    std::vector<std::vector<vc::float32>> history;
    history.reserve(f.channels());
    bool all_converged = true;

    for (vc::channel_count ch = 0; ch < f.channels(); ++ch) {
        const vc_image& f_plane = planes[ch];
        require_zero_mean(f_plane, "poisson_iter");

        const double f_rms = get_rms(f_plane);
        const auto count = f_plane.pixel_count();

        vc_image_writer buf_a{f_plane.width(), f_plane.height(), 1,
                              vc::buf_f32{0.0F}};
        std::optional<vc_image_writer> buf_b;
        if (sweep == poisson_sweep::jacobi) {
            buf_b.emplace(f_plane.width(), f_plane.height(), 1,
                          vc::buf_f32{0.0F});
        }

        vc_image_writer* current = &buf_a;

        std::vector<vc::float32> channel_history;
        // Capped: max_sweeps is 32-bit, and reserving for a budget near its
        // maximum would ask for gigabytes on a call that converges in 300
        // sweeps. push_back grows past this if it ever needs to.
        channel_history.reserve(std::min<std::size_t>(max_sweeps, 4096) + 1);

        // BEFORE any sweep: phi is zero, so r = f - laplacian(0) = f and the
        // ratio is exactly 1.0 -- except for an all-zero plane, where the
        // clamped denominator gives 0.0 rather than 0/0.
        channel_history.push_back(static_cast<vc::float32>(
            relative_residual(f_plane, *current, f_rms)));

        bool converged = channel_history.back() <= tolerance;

        for (sweep_count s = 0; s < max_sweeps && !converged; ++s) {
            vc_image_writer* destination = current;
            if (sweep == poisson_sweep::jacobi) {
                destination = (current == &buf_a) ? &buf_b.value() : &buf_a;
            }
            do_poisson_sweep(f_plane, *current, *destination);
            current = destination;

            subtract_mean(*current, count);
            channel_history.push_back(static_cast<vc::float32>(
                relative_residual(f_plane, *current, f_rms)));
            converged = channel_history.back() <= tolerance;
        }

        all_converged = all_converged && converged;
        history.push_back(std::move(channel_history));

        for (vc::image_dim y = 0; y < f_plane.height(); ++y) {
            for (vc::image_dim x = 0; x < f_plane.width(); ++x) {
                result.at<vc::buf_f32>(x, y, ch) =
                    current->at<vc::buf_f32>(x, y, 0);
            }
        }
    }

    return poisson_iter_report{.phi = std::move(result).seal(),
                               .residual = std::move(history),
                               .converged = all_converged};
}

} // namespace vc::pixelops
