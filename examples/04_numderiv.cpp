// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 04 -- the derivative, as a computer can actually compute it.
//
// A derivative is defined by a limit: how the ratio (f(x+h) - f(x)) / h
// behaves as h shrinks toward zero. A computer cannot shrink anything to zero
// -- it only has actual numbers -- so it evaluates that ratio at some small
// but real h and accepts the answer is slightly off. That is the whole idea,
// and the name says it: the difference is FINITE, not infinitesimal.
//
//     forward difference:   (f(x+h) - f(x)) / h
//
// ---- On a scanline you do not get to choose h ----
//
// A row of pixels has samples only at whole positions, so the smallest gap
// available is one pixel. h = 1, and the division disappears:
//
//     derivative at pixel i  ~=  f[i+1] - f[i]
//
// Which is why an image gradient is a subtraction of neighbours. Not a
// shortcut -- it is the difference quotient with the only h that exists.
//
// The framing that makes this legitimate rather than a fudge: a scanline is
// not fundamentally discrete. Brightness across the sensor varies
// continuously; the pixels are point SAMPLES of it. "The derivative of a
// scanline" means an estimate of the derivative of the underlying continuous
// function, computed from its samples -- the same reconstruction idea as
// bilinear interpolation in example 03, applied to a slope instead of a value.
//
// ---- Two errors, pulling opposite ways ----
//
// TRUNCATION error comes from stopping the limit early: the chord from x to
// x+h is not the tangent at x. It shrinks as h shrinks, in proportion to h.
//
// ROUNDOFF error comes from floating point. f(x+h) and f(x) are nearly equal,
// so most of their significant digits are identical and cancel to zero in the
// subtraction, leaving few digits carrying the answer. Then you divide by a
// tiny h, magnifying whatever survived. It GROWS as h shrinks. Push far enough
// and x + h rounds back to x, the difference is exactly zero, and the estimate
// is zero.
//
// So error against h is a V, not a line. Everything the acceptance criterion
// says -- halving h halves the error -- describes only the right-hand arm. For
// float32 the turn is somewhere near h = 3e-4.
//
// None of that troubles the scanline, where h is pinned at 1 -- about three
// thousand times larger than the floor. There, truncation is the whole story.
//
// ---- What this is NOT ----
//
// One dimension only. The 2-D gradient (df/dx and df/dy together), edge
// magnitude, and Sobel are P3. Convolution -- of which [-1, 1] is already an
// instance -- is P2. And differencing AMPLIFIES noise: two neighbouring pixels
// carry independent sensor noise, and subtracting them adds those errors while
// the true signal difference may be tiny. Expect flat regions in the scanline
// plot to look fuzzy rather than flat. That is real, it is why Sobel smooths
// across the edge, and the fix is P3's.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/io/vc_io_stb.h"

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

// ---------------------------------------------------------------------
// The two reps -- yours to write
// ---------------------------------------------------------------------

