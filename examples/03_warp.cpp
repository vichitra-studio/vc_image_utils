// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 03 — moving pixels around: affine warp by inverse mapping.
//
// THE ALGORITHM AND ITS RATIONALE LIVE IN include/vc/pixelops/vc_warp.h — why
// inverse mapping rather than forward, the area pixel-centre convention, why
// the edge policy is applied per neighbour, and why downscaling aliases. That
// header is written to be read; start there.
//
// What is left here is what an example is actually for: the phase plan's
// acceptance criteria turned into assertions, hand-computed sampler values to
// check them against, the rotate-back measurement, and the pictures.
//
// Examples 01 and 02 changed pixel VALUES; this one changes their POSITIONS.
// That range/domain distinction is what vc_warp.h opens with.
//
// Outputs, all under examples_out/:
//
//   03_card.png                       the test card, before
//   03_card_rot30_clipped.png         rotated on a same-size canvas — corners lost
//   03_card_rot30_fitted.png          rotated on a fitted canvas — nothing lost
//   03_card_downscale_quarter.png     the 1px checkerboard collapsing to grey
//   03_rotated_30.png / _fitted.png   the same two on a photograph
//   03_roundtrip.png                  +30 then -30
//   03_roundtrip_diff_x20.png         the residual, amplified 20x to be visible

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/io/vc_io_stb.h"
#include "vc/math/vc_linalg.h"
#include "vc/math/vc_transform.h"
#include "vc/pixelops/vc_warp.h"

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

bool approx_equal(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

// A ramp with every element distinct: value(x, y, ch) = 10x + y + 100ch.
// Deliberately non-square with odd dimensions and per-channel offsets, so a
// transposed index, an x/y swap or a channel mix-up lands on a wrong value
// rather than a plausible one.
vc::vc_image make_ramp(vc::image_dim w, vc::image_dim h, vc::channel_count c) {
    vc::vc_image_writer out{w, h, c, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            for (vc::channel_count ch = 0; ch < c; ++ch) {
                out.at<vc::buf_f32>(x, y, ch) =
                    static_cast<float>((10 * x) + y + (100 * ch));
            }
        }
    }
    return std::move(out).seal();
}

// A synthetic test card: 400x300, non-square so an x/y swap is obvious, with
// four features chosen to make a geometric transform LEGIBLE rather than
// merely correct.
//
//   white border      the outline, so a rotation's angle and any clipping are
//                     immediately visible
//   red square        an asymmetric marker near one corner, so a flip or a
//                     90-degree error cannot masquerade as a small rotation
//   grey gradient     left-to-right, so orientation survives even where the
//                     border is cropped away
//   1px checkerboard  the finest pattern the grid can hold — period 2, exactly
//                     at the Nyquist limit, so ANY downscale must alias it
vc::vc_image make_test_card() {
    constexpr vc::image_dim card_w = 400;
    constexpr vc::image_dim card_h = 300;
    vc::vc_image_writer out{card_w, card_h, 3, vc::buf_f32{0.0F}};

    for (vc::image_dim y = 0; y < card_h; ++y) {
        for (vc::image_dim x = 0; x < card_w; ++x) {
            float r = 0.0F;
            float g = 0.0F;
            float b = 0.0F;

            const bool border =
                x < 4 || y < 4 || x >= card_w - 4 || y >= card_h - 4;
            const bool marker = x >= 24 && x < 74 && y >= 24 && y < 74;
            const bool checker_area =
                x >= 220 && x < 376 && y >= 180 && y < 276;

            if (border) {
                r = g = b = 1.0F;
            } else if (marker) {
                r = 0.9F;
                g = 0.15F;
                b = 0.15F;
            } else if (checker_area) {
                const float v = ((x + y) % 2 == 0) ? 1.0F : 0.0F;
                r = g = b = v;
            } else {
                // Left-to-right ramp, dark to light.
                const float v = 0.15F + (0.5F * static_cast<float>(x) /
                                         static_cast<float>(card_w));
                r = g = b = v;
            }
            out.at<vc::buf_f32>(x, y, 0) = r;
            out.at<vc::buf_f32>(x, y, 1) = g;
            out.at<vc::buf_f32>(x, y, 2) = b;
        }
    }
    return std::move(out).seal();
}

