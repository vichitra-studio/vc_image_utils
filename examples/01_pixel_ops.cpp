// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 01 — per-pixel operations.
//
// Every operation here looks at ONE pixel and produces ONE pixel. None of them
// consults a neighbour. That is what makes them describable in colour space
// (R^3) rather than in image space: the same tiny map is applied independently
// at every location, so the interesting object is 3 numbers wide, not
// width*height*3.
//
// Two of them are literally matrices acting on a colour vector:
//
//     grayscale       1x3    [ 0.299  0.587  0.114 ]     R^3 -> R^1
//     per-channel     3x3    diag(sr, sg, sb)            R^3 -> R^3
//
// Grayscale is not square, so it is not invertible — a whole plane of colours
// maps to the same grey, and no "un-grayscale" exists. The diagonal one is
// invertible whenever no gain is zero, which is why white balance can be
// undone and desaturation cannot.
//
// UNLIKE example 00 this reads f32, not u8, and that is a deliberate choice
// rather than a preference: brightness has to be EXACTLY reversible, and in
// u8 the rounding and clamping destroy that on the first operation.
//
// Nothing here clamps to [0, 1]. Clamping is not a linear operation, so a
// pipeline that clamps between steps cannot be reasoned about as a composition
// of linear maps — and values above 1.0 are exactly what HDR work needs to
// survive. Display-range clamping belongs at the very end, in the encoder.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

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

// Rec.601 luma weights. They sum to 1.0, so a neutral grey maps to itself.
constexpr float kLumaR = 0.299f;
constexpr float kLumaG = 0.587f;
constexpr float kLumaB = 0.114f;

// R^3 -> R^1. The output image has ONE channel, which is the shape of the map
// showing up in the geometry: 3 numbers in, 1 out.
vc::vc_image to_grayscale(const vc::vc_image& src) {
    const auto in = src.pixels()->as<vc::buf_f32>();
    vc::vc_image_writer out{src.width(), src.height(), 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            out.at<vc::buf_f32>(x, y, 0) =
                kLumaR * in[src.meta().index(x, y, 0)] +
                kLumaG * in[src.meta().index(x, y, 1)] +
                kLumaB * in[src.meta().index(x, y, 2)];
        }
    }
    return std::move(out).seal();
}

// R^3 -> R^3, diagonal. This is what a white-balance gain is.
vc::vc_image
scale_channels(const vc::vc_image& src, float sr, float sg, float sb) {
    // Enforced rather than documented: `gain` has three entries, so a
    // 1-channel image (to_grayscale's output, say) would index past its end.
    if (src.channels() != 3) {
        throw std::invalid_argument(
            "scale_channels: expects a 3-channel image");
    }
    const std::array<float, 3> gain{sr, sg, sb};
    const auto in = src.pixels()->as<vc::buf_f32>();
    vc::vc_image_writer out{src.width(), src.height(), src.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            for (vc::channel_count ch = 0; ch < src.channels(); ++ch) {
                out.at<vc::buf_f32>(x, y, ch) =
                    gain[ch] * in[src.meta().index(x, y, ch)];
            }
        }
    }
    return std::move(out).seal();
}

// A uniform gain — scale_channels with all three equal. Kept separate because
// it is the operation whose exact reversibility is being demonstrated.
vc::vc_image brighten(const vc::vc_image& src, float k) {
    return scale_channels(src, k, k, k);
}

// v -> 1 - v. NOT linear: it fails Phi(0) = 0, since black inverts to white.
// It is AFFINE — a linear part (negation) plus a constant. Same shape as the
// translation that forces `warp` into a 3x3 homogeneous matrix.
vc::vc_image invert(const vc::vc_image& src) {
    const auto in = src.pixels()->as<vc::buf_f32>();
    vc::vc_image_writer out{src.width(), src.height(), src.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            for (vc::channel_count ch = 0; ch < src.channels(); ++ch) {
                out.at<vc::buf_f32>(x, y, ch) =
                    1.0F - in[src.meta().index(x, y, ch)];
            }
        }
    }
    return std::move(out).seal();
}

