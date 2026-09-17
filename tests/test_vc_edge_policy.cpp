// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// The edge policies, tested as a TABLE rather than through a picture.
//
// Every one of these could be checked by blurring a photo and squinting at the
// border. None of them should be. A one-index error in `reflect` produces a
// blur that looks entirely correct except for a single pixel at four edges,
// and a missing second modulo in `wrap` produces an index of about four
// billion, which is a crash or a garbage read depending on the day.
//
// THE FIXTURES ARE 5x1 AND 1x5, AND THAT IS THE POINT. A 5x1 image makes
// fetch() into the x-axis table; a 1x5 makes it the y-axis table. Testing both
// also catches the failure a purely 1-D helper could never see: passing
// height() where width() belongs. That bug compiles, runs, and is invisible on
// every square test image.
//
// The reference row, width 5, indices 0..4:
//
//     index:       -3 -2 -1 | 0  1  2  3  4 | 5  6  7
//                  --------- ---------------- --------
//     zero:         0  0  0 | 1  2  3  4  5 | 0  0  0   <- VALUES, not indices
//     clamp:        1  1  1 | 1  2  3  4  5 | 5  5  5
//     reflect:      4  3  2 | 1  2  3  4  5 | 4  3  2
//     wrap:         3  4  5 | 1  2  3  4  5 | 1  2  3
//
// Read the reflect row carefully, because it is the one with a choice in it.
// The mirror is about the EDGE PIXEL, so index 0 is the axis of symmetry and
// is not repeated: index -1 reads the value at index 1. The other convention
// (mirror about the half-pixel boundary, giving -1 -> 0) is equally common,
// and under it reflect and clamp would agree for a one-tap overhang -- which
// would make this entire table unable to tell them apart.

#include <cstdint>
#include <span>

#include "doctest/doctest.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/pixelops/vc_edge_policy.h"

namespace {

using vc::pixelops::vc_edge_policy;

// Values 1..5 so that "the value at index i" is i+1 -- distinguishable from
// zero, and readable straight off the table above.
vc::vc_image line(vc::image_dim w, vc::image_dim h) {
    vc::vc_image_writer out{w, h, 1, vc::buf_f32{0.0F}};
    float v = 1.0F;
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            out.at<vc::buf_f32>(x, y, 0) = v;
            v += 1.0F;
        }
    }
    return std::move(out).seal();
}

// fetch() along x on a 5-wide, 1-tall image.
float fx(const vc::vc_image& img, std::int64_t i, vc_edge_policy p) {
    float got = 0.0F;
    img.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        got = vc::pixelops::fetch(px, img, i, 0, 0, p);
    });
    return got;
}

// fetch() along y on a 1-wide, 5-tall image. Same expected table -- if these
// two ever disagree, x has been paired with height() or y with width().
float fy(const vc::vc_image& img, std::int64_t i, vc_edge_policy p) {
    float got = 0.0F;
    img.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        got = vc::pixelops::fetch(px, img, 0, i, 0, p);
    });
    return got;
}

} // namespace

TEST_CASE("fetch: in-range indices read the real pixel, whatever the policy") {
    const vc::vc_image row = line(5, 1);
    const vc::vc_image col = line(1, 5);
    for (const auto p : {vc_edge_policy::zero, vc_edge_policy::clamp,
                         vc_edge_policy::reflect, vc_edge_policy::wrap}) {
        for (std::int64_t i = 0; i < 5; ++i) {
            const auto expected = static_cast<float>(i + 1);
            CHECK(fx(row, i, p) == doctest::Approx(expected));
            CHECK(fy(col, i, p) == doctest::Approx(expected));
        }
    }
}

TEST_CASE("fetch: zero contributes nothing outside the image") {
    const vc::vc_image row = line(5, 1);
    const vc::vc_image col = line(1, 5);
    // 0, not the edge value. A policy that returned 1.0F here would be clamp
    // wearing zero's name, and a box blur would stop darkening its border --
    // which is the single most visible thing `zero` is for.
    for (const std::int64_t i : {-3, -1, 5, 7}) {
        CHECK(fx(row, i, vc_edge_policy::zero) == doctest::Approx(0.0F));
        CHECK(fy(col, i, vc_edge_policy::zero) == doctest::Approx(0.0F));
    }
}

TEST_CASE("fetch: clamp replicates the edge pixel") {
    const vc::vc_image row = line(5, 1);
    const vc::vc_image col = line(1, 5);
    for (const std::int64_t i : {-1, -3, -12}) {
        CHECK(fx(row, i, vc_edge_policy::clamp) == doctest::Approx(1.0F));
        CHECK(fy(col, i, vc_edge_policy::clamp) == doctest::Approx(1.0F));
    }
    for (const std::int64_t i : {5, 7, 16}) {
        CHECK(fx(row, i, vc_edge_policy::clamp) == doctest::Approx(5.0F));
        CHECK(fy(col, i, vc_edge_policy::clamp) == doctest::Approx(5.0F));
    }
}

