// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 02 — how different are two patches?
//
// Unlike example 01, this operation is NOT per-pixel: it consumes a whole
// neighbourhood and produces a single number. The natural space is therefore
// patch space — flatten a w x h x c patch into a vector of w*h*c numbers and
// the question "how different are these two patches" becomes the ordinary
// geometric question "how far apart are these two points":
//
//     SSD(a, b) = ||a - b||^2      sum of squared differences
//     SAD(a, b) = ||a - b||_1      sum of absolute differences
//     L2 (a, b) = ||a - b||        the plain Euclidean distance
//
// Three metrics, one idea, differing only in which norm is applied to the
// difference vector.
//
// Flattening does not throw away locality here, and that is worth being clear
// about: the ARRAY indexing is what chooses which values belong together (a
// contiguous neighbourhood), and both patches are walked in the same order, so
// element k of one corresponds to element k of the other. Locality lives in
// the extraction; the algebra happens afterwards.
//
// This is the primitive that block-matching alignment (P11a) and non-local
// means denoising (P9b) are built from. Sliding a window over an image to find
// the best-matching patch comes later; this example only establishes the
// distance itself.
//
// One property to note for later: SSD and SAD both compare LENGTHS, so a
// uniform brightness change between two otherwise identical patches registers
// as a mismatch. When aligning exposure-bracketed frames that is exactly the
// wrong behaviour, which is why NCC — an angle rather than a distance — shows
// up in that setting. Not implemented here.

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
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

bool approx_equal(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

// A non-owning rectangular view into an image. Nothing is copied — the patch
// is a coordinate window, and the distance functions walk it in place.
struct patch {
    const vc::vc_image* image;
    vc::image_dim x;
    vc::image_dim y;
    vc::image_dim width;
    vc::image_dim height;
};

bool same_shape(const patch& a, const patch& b) {
    return a.width == b.width && a.height == b.height &&
           a.image->channels() == b.image->channels();
}

// The one traversal all three metrics share. `combine` receives each pair of
// corresponding elements and returns that pair's contribution to the total —
// so SSD and SAD differ by exactly one lambda, which is the point.
template <typename Combine>
float accumulate_over(const patch& a, const patch& b, Combine combine) {
    // Enforced here rather than left to the caller: comparing patches of
    // different shape is meaningless, and element k of one would stop
    // corresponding to element k of the other — the whole basis of the metric.
    if (!same_shape(a, b)) {
        throw std::invalid_argument(
            "patch distance: both patches must have the same width, height "
            "and channel count");
    }
    const auto pa = a.image->pixels()->as<vc::buf_f32>();
    const auto pb = b.image->pixels()->as<vc::buf_f32>();
    const vc::channel_count channels = a.image->channels();

    float total = 0.0F;
    for (vc::image_dim dy = 0; dy < a.height; ++dy) {
        for (vc::image_dim dx = 0; dx < a.width; ++dx) {
            for (vc::channel_count ch = 0; ch < channels; ++ch) {
                // Both patches walked in the SAME order, so element k here
                // corresponds to element k there.
                const float va =
                    pa[a.image->meta().index(a.x + dx, a.y + dy, ch)];
                const float vb =
                    pb[b.image->meta().index(b.x + dx, b.y + dy, ch)];
                total += combine(va - vb);
            }
        }
    }
    return total;
}

float ssd(const patch& a, const patch& b) {
    return accumulate_over(a, b, [](float d) { return d * d; });
}

float sad(const patch& a, const patch& b) {
    return accumulate_over(a, b, [](float d) { return std::fabs(d); });
}

// L2 is the square root of SSD — the same quantity, un-squared. SSD is usually
// preferred in a search loop precisely because skipping the sqrt changes no
// ordering: sqrt is monotonic, so whichever patch minimises SSD also minimises
// L2, for free.
float l2(const patch& a, const patch& b) {
    return std::sqrt(ssd(a, b));
}

// A single-channel image built from an explicit list of values, row-major.
vc::vc_image make_gray(vc::image_dim w,
                       vc::image_dim h,
                       std::initializer_list<float> values) {
    if (values.size() != static_cast<std::size_t>(w) * h) {
        throw std::invalid_argument(
            "make_gray: value count does not match w * h");
    }
    vc::vc_image_writer out{w, h, 1, vc::buf_f32{0.0F}};
    const float* v = values.begin();
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            out.at<vc::buf_f32>(x, y, 0) = *v++;
        }
    }
    return std::move(out).seal();
}