// Element-wise sum of two images of identical geometry — vector addition in
// R^(W*H*C), and the operation the linearity check below needs.
vc::vc_image add(const vc::vc_image& a, const vc::vc_image& b) {
    // Without this, a geometry mismatch reads past the end of the shorter
    // buffer — silently, since the loop is bounded by the destination.
    //
    // Compared field by field rather than as `a.meta() != b.meta()`: the
    // descriptor's defaulted operator== also compares the composed metadata
    // handle, so two images of identical geometry carrying different EXIF
    // would be rejected. Only the geometry matters here.
    if (a.width() != b.width() || a.height() != b.height() ||
        a.channels() != b.channels()) {
        throw std::invalid_argument("add: images must have identical geometry");
    }
    const auto pa = a.pixels()->as<vc::buf_f32>();
    const auto pb = b.pixels()->as<vc::buf_f32>();
    vc::vc_image_writer out{a.width(), a.height(), a.channels(),
                            vc::buf_f32{0.0F}};
    out.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> dst) {
        for (std::size_t i = 0; i < dst.size(); ++i) {
            dst[i] = pa[i] + pb[i];
        }
    });
    return std::move(out).seal();
}

// Keep one channel, zero the rest — so the dump renders in that channel's
// colour instead of as an anonymous greyscale plane, which makes an R/B swap
// obvious at a glance.
vc::vc_image channel_only(const vc::vc_image& src, vc::channel_count keep) {
    const auto in = src.pixels()->as<vc::buf_f32>();
    vc::vc_image_writer out{src.width(), src.height(), src.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            out.at<vc::buf_f32>(x, y, keep) = in[src.meta().index(x, y, keep)];
        }
    }
    return std::move(out).seal();
}

// A 1x3 image holding three hand-checkable colours.
vc::vc_image make_probe() {
    vc::vc_image_writer w{3, 1, 3, vc::buf_f32{0.0F}};
    w.at<vc::buf_f32>(0, 0, 0) = 1.0F; // pure red
    w.at<vc::buf_f32>(1, 0, 1) = 1.0F; // pure green
    w.at<vc::buf_f32>(2, 0, 0) = 0.5F; // a mixed colour
    w.at<vc::buf_f32>(2, 0, 1) = 0.25F;
    w.at<vc::buf_f32>(2, 0, 2) = 0.75F;
    return std::move(w).seal();
}

bool approx_equal(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

} // namespace