// A 2x2 single-channel image, row-major, for the hand-computed sampler case.
vc::vc_image make_quad(float a, float b, float c, float d) {
    vc::vc_image_writer out{2, 2, 1, vc::buf_f32{0.0F}};
    out.at<vc::buf_f32>(0, 0, 0) = a;
    out.at<vc::buf_f32>(1, 0, 0) = b;
    out.at<vc::buf_f32>(0, 1, 0) = c;
    out.at<vc::buf_f32>(1, 1, 0) = d;
    return std::move(out).seal();
}

// Mean absolute error over a CENTRED SQUARE of side min(w, h) / 2.
//
// Why that region and not the whole image: rotating about the centre throws
// the corners out of frame, and rotating back refills them from the edge
// policy rather than from the original content. That loss is CLIPPING, not
// resampling, and it is unrecoverable by construction — including it would
// measure the canvas policy instead of the interpolation.
//
// The bound is honest for any aspect ratio. A pixel survives both passes if it
// stays within the inscribed circle, radius min(w, h) / 2. The furthest corner
// of this region is sqrt(2) * min(w, h) / 4 ~= 0.354 * min(w, h) from the
// centre, comfortably inside it. Basing the half-extent on min(w, h) rather
// than on w and h separately is what makes that true for a wide image too.
// Mean AND peak, because a mean alone hides the tail. Resampling error is not
// spread evenly: flat regions come back essentially exact while fine detail
// takes the whole loss, so the mean can look reassuring while individual
// textured pixels are several levels out. Both numbers, or neither.
struct error_stats {
    float mean;
    float peak;
};

error_stats interior_error(const vc::vc_image& a, const vc::vc_image& b) {
    if (a.width() != b.width() || a.height() != b.height() ||
        a.channels() != b.channels()) {
        throw std::invalid_argument(
            "interior_error: images must have the same geometry");
    }

    const vc::image_dim half = std::min(a.width(), a.height()) / 4;
    const vc::image_dim cx = a.width() / 2;
    const vc::image_dim cy = a.height() / 2;
    // No unsigned underflow: half <= min(w,h)/4 <= w/2 == cx, and likewise for
    // cy — worth stating because image_dim is unsigned and cx - half would
    // wrap catastrophically rather than going negative.
    const vc::image_dim x_begin = cx - half;
    const vc::image_dim y_begin = cy - half;

    double total = 0.0;
    float peak = 0.0F;
    std::size_t count = 0;
    for (vc::image_dim y = y_begin; y < cy + half; ++y) {
        for (vc::image_dim x = x_begin; x < cx + half; ++x) {
            for (vc::channel_count ch = 0; ch < a.channels(); ++ch) {
                const float d = std::fabs(a.at<vc::buf_f32>(x, y, ch) -
                                          b.at<vc::buf_f32>(x, y, ch));
                total += static_cast<double>(d);
                peak = std::max(peak, d);
                ++count;
            }
        }
    }
    if (count == 0) {
        throw std::invalid_argument(
            "interior_error: the interior region is empty — the image is "
            "too small for this measurement");
    }
    return error_stats{
        .mean = static_cast<float>(total / static_cast<double>(count)),
        .peak = peak};
}

// |a - b|, amplified so it is visible. A difference under the 2/255 tolerance
// is essentially black on screen, so a raw dump would show nothing and prove
// nothing. The gain is stated in the filename.
vc::vc_image
absolute_difference(const vc::vc_image& a, const vc::vc_image& b, float gain) {
    vc::vc_image_writer out{a.width(), a.height(), a.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < a.height(); ++y) {
        for (vc::image_dim x = 0; x < a.width(); ++x) {
            for (vc::channel_count ch = 0; ch < a.channels(); ++ch) {
                const float d = std::fabs(a.at<vc::buf_f32>(x, y, ch) -
                                          b.at<vc::buf_f32>(x, y, ch));
                out.at<vc::buf_f32>(x, y, ch) =
                    std::clamp(d * gain, 0.0F, 1.0F);
            }
        }
    }
    return std::move(out).seal();
}

} // namespace