// Forward difference over a sequence of samples.
//
// WHAT h IS, since the name invites the wrong reading (mine -- `spacing` would
// have been the honest one). Samples live at real positions:
//
//     x_i  =  x0 + i * spacing
//
// so the NEXT sample is always index i + 1, whatever the spacing happens to
// be, and the gap between them in x is `spacing`. Two different measurements
// of the same move: one slot in the array, `spacing` units along x.
//
//     result[i] = (values[i + 1] - values[i]) / spacing;
//
// h is NOT a step through the array. Using it as one works by coincidence at
// spacing 1 and silently returns zeros at spacing 0.5, since casting 0.5 to an
// index gives 0 and every value is then subtracted from itself.
//
// (A stride -- skipping k samples -- is a real and occasionally useful
// parameter, for a wider, less noise-sensitive difference. But it is a SECOND
// parameter, not this one: the divisor would then be stride * spacing.)
//
// For pixels the spacing is 1, because the pixel IS the unit of position, and
// the division changes nothing. Pass the sensor's pixel pitch instead and the
// same code returns brightness per millimetre. That is all the divisor does:
// convert "per sample" into "per unit of whatever x measures".
//
// TODO(you). Two decisions this signature deliberately does not make for you:
//
//   1. THE RIGHT EDGE. The last sample has no neighbour to its right, so there
//      is nothing to subtract. Same problem warp() had, same menu: return one
//      fewer value than you were given; repeat the last value so the final
//      derivative reads 0; or switch to a backward difference for that one
//      position. All defensible. Pick one and write down which, because it is
//      visible in the output and in the length of what you return.
//
//   2. WHAT THE ANSWER IS THE DERIVATIVE *OF*. Forward difference over the
//      interval [i, i+1] estimates the slope midway along it -- at i + 0.5,
//      not at i. Storing it at index i builds in a half-pixel shift. Harmless
//      for finding WHERE edges are; not harmless if you later compare against
//      something computed the other way.
[[nodiscard]] std::vector<float>
forward_difference(std::span<const float> values, float h) {
    // EDGE POLICY, chosen: the result is the SAME LENGTH as the input, and the
    // last element is 0.
    //
    // The last sample has no right-hand neighbour, so there is no estimate to
    // be had there. Zero is a convention, not a measurement -- it is invented
    // data, and if the signal ends on a strong edge you will read a flat zero
    // exactly where it is steepest. A backward difference at that one position
    // would be a real estimate; returning n-1 values would be honest about
    // there being n-1 gaps. Both are better. This one is simplest and keeps
    // result indices aligned with input indices, which the plots rely on.
    //
    // Worth stating explicitly because the zero arrives from the vector's
    // value-initialisation rather than from a store -- without this comment a
    // reader cannot tell the convention from an accident.
    std::vector<float> result(values.size());

    // `i + 1 < size()`, NOT `i < size() - 1`. size() is unsigned, so on an
    // empty span `size() - 1` is not -1 but about 18 quintillion, and the loop
    // would run off the end of memory. Written this way the empty case needs
    // no special handling at all: the loop simply never runs.
    for (std::size_t i = 0; i + 1 < values.size(); ++i) {
        result[i] = (values[i + 1] - values[i]) / h;
    }
    return result;
}

// Forward difference of a function at a point -- the version where you DO get
// to choose h, and therefore the version that can show the V.
//
// TODO(you).
[[nodiscard]] float forward_difference_at(float (*f)(float), float x, float h) {
    return (f(x + h) - f(x)) / h;
}

// The test function and its exact derivative.
//
// TODO(you): a quadratic, and its derivative worked out by hand -- that is the
// homework for this row, and the whole point is that the reference value comes
// from your own differentiation rather than from another numerical method.
[[nodiscard]] float test_function(float x) {
    return x * x; // Example quadratic function
}

[[nodiscard]] float test_derivative_exact(float x) {
    return 2.0F * x; // Exact derivative of the quadratic function
}

// ---------------------------------------------------------------------
// Fixtures and plumbing
// ---------------------------------------------------------------------

// A step: `length` samples, 0.0 up to `edge_at`, then 1.0.
//
// The value of this signal is that its derivative is known EXACTLY -- zero
// everywhere except at one position -- so it admits an assertion with no
// tolerance at all. It also pins the off-by-one: whether the spike lands on
// the last dark sample or the first bright one follows from your scheme, and
// you should be able to say which before running it.
[[nodiscard]] std::vector<float> make_step(std::size_t length,
                                           std::size_t edge_at) {
    std::vector<float> v(length, 0.0F);
    for (std::size_t i = edge_at; i < length; ++i) {
        v[i] = 1.0F;
    }
    return v;
}

// One row of one channel, as a plain sequence.
[[nodiscard]] std::vector<float>
extract_row(const vc::vc_image& img, vc::image_dim y, vc::channel_count ch) {
    std::vector<float> row(img.width());
    for (vc::image_dim x = 0; x < img.width(); ++x) {
        row[x] = img.at<vc::buf_f32>(x, y, ch);
    }
    return row;
}