int main() {
    const vc::io::path input =
        std::string(VC_EXAMPLES_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);

    vc::io::stb_image_reader reader;
    vc::io::stb_image_writer writer;
    const vc::io::write_config png{.format = vc::io::vc_image_format::png};

    const vc::vc_image img =
        reader.read(input, vc::io::read_config{.dtype = vc::pixel_dtype::f32});
    std::cout << "loaded " << img.width() << " x " << img.height() << " x "
              << img.channels() << " (f32)\n";

    bool ok = true;

    // scale_channels() and the probe below both assume RGB.
    if (!check(img.channels() == 3, "fixture is 3-channel")) {
        return EXIT_FAILURE;
    }

    // ---- grayscale, checked against hand-computed values ----
    //
    // Worked by hand first, then asserted — the point of the exercise. The
    // tolerance is 1/255 because that is the finest distinction an 8-bit dump
    // can express; the float maths is far more accurate than that.
    const vc::vc_image probe = make_probe();
    const vc::vc_image probe_grey = to_grayscale(probe);
    const auto grey = probe_grey.pixels()->as<vc::buf_f32>();
    constexpr float kLsb = 1.0F / 255.0F;

    // (1, 0, 0)          -> 0.299
    // (0, 1, 0)          -> 0.587
    // (0.5, 0.25, 0.75)  -> 0.299*0.5 + 0.587*0.25 + 0.114*0.75
    //                     = 0.1495 + 0.14675 + 0.0855 = 0.38175
    std::cout << "grayscale probe: " << grey[0] << ", " << grey[1] << ", "
              << grey[2] << "  (expected 0.299, 0.587, 0.38175)\n";
    ok = check(approx_equal(grey[0], 0.299F, kLsb),
               "grayscale(1,0,0) == 0.299") &&
         ok;
    ok = check(approx_equal(grey[1], 0.587F, kLsb),
               "grayscale(0,1,0) == 0.587") &&
         ok;
    ok = check(approx_equal(grey[2], 0.38175F, kLsb),
               "grayscale(0.5,0.25,0.75) == 0.38175") &&
         ok;
    ok = check(probe_grey.channels() == 1, "grayscale output has 1 channel") &&
         ok;

    // A neutral grey must survive unchanged, since the weights sum to 1.
    ok = check(approx_equal(kLumaR + kLumaG + kLumaB, 1.0F, 1e-6F),
               "luma weights sum to 1") &&
         ok;

    // ---- brightness is EXACTLY reversible in float ----
    //
    // x2 then x0.5 is exact in binary floating point: both are powers of two,
    // so only the exponent moves and the mantissa is untouched. An arbitrary
    // gain (say 1.7 then 1/1.7) would round twice and come back merely close.
    // In u8 even this pair is destroyed, because every step rounds to a whole
    // number and anything above 255 is clamped away.
    const vc::vc_image round_trip = brighten(brighten(img, 2.0F), 0.5F);
    const auto original_px = img.pixels()->as<vc::buf_f32>();
    const auto round_trip_px = round_trip.pixels()->as<vc::buf_f32>();
    bool exact = true;
    for (std::size_t i = 0; i < original_px.size(); ++i) {
        if (original_px[i] != round_trip_px[i]) {
            exact = false;
            break;
        }
    }
    ok = check(exact, "brighten(x2) then brighten(x0.5) is bit-exact in f32") &&
         ok;

    // ---- invert is its own inverse ----
    //
    // Checked with a tolerance rather than bit-exactly: 1 - v is exact only
    // for v in [0.5, 2] (Sterbenz), so a very small value can lose its low
    // bits on the way out and back.
    const vc::vc_image inverted = invert(img);
    const vc::vc_image twice = invert(inverted);
    const auto twice_px = twice.pixels()->as<vc::buf_f32>();
    bool involution = true;
    for (std::size_t i = 0; i < original_px.size(); ++i) {
        if (!approx_equal(original_px[i], twice_px[i], 1e-6F)) {
            involution = false;
            break;
        }
    }
    ok = check(involution, "invert(invert(x)) == x") && ok;

    // ---- linearity, as a runnable assertion ----
    //
    // grayscale is linear, so combining two colours and then converting must
    // equal converting each and then combining. brighten-by-adding a constant
    // would fail this same test — which is the difference between linear and
    // affine, made executable.
    const vc::vc_image a = scale_channels(probe, 0.25F, 0.40F, 0.10F);
    const vc::vc_image b = scale_channels(probe, 0.60F, 0.05F, 0.30F);
    const vc::vc_image a_plus_b = add(a, b);

    const vc::vc_image grey_of_sum = to_grayscale(a_plus_b);
    const vc::vc_image grey_a_img = to_grayscale(a);
    const vc::vc_image grey_b_img = to_grayscale(b);
    const vc::vc_image sum_of_greys = add(grey_a_img, grey_b_img);

    const auto lhs = grey_of_sum.pixels()->as<vc::buf_f32>();
    const auto rhs = sum_of_greys.pixels()->as<vc::buf_f32>();
    bool linear = true;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!approx_equal(lhs[i], rhs[i], 1e-6F)) {
            linear = false;
            break;
        }
    }
    ok = check(linear, "grayscale(a + b) == grayscale(a) + grayscale(b)") && ok;

    // The counter-example, asserted rather than described: adding a constant
    // is AFFINE, and affine maps fail this exact test. Run through the same
    // shape, offset(a + b) differs from offset(a) + offset(b) by one extra
    // copy of the constant — so it must NOT be linear.
    const auto plus_const = [](const vc::vc_image& src, float c) {
        return add(src, scale_channels(vc::vc_image::with_fill<vc::buf_f32>(
                                           src.width(), src.height(),
                                           src.channels(), 1.0F),
                                       c, c, c));
    };
    // LIFETIME: these images must be NAMED, not left as temporaries.
    // vc_image::pixels() hands back the shared_ptr BY VALUE, so
    // `make_image().pixels()->as<T>()` drops the last two references at the end
    // of the full expression and leaves the span pointing at freed memory. The
    // span outliving the image it came from is silent — it reads plausible
    // garbage rather than crashing.
    const vc::vc_image affine_lhs_img = plus_const(a_plus_b, 0.1F);
    const vc::vc_image affine_rhs_img =
        add(plus_const(a, 0.1F), plus_const(b, 0.1F));
    const auto affine_lhs = affine_lhs_img.pixels()->as<vc::buf_f32>();
    const auto affine_rhs = affine_rhs_img.pixels()->as<vc::buf_f32>();
    bool affine_is_linear = true;
    for (std::size_t i = 0; i < affine_lhs.size(); ++i) {
        if (!approx_equal(affine_lhs[i], affine_rhs[i], 1e-6F)) {
            affine_is_linear = false;
            break;
        }
    }
    ok = check(!affine_is_linear,
               "add-a-constant is affine, so it FAILS the linearity test") &&
         ok;

    // ---- per-channel scale, checked numerically ----
    //
    // Dumping a warm-looking image proves nothing; these are the values.
    // Probe pixel 2 is (0.5, 0.25, 0.75), so gains (2, 4, 0.5) must give
    // (1.0, 1.0, 0.375) — each channel independent of the others, which is
    // what "diagonal matrix" means in practice.
    const vc::vc_image scaled = scale_channels(probe, 2.0F, 4.0F, 0.5F);
    const auto scaled_px = scaled.pixels()->as<vc::buf_f32>();
    ok = check(
             approx_equal(scaled_px[scaled.meta().index(2, 0, 0)], 1.0F, 1e-6F),
             "scale_channels: 0.5 * 2 == 1.0") &&
         ok;
    ok = check(
             approx_equal(scaled_px[scaled.meta().index(2, 0, 1)], 1.0F, 1e-6F),
             "scale_channels: 0.25 * 4 == 1.0") &&
         ok;
    ok = check(approx_equal(scaled_px[scaled.meta().index(2, 0, 2)], 0.375F,
                            1e-6F),
               "scale_channels: 0.75 * 0.5 == 0.375") &&
         ok;

    // ---- invert, checked at the endpoints ----
    //
    // The involution check above would still pass for the identity map, so
    // the actual values need pinning too.
    const vc::vc_image probe_inv = invert(probe);
    const auto inv_px = probe_inv.pixels()->as<vc::buf_f32>();
    ok = check(
             approx_equal(inv_px[probe_inv.meta().index(0, 0, 0)], 0.0F, 1e-6F),
             "invert: 1.0 -> 0.0") &&
         ok;
    ok = check(
             approx_equal(inv_px[probe_inv.meta().index(0, 0, 1)], 1.0F, 1e-6F),
             "invert: 0.0 -> 1.0") &&
         ok;

    // ---- channel splits, all three ----
    //
    // Checked for every channel, not just R: a swap between G and B would
    // survive an R-only assertion untouched.
    for (vc::channel_count keep = 0; keep < img.channels(); ++keep) {
        const vc::vc_image split = channel_only(img, keep);
        const auto split_px = split.pixels()->as<vc::buf_f32>();
        bool split_ok = true;
        for (vc::image_dim y = 0; y < img.height() && split_ok; ++y) {
            for (vc::image_dim x = 0; x < img.width(); ++x) {
                bool pixel_ok = true;
                for (vc::channel_count ch = 0; ch < img.channels(); ++ch) {
                    const float expected =
                        (ch == keep) ? original_px[img.meta().index(x, y, ch)]
                                     : 0.0F;
                    if (split_px[img.meta().index(x, y, ch)] != expected) {
                        pixel_ok = false;
                        break;
                    }
                }
                if (!pixel_ok) {
                    split_ok = false;
                    break;
                }
            }
        }
        ok = check(split_ok,
                   "channel_only keeps its channel and zeroes the others") &&
             ok;
    }

    // ---- dumps ----
    writer.write(out_dir + "/01_grayscale.png", to_grayscale(img), png);
    writer.write(out_dir + "/01_inverted.png", inverted, png);
    writer.write(out_dir + "/01_brighter.png", brighten(img, 1.8F), png);
    writer.write(out_dir + "/01_warm.png",
                 scale_channels(img, 1.3F, 1.0F, 0.8F), png);
    writer.write(out_dir + "/01_channel_r.png", channel_only(img, 0), png);
    writer.write(out_dir + "/01_channel_g.png", channel_only(img, 1), png);
    writer.write(out_dir + "/01_channel_b.png", channel_only(img, 2), png);
    std::cout << "wrote 7 PNGs to " << out_dir << '\n';
    std::cout << "  compare 01_channel_r/g/b — each should be a plausible "
                 "single-colour version of the scene\n";

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