int main() {
    bool ok = true;

    // The example is RED until the TODO(you) bodies here and in
    // src/math/vc_transform.cpp are written. Caught rather than left to
    // terminate so the failure reports as a readable ctest failure instead of
    // an abort. Delete this wrapper once everything is implemented if you
    // prefer an uncaught throw to be loud.
    try {
        const float pi = std::acos(-1.0F);
        const float deg30 = pi / 6.0F;

        // ---- 1. the sampler, against hand-computed values ----
        //
        //   0   10          at (0.5, 0.5):  top = lerp(0, 10, 0.5)  =  5
        //   20  30                          bot = lerp(20, 30, 0.5) = 25
        //                                   out = lerp(5, 25, 0.5)  = 15
        //
        //   at (0.3, 0.8):  top = 0.7*0  + 0.3*10 =  3
        //                   bot = 0.7*20 + 0.3*30 = 23
        //                   out = 0.2*3  + 0.8*23 = 19
        const vc::vc_image quad = make_quad(0.0F, 10.0F, 20.0F, 30.0F);

        // Exact integer coordinates must return the stored pixel untouched —
        // this is why an identity transform can be pixel-exact at all.
        ok = check(approx_equal(vc::pixelops::sample_bilinear(
                                    quad, 0.0F, 0.0F, 0,
                                    vc::pixelops::vc_edge_policy::clamp),
                                0.0F, 1e-6F),
                   "sampling at (0, 0) returns the stored pixel") &&
             ok;
        ok = check(approx_equal(vc::pixelops::sample_bilinear(
                                    quad, 1.0F, 1.0F, 0,
                                    vc::pixelops::vc_edge_policy::clamp),
                                30.0F, 1e-6F),
                   "sampling at (1, 1) returns the stored pixel") &&
             ok;
        ok = check(approx_equal(vc::pixelops::sample_bilinear(
                                    quad, 0.5F, 0.5F, 0,
                                    vc::pixelops::vc_edge_policy::clamp),
                                15.0F, 1e-5F),
                   "sampling at (0.5, 0.5) matches the hand value 15") &&
             ok;
        ok = check(approx_equal(vc::pixelops::sample_bilinear(
                                    quad, 0.3F, 0.8F, 0,
                                    vc::pixelops::vc_edge_policy::clamp),
                                19.0F, 1e-4F),
                   "sampling at (0.3, 0.8) matches the hand value 19") &&
             ok;

        // ---- 1b. an edge in ONE axis must not disable the other ----
        //
        // 5x3 ramp, value(x, y) = 10x + y, so src(4,1) = 41, src(4,2) = 42.
        // At (4.2, 1.5) the x-blend genuinely collapses — there is no column 5
        // to weigh in — but rows 1 and 2 both exist, so y must still
        // interpolate. The answer is 41.5, not 41.
        //
        // Pinned here because the interior-only MAE check further down CANNOT
        // see this: it deliberately excludes the border, which is precisely
        // where this class of bug lives. Without these two assertions, a
        // single upfront bounds gate that discards both blends at once passes
        // every other check in this file.
        const vc::vc_image edge_ramp = make_ramp(5, 3, 1);
        ok = check(approx_equal(vc::pixelops::sample_bilinear(
                                    edge_ramp, 4.2F, 1.5F, 0,
                                    vc::pixelops::vc_edge_policy::clamp),
                                41.5F, 1e-4F),
                   "right edge: x collapses, y still interpolates") &&
             ok;
        // The mirror case. src(1,2) = 12, src(2,2) = 22, so at (1.5, 2.4) the
        // y-blend collapses against the bottom row and x gives 17.
        ok = check(approx_equal(vc::pixelops::sample_bilinear(
                                    edge_ramp, 1.5F, 2.4F, 0,
                                    vc::pixelops::vc_edge_policy::clamp),
                                17.0F, 1e-4F),
                   "bottom edge: y collapses, x still interpolates") &&
             ok;

        // ---- 2. identity is pixel-exact ----
        const vc::vc_image ramp = make_ramp(5, 3, 3);
        const vc::vc_image identity_warped =
            vc::pixelops::warp(ramp, vc::math::vc_mat3::identity(),
                               vc::pixelops::vc_edge_policy::clamp);

        bool identity_exact = identity_warped.width() == ramp.width() &&
                              identity_warped.height() == ramp.height() &&
                              identity_warped.channels() == ramp.channels();
        {
            for (vc::image_dim y = 0; y < ramp.height(); ++y) {
                for (vc::image_dim x = 0; x < ramp.width(); ++x) {
                    for (vc::channel_count ch = 0; ch < ramp.channels(); ++ch) {
                        if (ramp.at<vc::buf_f32>(x, y, ch) !=
                            identity_warped.at<vc::buf_f32>(x, y, ch)) {
                            identity_exact = false;
                        }
                    }
                }
            }
        }
        ok = check(identity_exact,
                   "the identity transform is pixel-exact, bit for bit") &&
             ok;

        // ---- 3. translation direction — does the picture move the right way?
        //
        // With inverse mapping, dst(x, y) = src(M^-1 * (x, y)), so a forward
        // translate of +1 in x makes dst(x+1, y) == src(x, y): the content
        // moves RIGHT. Applying M instead of M^-1 moves it left, and this
        // check is the cheapest way to catch that.
        const vc::vc_image shifted =
            vc::pixelops::warp(ramp, vc::math::translate(1.0F, 0.0F),
                               vc::pixelops::vc_edge_policy::clamp);
        {
            bool moved_right = true;
            for (vc::image_dim y = 0; y < ramp.height(); ++y) {
                for (vc::image_dim x = 0; x + 1 < ramp.width(); ++x) {
                    for (vc::channel_count ch = 0; ch < ramp.channels(); ++ch) {
                        if (!approx_equal(shifted.at<vc::buf_f32>(x + 1, y, ch),
                                          ramp.at<vc::buf_f32>(x, y, ch),
                                          1e-5F)) {
                            moved_right = false;
                        }
                    }
                }
            }
            ok = check(moved_right,
                       "translate(+1, 0) moves content right — the inverse "
                       "map is applied in the correct direction") &&
                 ok;
        }

        // ---- 4. rotate +30, then -30, on a real photo ----
        const vc::io::path input =
            std::string(VC_EXAMPLES_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
        vc::io::stb_image_reader reader;
        const vc::vc_image photo = reader.read(
            input, vc::io::read_config{.dtype = vc::pixel_dtype::f32});

        const float cx = static_cast<float>(photo.width()) / 2.0F;
        const float cy = static_cast<float>(photo.height()) / 2.0F;

        const vc::vc_image rotated =
            vc::pixelops::warp(photo, vc::math::rotate_about(cx, cy, deg30),
                               vc::pixelops::vc_edge_policy::clamp);
        const vc::vc_image restored =
            vc::pixelops::warp(rotated, vc::math::rotate_about(cx, cy, -deg30),
                               vc::pixelops::vc_edge_policy::clamp);

        const error_stats err = interior_error(photo, restored);
        std::cout << "rotate +30 then -30 (interior): mean = " << err.mean
                  << " (" << (err.mean * 255.0F) << "/255)"
                  << ", peak = " << err.peak << " (" << (err.peak * 255.0F)
                  << "/255)"
                  << "  [tolerance on the mean: " << (2.0F / 255.0F) << "]\n";
        ok = check(err.mean < 2.0F / 255.0F,
                   "rotating back recovers the interior within 2/255") &&
             ok;

        // The residual is NOT a bug and NOT clipping — the rotation matrix is
        // perfectly invertible. It is resampling loss: bilinear averages
        // between grid points, and averaging cannot be undone. Two passes, two
        // roundings.
        vc::io::stb_image_writer writer;
        const vc::io::write_config png{.format = vc::io::vc_image_format::png};
        const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);
        writer.write(out_dir + "/03_rotated_30.png", rotated, png);
        writer.write(out_dir + "/03_roundtrip.png", restored, png);
        writer.write(out_dir + "/03_roundtrip_diff_x20.png",
                     absolute_difference(photo, restored, 20.0F), png);

        // ---- rotate WITHOUT cropping ----
        //
        // The same rotation on a FITTED canvas: fitted_destination() grows the
        // output to the bounding box of the rotated rectangle and shifts the
        // transform to match, so nothing is clipped. Open it beside
        // 03_rotated_30.png — that one is the same rotation on the original
        // canvas and loses its corners; this one keeps everything and pads
        // instead. vc::pixelops::vc_edge_policy::zero so the padding is visibly black rather
        // than smeared edge pixels.
        const vc::vc_image rotated_fitted =
            vc::pixelops::warp(photo, vc::math::rotate_about(cx, cy, deg30),
                               vc::pixelops::vc_edge_policy::zero,
                               vc::pixelops::vc_output_size::fit_transform);
        writer.write(out_dir + "/03_rotated_30_fitted.png", rotated_fitted,
                     png);

        // Pinned, not merely exercised. Rotating a W x H rectangle by t grows
        // its bounding box to W|cos t| + H|sin t| by W|sin t| + H|cos t|.
        // Compared within one pixel: the closed form and the corner-mapping in
        // fitted_destination() round through ceil() independently, so an exact
        // match would be a flaky test rather than a stronger one.
        {
            const auto fw = static_cast<float>(photo.width());
            const auto fh = static_cast<float>(photo.height());
            const float ac = std::fabs(std::cos(deg30));
            const float as = std::fabs(std::sin(deg30));
            const auto expect_w =
                static_cast<vc::image_dim>(std::ceil((fw * ac) + (fh * as)));
            const auto expect_h =
                static_cast<vc::image_dim>(std::ceil((fw * as) + (fh * ac)));
            const auto within_one = [](vc::image_dim a, vc::image_dim b) {
                return (a > b ? a - b : b - a) <= 1;
            };
            std::cout << "fitted rotation: " << photo.width() << "x"
                      << photo.height() << " -> " << rotated_fitted.width()
                      << "x" << rotated_fitted.height() << "  (expected "
                      << expect_w << "x" << expect_h << ")\n";
            ok = check(within_one(rotated_fitted.width(), expect_w) &&
                           within_one(rotated_fitted.height(), expect_h),
                       "the fitted canvas matches the rotated bounding box") &&
                 ok;
        }

        // ---- 5. downscale, and look at what breaks ----
        //
        // Not asserted — this one is here to be LOOKED AT. Shrinking by 4
        // means each output pixel stands for a 4x4 source region, but bilinear
        // consults only 4 pixels around a single point and never reads the
        // other twelve. Detail finer than the new grid can hold does not fade
        // out, it comes back disguised as coarser structure: moire on fabric
        // and brick, shimmer on fine lines.
        //
        // The fix is to blur away everything finer than the new sampling
        // interval BEFORE sampling, which needs convolution — P2. Aliasing
        // cannot be removed afterwards: two different scenes can produce
        // byte-identical samples, so no later filter can tell which one you
        // photographed.
        const vc::vc_image downscaled = vc::pixelops::warp(
            photo, vc::math::scale_about(cx, cy, 0.25F, 0.25F),
            vc::pixelops::vc_edge_policy::clamp);
        writer.write(out_dir + "/03_downscale_quarter_aliased.png", downscaled,
                     png);
        // ---- 6. the same operations on a legible test card ----
        //
        // The photo above is nearly radially symmetric, so a rotation of it is
        // almost impossible to SEE — the maths is checked by the numbers, but
        // the pictures prove nothing. These four outputs are for looking at.
        const vc::vc_image card = make_test_card();
        const float card_cx = static_cast<float>(card.width()) / 2.0F;
        const float card_cy = static_cast<float>(card.height()) / 2.0F;

        writer.write(out_dir + "/03_card.png", card, png);
        writer.write(out_dir + "/03_card_rot30_clipped.png",
                     vc::pixelops::warp(
                         card, vc::math::rotate_about(card_cx, card_cy, deg30),
                         vc::pixelops::vc_edge_policy::zero),
                     png);
        writer.write(out_dir + "/03_card_rot30_fitted.png",
                     vc::pixelops::warp(
                         card, vc::math::rotate_about(card_cx, card_cy, deg30),
                         vc::pixelops::vc_edge_policy::zero,
                         vc::pixelops::vc_output_size::fit_transform),
                     png);
        writer.write(out_dir + "/03_card_downscale_quarter.png",
                     vc::pixelops::warp(
                         card,
                         vc::math::scale_about(card_cx, card_cy, 0.25F, 0.25F),
                         vc::pixelops::vc_edge_policy::zero),
                     png);
        std::cout << "wrote 03_card*.png — clipped vs fitted rotation, and the "
                     "checkerboard aliasing under a 0.25x downscale\n";

        std::cout << "wrote 03_downscale_quarter_aliased.png — open it and "
                     "look at the fine texture; the false patterns are "
                     "aliasing, not blur\n";

    } catch (const std::exception& e) {
        std::cerr << "\nexample 03 failed: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