// A 6x4 single-channel ramp, value(x, y) = 10x + y — every element distinct,
// and non-square so an x/y swap cannot land somewhere plausible. The 2x2 block
// living at (3, 2) is ALSO written at (0, 0), giving a duplicate at a known
// location to match against.
vc::vc_image make_ramp_with_duplicate() {
    constexpr vc::image_dim kW = 6;
    constexpr vc::image_dim kH = 4;
    vc::vc_image_writer out{kW, kH, 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < kH; ++y) {
        for (vc::image_dim x = 0; x < kW; ++x) {
            out.at<vc::buf_f32>(x, y, 0) = static_cast<float>(10 * x + y);
        }
    }
    // Paste the (3, 2) block over (0, 0).
    for (vc::image_dim dy = 0; dy < 2; ++dy) {
        for (vc::image_dim dx = 0; dx < 2; ++dx) {
            out.at<vc::buf_f32>(dx, dy, 0) =
                static_cast<float>(10 * (3 + dx) + (2 + dy));
        }
    }
    return std::move(out).seal();
}

} // namespace

int main() {
    bool ok = true;

    // ---- the hand-computed 2x2 case ----
    //
    //   A = [ 1  2 ]      B = [ 2  4 ]      A - B = [ -1  -2 ]
    //       [ 3  4 ]          [ 6  8 ]              [ -3  -4 ]
    //
    //   SSD = 1 + 4 + 9 + 16 = 30
    //   SAD = 1 + 2 + 3 + 4  = 10
    //   L2  = sqrt(30)       = 5.477225575... (prints as 5.47723)
    const vc::vc_image a_img = make_gray(2, 2, {1.0F, 2.0F, 3.0F, 4.0F});
    const vc::vc_image b_img = make_gray(2, 2, {2.0F, 4.0F, 6.0F, 8.0F});
    const patch a{.image = &a_img, .x = 0, .y = 0, .width = 2, .height = 2};
    const patch b{.image = &b_img, .x = 0, .y = 0, .width = 2, .height = 2};

    std::cout << "hand case:  SSD = " << ssd(a, b) << "  SAD = " << sad(a, b)
              << "  L2 = " << l2(a, b) << "\n"
              << "  expected:  SSD = 30  SAD = 10  L2 = 5.47723\n";

    ok = check(same_shape(a, b), "patches have matching shape") && ok;
    ok = check(approx_equal(ssd(a, b), 30.0F, 1e-5F),
               "SSD matches hand value 30") &&
         ok;
    ok = check(approx_equal(sad(a, b), 10.0F, 1e-5F),
               "SAD matches hand value 10") &&
         ok;
    ok = check(approx_equal(l2(a, b), std::sqrt(30.0F), 1e-5F),
               "L2 matches hand value sqrt(30)") &&
         ok;

    // L2 really is the square root of SSD, not an independently computed thing.
    ok = check(approx_equal(l2(a, b) * l2(a, b), ssd(a, b), 1e-4F),
               "L2 squared == SSD") &&
         ok;

    // ---- identity and symmetry: the properties a distance must have ----
    ok = check(ssd(a, a) == 0.0F, "a patch is at distance 0 from itself") && ok;
    ok = check(approx_equal(ssd(a, b), ssd(b, a), 1e-6F), "SSD is symmetric") &&
         ok;

    // ---- predict which candidate is closer, then verify ----
    //
    // Against A = [1 2 / 3 4], `near_img` differs by 0.1 in each element and
    // `far_img` by 1.0 in each, so the ordering is decidable before running
    // anything — which is the point of predicting first.
    const vc::vc_image near_img = make_gray(2, 2, {1.1F, 2.1F, 3.1F, 4.1F});
    const vc::vc_image far_img = make_gray(2, 2, {2.0F, 3.0F, 4.0F, 5.0F});
    const patch near_p{
        .image = &near_img, .x = 0, .y = 0, .width = 2, .height = 2};
    const patch far_p{
        .image = &far_img, .x = 0, .y = 0, .width = 2, .height = 2};

    // Four elements, each off by 0.1 -> SSD = 4 * 0.01 = 0.04
    // Four elements, each off by 1.0 -> SSD = 4 * 1.00 = 4.00
    std::cout << "candidates: SSD(near) = " << ssd(a, near_p)
              << "  SSD(far) = " << ssd(a, far_p)
              << "   (expected 0.04 and 4)\n";
    ok = check(approx_equal(ssd(a, near_p), 0.04F, 1e-5F),
               "SSD to near candidate is 0.04") &&
         ok;
    ok = check(approx_equal(ssd(a, far_p), 4.0F, 1e-5F),
               "SSD to far candidate is 4") &&
         ok;
    ok = check(ssd(a, near_p) < ssd(a, far_p),
               "the nearer patch scores lower — the prediction holds") &&
         ok;

    // All three metrics must agree on the ORDERING even though their
    // magnitudes differ; that is what makes them interchangeable for search.
    ok = check(sad(a, near_p) < sad(a, far_p), "SAD agrees on the ordering") &&
         ok;
    ok = check(l2(a, near_p) < l2(a, far_p), "L2 agrees on the ordering") && ok;

    // ---- extraction at a NON-ZERO offset, against known values ----
    //
    // Everything above compares patches anchored at (0, 0) — whole 2x2 images
    // — so accumulate_over's offset arithmetic (a.x + dx, a.y + dy) has never
    // been exercised against a known answer. That is a genuine hole: swapping
    // x and y there would still pass the hand case (both offsets are zero),
    // and would still pass the photo checks below, because both patches would
    // be displaced identically — self-distance stays 0 and a shift stays
    // non-zero.
    //
    // The fixture is 6 wide and 4 tall. Non-square on purpose: with a square
    // one, a swapped x/y lands on a valid coordinate and reads plausible
    // values instead of failing.
    const vc::vc_image ramp = make_ramp_with_duplicate();

    // value(x, y) = 10x + y, so the 2x2 window anchored at (3, 2) holds
    //     (3,2) (4,2)      32  42
    //     (3,3) (4,3)  =   33  43
    const patch window{.image = &ramp, .x = 3, .y = 2, .width = 2, .height = 2};
    const vc::vc_image expected = make_gray(2, 2, {32.0F, 42.0F, 33.0F, 43.0F});
    const patch expected_p{
        .image = &expected, .x = 0, .y = 0, .width = 2, .height = 2};
    ok = check(ssd(window, expected_p) == 0.0F,
               "a patch anchored at (3, 2) reads exactly the values stored "
               "there") &&
         ok;

    // The same 2x2 block also sits at (0, 0), placed there deliberately. A
    // patch is at distance 0 from its duplicate and non-zero from anywhere
    // else — which is the ground truth a search would have to recover.
    // SEARCHING for that location is S6, week 3; this only pins extraction.
    const patch duplicate{
        .image = &ramp, .x = 0, .y = 0, .width = 2, .height = 2};
    const patch wrong{.image = &ramp, .x = 1, .y = 0, .width = 2, .height = 2};
    ok = check(ssd(window, duplicate) == 0.0F,
               "the duplicated block scores exactly 0 at its known location") &&
         ok;
    ok = check(ssd(window, wrong) > 0.0F,
               "a neighbouring, wrong location scores worse") &&
         ok;
    std::cout << "ramp 2x2 window at (3, 2): SSD vs its duplicate at (0, 0) = "
              << ssd(window, duplicate)
              << ", vs (1, 0) = " << ssd(window, wrong) << "\n";

    // ---- on a real image ----
    //
    // A patch matched against itself must be exactly 0; the same patch shifted
    // by one pixel must not be, unless the region is perfectly flat.
    const vc::io::path input =
        std::string(VC_EXAMPLES_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    vc::io::stb_image_reader reader;
    const vc::vc_image photo =
        reader.read(input, vc::io::read_config{.dtype = vc::pixel_dtype::f32});

    const vc::image_dim cx = photo.width() / 2;
    const vc::image_dim cy = photo.height() / 2;
    const patch here{
        .image = &photo, .x = cx, .y = cy, .width = 8, .height = 8};
    const patch same{
        .image = &photo, .x = cx, .y = cy, .width = 8, .height = 8};
    const patch shifted{
        .image = &photo, .x = cx + 1, .y = cy, .width = 8, .height = 8};
    const patch elsewhere{
        .image = &photo, .x = cx / 2, .y = cy / 2, .width = 8, .height = 8};

    std::cout << "photo 8x8 patch at (" << cx << ", " << cy << "):\n"
              << "  vs itself:        SSD = " << ssd(here, same) << '\n'
              << "  vs shifted by 1:  SSD = " << ssd(here, shifted) << '\n'
              << "  vs a distant one: SSD = " << ssd(here, elsewhere) << '\n';

    ok = check(ssd(here, same) == 0.0F,
               "identical photo patches score exactly 0") &&
         ok;
    ok = check(ssd(here, shifted) > 0.0F,
               "a one-pixel shift is detectable — the region is not flat") &&
         ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