TEST_CASE("fetch: reflect mirrors about the EDGE PIXEL, not the boundary") {
    const vc::vc_image row = line(5, 1);
    const vc::vc_image col = line(1, 5);

    // The distinguishing case. Under the half-pixel convention this is 1.0F.
    CHECK(fx(row, -1, vc_edge_policy::reflect) == doctest::Approx(2.0F));
    CHECK(fx(row, -2, vc_edge_policy::reflect) == doctest::Approx(3.0F));
    CHECK(fx(row, -3, vc_edge_policy::reflect) == doctest::Approx(4.0F));

    // Mirrored about index 4 on the high side.
    CHECK(fx(row, 5, vc_edge_policy::reflect) == doctest::Approx(4.0F));
    CHECK(fx(row, 6, vc_edge_policy::reflect) == doctest::Approx(3.0F));
    CHECK(fx(row, 7, vc_edge_policy::reflect) == doctest::Approx(2.0F));

    // Same table down the other axis.
    CHECK(fy(col, -1, vc_edge_policy::reflect) == doctest::Approx(2.0F));
    CHECK(fy(col, 5, vc_edge_policy::reflect) == doctest::Approx(4.0F));

    // reflect and clamp MUST differ at a one-tap overhang. If this fails,
    // reflect has been written as the half-pixel-boundary variant and the two
    // policies have silently collapsed into one.
    CHECK(fx(row, -1, vc_edge_policy::reflect) !=
          doctest::Approx(fx(row, -1, vc_edge_policy::clamp)));
}

TEST_CASE("fetch: wrap tiles the plane, and survives C++'s negative modulo") {
    const vc::vc_image row = line(5, 1);
    const vc::vc_image col = line(1, 5);

    // -1 % 5 is -1 in C++, not 4. One modulo leaves a negative index, and the
    // cast to an unsigned image_dim then yields roughly four billion.
    CHECK(fx(row, -1, vc_edge_policy::wrap) == doctest::Approx(5.0F));
    CHECK(fx(row, -2, vc_edge_policy::wrap) == doctest::Approx(4.0F));
    CHECK(fx(row, 5, vc_edge_policy::wrap) == doctest::Approx(1.0F));
    CHECK(fx(row, 6, vc_edge_policy::wrap) == doctest::Approx(2.0F));

    // More than one full period out, both directions. A fold-back written as a
    // single conditional passes everything above and fails these.
    CHECK(fx(row, -5, vc_edge_policy::wrap) == doctest::Approx(1.0F));
    CHECK(fx(row, -6, vc_edge_policy::wrap) == doctest::Approx(5.0F));
    CHECK(fx(row, 10, vc_edge_policy::wrap) == doctest::Approx(1.0F));
    CHECK(fx(row, 11, vc_edge_policy::wrap) == doctest::Approx(2.0F));

    CHECK(fy(col, -1, vc_edge_policy::wrap) == doctest::Approx(5.0F));
    CHECK(fy(col, 5, vc_edge_policy::wrap) == doctest::Approx(1.0F));
}

TEST_CASE("fetch: x pairs with width and y with height, on a NON-SQUARE "
          "image") {
    // The test a 1-D helper cannot express, and the reason fetch() takes the
    // image rather than a bare extent. On a 7x3 image, running off the bottom
    // must fold against 3 and running off the right must fold against 7. Swap
    // the two and this is the first thing that fails.
    const vc::vc_image img = line(7, 3);

    const auto at = [&img](std::int64_t x, std::int64_t y, vc_edge_policy p) {
        float got = 0.0F;
        img.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
            got = vc::pixelops::fetch(px, img, x, y, 0, p);
        });
        return got;
    };

    // Values run 1..21 row-major, so (x, y) holds y*7 + x + 1.
    CHECK(at(0, 0, vc_edge_policy::clamp) == doctest::Approx(1.0F));
    CHECK(at(6, 2, vc_edge_policy::clamp) == doctest::Approx(21.0F));

    // y = 3 is out of range (height 3) and must clamp to row 2, not to
    // whatever row 3 would be if the height were 7.
    CHECK(at(0, 3, vc_edge_policy::clamp) == doctest::Approx(15.0F));

    // x = 7 is out of range (width 7) and must clamp to column 6.
    CHECK(at(7, 0, vc_edge_policy::clamp) == doctest::Approx(7.0F));

    // wrap makes the swap unmistakable: y wraps modulo 3, x modulo 7.
    CHECK(at(0, 3, vc_edge_policy::wrap) == doctest::Approx(1.0F));
    CHECK(at(7, 0, vc_edge_policy::wrap) == doctest::Approx(1.0F));
    CHECK(at(0, 4, vc_edge_policy::wrap) == doctest::Approx(8.0F));
}

TEST_CASE("fetch: a width-1 image is degenerate but not special-cased") {
    // Every index collapses onto the single pixel. Worth pinning because
    // reflect's 2*(n-1) - i becomes -i when n == 1, and wrap's modulo by 1 is
    // always 0 -- both correct, both easy to break with a guard written for
    // the general case.
    const vc::vc_image dot = line(1, 1);
    for (std::int64_t i = -3; i <= 3; ++i) {
        CHECK(fx(dot, i, vc_edge_policy::clamp) == doctest::Approx(1.0F));
        CHECK(fx(dot, i, vc_edge_policy::reflect) == doctest::Approx(1.0F));
        CHECK(fx(dot, i, vc_edge_policy::wrap) == doctest::Approx(1.0F));
    }
}