// Render a 1-D signal as an image, with ZERO ON THE CENTRE LINE.
//
// A derivative is signed: a dark-to-bright edge gives a positive spike, a
// bright-to-dark edge a negative one. Plotting the magnitude would make both
// read as peaks and throw away which direction each edge runs, so zero sits
// halfway up and bars extend either way from it. Positive is drawn white,
// negative orange, so the sign survives even in a thumbnail.
//
// Scaled by the largest magnitude present, so the tallest bar just fills the
// half-height. That means two plots are NOT comparable to each other unless
// you pass them through the same scale -- the same trap example 02 had with
// its two SSD maps.
//
// `columns_per_sample` widens short signals so they are readable -- a
// 64-sample plot one column per sample is 64 pixels wide and useless. It only
// ever EXPANDS: compressing a long signal to fit a fixed width would drop
// samples, and dropping samples from a plot whose whole purpose is to show
// isolated spikes would hide exactly what you are looking for. (It would also
// be aliasing, in a file about derivatives -- see example 03.)
[[nodiscard]] vc::vc_image plot_signed(std::span<const float> values,
                                       vc::image_dim height,
                                       vc::image_dim columns_per_sample = 1) {
    const auto width =
        static_cast<vc::image_dim>(values.size()) * columns_per_sample;
    vc::vc_image_writer out{width, height, 3, vc::buf_f32{0.08F}};

    const vc::image_dim centre = height / 2;

    float scale = 0.0F;
    for (const float v : values) {
        scale = std::max(scale, std::fabs(v));
    }
    // An all-zero signal has no scale to speak of; drawing just the axis is
    // the honest rendering rather than dividing by zero.
    const float half = static_cast<float>(centre) - 1.0F;
    const float to_pixels = scale > 0.0F ? half / scale : 0.0F;

    // The zero line first, so bars draw over it.
    for (vc::image_dim x = 0; x < width; ++x) {
        for (vc::channel_count ch = 0; ch < 3; ++ch) {
            out.at<vc::buf_f32>(x, centre, ch) = 0.35F;
        }
    }

    for (vc::image_dim x = 0; x < width; ++x) {
        const float v = values[x / columns_per_sample];
        const auto extent =
            static_cast<vc::image_dim>(std::fabs(v) * to_pixels);
        const bool positive = v >= 0.0F;

        // From d = 1, not 0. Starting at 0 would paint the centre pixel in
        // every column -- including columns whose value is zero -- burying the
        // axis under the bars and making "no signal" look identical to "signal
        // too small to see". Leaving d = 0 alone lets the axis show through
        // wherever the derivative is flat, which is most of a real scanline.
        // extent cannot exceed centre - 1: the largest |v| is `scale`, and
        // scale * to_pixels is exactly `half` = centre - 1. So centre - d is at
        // least 1 and centre + d at most height - 1, and no bounds check is
        // needed here -- stating the bound beats a guard that implies a hazard
        // which cannot occur.
        for (vc::image_dim d = 1; d <= extent; ++d) {
            // Positive bars go UP, which in image coordinates means towards
            // smaller y.
            const vc::image_dim y = positive ? centre - d : centre + d;
            out.at<vc::buf_f32>(x, y, 0) = 1.0F;
            out.at<vc::buf_f32>(x, y, 1) = positive ? 1.0F : 0.45F;
            out.at<vc::buf_f32>(x, y, 2) = positive ? 1.0F : 0.30F;
        }
    }
    return std::move(out).seal();
}

} // namespace

int main() {
    bool ok = true;

    const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);
    vc::io::stb_image_writer writer;
    const vc::io::write_config png{.format = vc::io::vc_image_format::png};

    // ---- 1. the step: an exactly-known derivative ----
    const std::vector<float> step = make_step(64, 32);
    const std::vector<float> step_d = forward_difference(step, 1.0F);

    std::cout << "step signal, forward difference:\n";
    for (std::size_t i = 30; i < 35 && i < step_d.size(); ++i) {
        std::cout << "  index " << i << ": " << step_d[i] << '\n';
    }
    // 8 columns per sample: 64 samples would otherwise give a 64-pixel-wide
    // image nobody can read.
    writer.write(out_dir + "/04_step_derivative.png",
                 plot_signed(step_d, 200, 8), png);

    // The spike lands at index 31, NOT 32.
    //
    // make_step(64, 32) is dark at 0..31 and bright at 32..63, so the change
    // happens BETWEEN index 31 and index 32. A forward difference measures the
    // interval [i, i+1] and stores the answer at its LEFT end, so the change
    // between 31 and 32 is reported at 31. A backward difference would report
    // the same change at 32. Neither is wrong; the scheme decides.
    //
    // Exact comparisons, no tolerance: 1 - 0 is exactly 1 and 0 - 0 is exactly
    // 0 in any floating-point format.
    ok = check(step_d[31] == 1.0F,
               "the step's derivative is exactly 1 at index 31 (the LEFT end "
               "of the interval that changed)") &&
         ok;

    // Every other index exactly 0 -- stronger than checking two neighbours,
    // because it also catches a spurious second spike anywhere in the signal.
    bool rest_flat = true;
    for (std::size_t i = 0; i < step_d.size(); ++i) {
        if (i != 31 && step_d[i] != 0.0F) {
            rest_flat = false;
            std::cerr << "  unexpected non-zero at index " << i << ": "
                      << step_d[i] << '\n';
        }
    }
    ok = check(rest_flat,
               "the step's derivative is exactly 0 everywhere else") &&
         ok;

    // The discrete fundamental theorem of calculus: summing the differences
    // telescopes, because every interior value is added once and subtracted
    // once. Everything cancels except the two ends.
    //
    // Checked on the STEP rather than on the photo further down, and that
    // choice is the point. On this photograph the row begins and ends in the
    // dark surround, so `last - first` is 0 and the property degenerates into
    // `sum == 0` -- which a sign-flipped implementation would also satisfy.
    // Here the expected value is 1, and since only one term is non-zero the
    // comparison can be exact rather than tolerant.
    double telescoped = 0.0;
    for (const float d : step_d) {
        telescoped += static_cast<double>(d);
    }
    ok = check(telescoped == 1.0,
               "the differences sum back to (last - first) -- the discrete "
               "fundamental theorem of calculus") &&
         ok;

    // A NON-UNIT spacing, which is the case nothing else here exercises.
    // f(x) = x^2 sampled at x = 0, 0.5, 1.0, 1.5 gives these four values, and
    // the forward difference at index 0 is (0.25 - 0) / 0.5 = 0.5.
    //
    // Both other callers pass spacing 1, so a divisor/index mix-up survives
    // every other check in this file. This is the one that sees it.
    const std::vector<float> quad_samples{0.0F, 0.25F, 1.0F, 2.25F};
    const std::vector<float> quad_d = forward_difference(quad_samples, 0.5F);
    std::cout << "spacing 0.5: first derivative = " << quad_d[0]
              << "   (expected 0.5)\n";
    ok = check(quad_d[0] == 0.5F, "spacing is a divisor, not an index step") &&
         ok;

    // ---- 2. the h sweep: does the error really shrink like h? ----
    constexpr float at_x = 3.0F;
    std::cout << "\nh sweep at x = " << at_x
              << "   (exact derivative = " << test_derivative_exact(at_x)
              << ")\n";
    std::cout << "        h        estimate          error     ratio\n";

    struct sweep_point {
        float h;
        float error;
    };
    std::vector<sweep_point> sweep;

    float previous_error = 0.0F;
    for (float h = 1.0F; h > 1e-8F; h /= 2.0F) {
        const float estimate = forward_difference_at(test_function, at_x, h);
        const float error = std::fabs(estimate - test_derivative_exact(at_x));
        const float ratio =
            previous_error > 0.0F ? error / previous_error : 0.0F;
        std::cout << "  " << h << "   " << estimate << "   " << error << "   "
                  << ratio << '\n';
        previous_error = error;
        sweep.push_back(sweep_point{.h = h, .error = error});
    }

    // ---- the RATE, on the truncation-dominated arm ----
    //
    // "Within O(h)" is a statement about a rate, so the thing to check is the
    // RATIO between consecutive errors, not any single error value. A fixed
    // bound like error < 0.01 would pass for an implementation whose error did
    // not shrink with h at all.
    //
    // For a quadratic the truncation error is exactly proportional to h, so
    // the ratio is exactly 0.5 wherever truncation dominates -- which is why
    // the band below can be tight rather than generous.
    //
    // WHERE THE ARM ENDS.
    //
    //     total error  ~=  h  +  (2 * precision * |f(x)|) / h
    //
    // The 2 is there because BOTH f(x+h) and f(x) are rounded before the
    // subtraction, so the numerator can carry two roundings' worth, not one.
    // With f(3) = 9 and float32 precision ~1.2e-7, one rounding is about
    // 1.1e-6 and the pair about 2.2e-6.
    //
    // The two terms are equal when h * h = 2.2e-6, so near h = 1.5e-3. Below
    // that the roundoff term takes over and the ratios stop meaning anything.
    // 1e-2 leaves an order of magnitude of margin above that crossover.
    constexpr float arm_min_h = 1e-2F;
    bool rate_holds = true;
    int rate_points = 0;
    for (std::size_t i = 1; i < sweep.size(); ++i) {
        if (sweep[i].h < arm_min_h) {
            break;
        }
        const float ratio = sweep[i].error / sweep[i - 1].error;
        if (ratio < 0.45F || ratio > 0.55F) {
            rate_holds = false;
            std::cerr << "  ratio out of band at h = " << sweep[i].h << ": "
                      << ratio << '\n';
        }
        ++rate_points;
    }
    ok = check(rate_points >= 5,
               "enough points above the roundoff crossover to test a rate") &&
         ok;
    ok = check(rate_holds,
               "halving h halves the error -- the truncation error really is "
               "O(h)") &&
         ok;

    // ---- the FLOOR: shrinking h eventually makes it WORSE ----
    //
    // The other half of the story, and a property rather than a number: the
    // smallest h in the sweep must give a worse answer than the best h did. If
    // error fell monotonically all the way down, floating point would not be
    // behaving as described and one of the two explanations is wrong.
    float best_error = sweep.front().error;
    for (const sweep_point& s : sweep) {
        best_error = std::min(best_error, s.error);
    }
    std::cout << "  best error " << best_error << " over the sweep; at the "
              << "smallest h it is " << sweep.back().error << '\n';
    ok = check(sweep.back().error > best_error * 2.0F,
               "past the floor, a smaller h makes the answer worse -- the "
               "error curve is a V, not a line") &&
         ok;

    // TODO(you): assert the CONVERGENCE RATE, not an error value. "Within
    // O(h)" means each halving of h roughly halves the error, so the thing to
    // check is the RATIO between consecutive errors, ~0.5.
    //
    // Check it only on the right-hand arm -- above h ~ 1e-3. Below that the
    // ratios stop behaving, and that is the roundoff floor rather than a bug
    // in your code. Deciding where the arm ends, and saying why, is part of
    // the exercise.

    // ---- 3. a real scanline ----
    const vc::io::path input =
        std::string(VC_EXAMPLES_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    vc::io::stb_image_reader reader;
    const vc::vc_image photo =
        reader.read(input, vc::io::read_config{.dtype = vc::pixel_dtype::f32});

    const vc::image_dim row_y = photo.height() / 2;
    const std::vector<float> row = extract_row(photo, row_y, 0);
    const std::vector<float> row_d = forward_difference(row, 1.0F);

    writer.write(out_dir + "/04_scanline.png", plot_signed(row, 200), png);
    writer.write(out_dir + "/04_scanline_derivative.png",
                 plot_signed(row_d, 200), png);
    std::cout << "\nwrote 04_scanline*.png -- row " << row_y
              << " and its derivative. The derivative should sit near zero "
                 "across flat areas and spike at edges, positive where the row "
                 "brightens and negative where it darkens.\n";

    // ---- a property, since there is no ground truth here ----
    //
    // A photograph has no analytic derivative to compare against, so this is a
    // RELATIONSHIP that must hold for any correct implementation on any image
    // -- the same shape as rotate-then-unrotate in example 03. It does not
    // need to know what the right answer is.

    // 1. The discrete fundamental theorem of calculus. Summing the
    //    differences telescopes: every interior value is added once and
    //    subtracted once, so everything cancels except the two ends. Exactly
    //    true in real arithmetic for any input whatsoever -- so if it fails,
    //    the arithmetic is wrong, not the image.
    //
    //    Summed in double for the reason example 03 uses one: a thousand
    //    small floats into a float accumulator loses a little on every
    //    addition. The tolerance covers what the float SUBTRACTIONS already
    //    rounded away before the sum ever saw them.
    {
        const std::size_t gaps = row.size() - 1;
        double total = 0.0;
        for (std::size_t i = 0; i < gaps; ++i) {
            total += static_cast<double>(row_d[i]);
        }
        const double expected =
            static_cast<double>(row.back()) - static_cast<double>(row.front());
        std::cout << "  telescoping sum = " << total
                  << "   (last - first = " << expected << ")\n";
        ok = check(std::fabs(total - expected) < 1e-4,
                   "the differences sum back to (last - first) -- the discrete "
                   "fundamental theorem of calculus") &&
             ok;
    }

    // 2. A derivative measures CHANGE, so a constant offset is invisible to
    //    it. Brighten every sample by the same amount and every difference
    //    must be unchanged. This would fail for anything that mixed the
    //    values' absolute level into the answer.
    //
    //    Compared with a tolerance rather than exactly: (v+c) - (w+c) is not
    //    bit-identical to v - w in floating point, since adding c first can
    //    push both operands onto a coarser part of the number line.
    {
        std::vector<float> brighter = row;
        for (float& v : brighter) {
            v += 0.25F;
        }
        const std::vector<float> brighter_d =
            forward_difference(brighter, 1.0F);

        bool unchanged = brighter_d.size() == row_d.size();
        for (std::size_t i = 0; unchanged && i < row_d.size(); ++i) {
            if (std::fabs(brighter_d[i] - row_d[i]) > 1e-5F) {
                unchanged = false;
            }
        }
        ok = check(unchanged,
                   "adding a constant to every sample leaves the derivative "
                   "unchanged -- it measures change, not level") &&
             ok;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
