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
// means denoising (P9b) are built from. The second half of the file turns the
// distance into a SEARCH: slide the window over every valid position, score
// each one, and the position of the minimum is the match. That map of scores
// is the object worth looking at -- a single best position is a summary of it,
// and a summary hides whether the minimum was sharp or the whole surface was
// flat.
//
// One property to note for later: SSD and SAD both compare LENGTHS, so a
// uniform brightness change between two otherwise identical patches registers
// as a mismatch. When aligning exposure-bracketed frames that is exactly the
// wrong behaviour, which is why NCC — an angle rather than a distance — shows
// up in that setting. Not implemented here.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
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

// ---------------------------------------------------------------------
// Part 2 -- the sliding-window search
// ---------------------------------------------------------------------

// Where the best match sits, and how good it was. The score travels with the
// position deliberately: "position 7,3" alone cannot tell you whether that was
// a confident match or the least-bad of a uniformly hopeless set.
struct match_result {
    vc::image_dim x;
    vc::image_dim y;
    float score;
};

// The SSD of `query` against every window position in `haystack`.
//
// The result is SMALLER than the haystack, and getting that boundary right is
// most of the exercise: a w-wide patch has valid origins 0 .. W-w inclusive,
// so the map is (W - w + 1) x (H - h + 1). Off by one here and the last column
// either reads out of bounds or is silently never searched.
//
// Single channel regardless of the input's channel count -- one score per
// position, summed over all channels by accumulate_over().
vc::vc_image ssd_map(const vc::vc_image& haystack, const patch& query) {
    if (query.width > haystack.width() || query.height > haystack.height()) {
        throw std::invalid_argument(
            "ssd_map: query patch is larger than the haystack");
    }
    vc::vc_image_writer out{haystack.width() - query.width + 1,
                            haystack.height() - query.height + 1, 1,
                            vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y <= haystack.height() - query.height; ++y) {
        for (vc::image_dim x = 0; x <= haystack.width() - query.width; ++x) {
            patch window{.image = &haystack,
                         .x = x,
                         .y = y,
                         .width = query.width,
                         .height = query.height};
            float score = ssd(window, query);
            out.at<vc::buf_f32>(x, y, 0) = score;
        }
    }
    return std::move(out).seal();
}

// The position of the minimum. Separate from ssd_map() on purpose: the map is
// what you dump and inspect, the argmin is what a caller acts on, and keeping
// them apart means you can look at the surface without recomputing it.
//
// Ties: return the FIRST minimum in scan order. Arbitrary, but stated -- an
// unstated tie-break is how a test passes on one machine and not another.
match_result best_match(const vc::vc_image& map) {
    auto px = map.pixels()->as<vc::buf_f32>();
    float best_score = std::numeric_limits<float>::max();
    vc::image_dim best_x = 0;
    vc::image_dim best_y = 0;
    for (vc::image_dim y = 0; y < map.height(); ++y) {
        for (vc::image_dim x = 0; x < map.width(); ++x) {
            float score = px[map.meta().index(x, y, 0)];
            if (score < best_score) {
                best_score = score;
                best_x = x;
                best_y = y;
            }
        }
    }
    return match_result{.x = best_x, .y = best_y, .score = best_score};
}

// Scale a map into [0, 1] so it can be written as a PNG. SSD values are
// unbounded and would otherwise clip to white everywhere.
//
// Low SSD means a good match, so the match stays DARK -- a literal trough,
// which is what makes the dump readable at a glance. Not inverted into a
// "heat map" for that reason: the criterion is that the surface troughs at the
// true position, and a trough should look like one.
float peak_of(const vc::vc_image& map) {
    float peak = 0.0F;
    map.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        for (const float v : px) {
            peak = std::max(peak, v);
        }
    });
    return peak;
}

// What fraction of positions score within `slack` of the best?
//
// This is the MEASURABLE form of "the map troughs at the true match". The
// argmin alone cannot express it: a map with one clear minimum and a map that
// is a wide basin of near-ties both have an argmin, and in the second case
// which position wins is luck rather than a match. The fraction says how much
// competition the winner actually had.
//
// Absolute slack, not relative: a self-match scores exactly 0, so a relative
// threshold would divide by zero. The units are SSD in the image's own scale.
float ambiguity_fraction(const vc::vc_image& map, float slack) {
    float best = std::numeric_limits<float>::max();
    std::size_t near = 0;
    std::size_t total = 0;
    map.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        for (const float v : px) {
            best = std::min(best, v);
        }
        for (const float v : px) {
            if (v <= best + slack) {
                ++near;
            }
            ++total;
        }
    });
    return static_cast<float>(near) / static_cast<float>(total);
}

// The best score anywhere OUTSIDE a neighbourhood of (tx, ty).
//
// This is "the map troughs at the true match" made checkable WITHOUT choosing
// a tolerance -- which matters, because the ambiguity sweep above shows the
// answer flips depending on which tolerance you pick. A trough means the true
// position is strictly better than every genuine alternative, and the margin
// between them says how much better.
//
// The radius exists because immediate neighbours of an exact match are almost
// exact too; they are the same match, not competitors.
float best_outside(const vc::vc_image& map,
                   vc::image_dim tx,
                   vc::image_dim ty,
                   vc::image_dim radius) {
    float best = std::numeric_limits<float>::max();
    for (vc::image_dim y = 0; y < map.height(); ++y) {
        for (vc::image_dim x = 0; x < map.width(); ++x) {
            const bool near_x = x + radius >= tx && x <= tx + radius;
            const bool near_y = y + radius >= ty && y <= ty + radius;
            if (near_x && near_y) {
                continue;
            }
            best = std::min(best, map.at<vc::buf_f32>(x, y, 0));
        }
    }
    return best;
}

// `peak` is passed IN rather than measured here so that two maps can be
// rendered on ONE scale. Normalising each by its own maximum makes them
// individually readable and mutually incomparable -- the darker of two such
// images may simply have had a smaller peak, which is exactly the illusion a
// side-by-side comparison must not create.
vc::vc_image normalised(const vc::vc_image& map, float peak) {
    // An all-zero peak means every position matched perfectly, which happens
    // for a uniform image. Dividing by zero would produce NaN and a garbage
    // PNG; a flat black image is the honest rendering of "no position is worse
    // than any other".
    const float scale = peak > 0.0F ? 1.0F / peak : 0.0F;

    vc::vc_image_writer out{map.width(), map.height(), 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < map.height(); ++y) {
        for (vc::image_dim x = 0; x < map.width(); ++x) {
            out.at<vc::buf_f32>(x, y, 0) = map.at<vc::buf_f32>(x, y, 0) * scale;
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

    // ---- part 2: find the duplicate by searching for it ----
    //
    // `ramp` is 6x4 with the 2x2 block from (3, 2) also pasted at (0, 0), so
    // there are two exact matches and the search must land on one of them.
    // Ties resolve to the first in scan order, which is (0, 0).
    const vc::vc_image map = ssd_map(ramp, window);

    // The boundary, pinned. A 2x2 patch over a 6x4 image has origins
    // 0..4 and 0..2, so the map is 5 x 3. Off by one and the map is 6 x 4
    // (reading out of bounds) or 4 x 2 (never searching the last column).
    ok = check(map.width() == 5 && map.height() == 3,
               "the SSD map is (W - w + 1) x (H - h + 1)") &&
         ok;

    const match_result best = best_match(map);
    std::cout << "search: best match at (" << best.x << ", " << best.y
              << ") with SSD = " << best.score << "   (expected (0, 0), 0)\n";
    ok = check(best.x == 0 && best.y == 0,
               "the search finds the duplicated block at its known location") &&
         ok;
    ok = check(best.score == 0.0F, "an exact duplicate scores exactly 0") && ok;

    // Both known-exact positions really are zero, and a neighbouring one is
    // not -- so the minimum is a genuine trough rather than a flat surface
    // that happens to start low.
    ok = check(map.at<vc::buf_f32>(0, 0, 0) == 0.0F &&
                   map.at<vc::buf_f32>(3, 2, 0) == 0.0F,
               "both copies of the block score 0") &&
         ok;
    ok =
        check(map.at<vc::buf_f32>(1, 0, 0) > 0.0F,
              "a neighbouring position scores worse -- the minimum is sharp") &&
        ok;

    // ---- the same search on the photo, and the map as a picture ----
    //
    // A patch taken FROM the photo must find itself, at distance exactly 0.
    // That is the strongest available ground truth: no tolerance, no
    // eyeballing, and it exercises the full-size loop rather than a 6x4 toy.
    // A query patch from a TEXTURED region, not from `here`.
    //
    // `here` sits at the image centre, which on this photo is a flat, dark
    // area. A patch with no distinctive structure matches every OTHER flat
    // area about as well, so its SSD map is a broad basin with the correct
    // minimum buried in it -- numerically right, and useless as a picture.
    // The acceptance criterion asks the map to TROUGH at the true match, and a
    // basin is not a trough.
    //
    // This is not a quirk of one photo. It is the reason P3 builds a corner
    // detector: Harris answers "which patches are worth matching at all", and
    // a flat patch is the case it exists to reject. Block matching is only as
    // good as the distinctiveness of what you choose to match.
    const vc::image_dim tx = 400; // foliage -- high local contrast
    const vc::image_dim ty = 500;
    const patch textured{
        .image = &photo, .x = tx, .y = ty, .width = 8, .height = 8};

    const vc::vc_image photo_map = ssd_map(photo, textured);
    const match_result photo_best = best_match(photo_map);
    std::cout << "photo search: patch from (" << tx << ", " << ty
              << ") found at (" << photo_best.x << ", " << photo_best.y
              << ") with SSD = " << photo_best.score << '\n';
    ok = check(photo_best.x == tx && photo_best.y == ty,
               "a patch cut from the photo is found at its own location") &&
         ok;
    ok = check(photo_best.score == 0.0F,
               "and matches itself at exactly zero distance") &&
         ok;

    // How much competition did each winner have? The sweep is reported, not
    // asserted: which query looks more ambiguous REVERSES with the tolerance
    // chosen -- foliage contains genuine near-duplicate windows, so it is
    // worse at tight tolerance, while everything flat is vaguely alike, so the
    // flat query is worse at loose tolerance. Any single-threshold assertion
    // would be picking the regime that flatters the conclusion.

    const vc::vc_image flat_map = ssd_map(photo, here);

    std::cout << "SWEEP slack: textured%  flat%\n";
    for (const float s : {0.1F, 0.5F, 1.0F, 2.0F, 5.0F, 10.0F, 20.0F}) {
        std::cout << "  " << s << ": "
                  << (ambiguity_fraction(photo_map, s) * 100.0F) << "   "
                  << (ambiguity_fraction(flat_map, s) * 100.0F) << "\n";
    }

    // The criterion the argmin check cannot express, without depending on a
    // tolerance. Note the sweep above deliberately does NOT get an assertion:
    // which query looks more ambiguous flips with the tolerance chosen, so any
    // single-threshold claim would be picking the regime that suits the story.
    const float rival_textured = best_outside(photo_map, tx, ty, 4);
    const float rival_flat = best_outside(flat_map, cx, cy, 4);
    std::cout << "best rival outside a radius of 4:  textured = "
              << rival_textured << "   flat = " << rival_flat << '\n';
    ok = check(rival_textured > 0.0F && rival_flat > 0.0F,
               "the true position is strictly better than every rival -- the "
               "minimum is a trough, not a plateau") &&
         ok;

    vc::io::stb_image_writer writer;
    const vc::io::write_config png{.format = vc::io::vc_image_format::png};
    // ONE scale for both, so the two images can honestly be compared.
    const float shared_peak = std::max(peak_of(photo_map), peak_of(flat_map));
    writer.write(std::string(VC_EXAMPLES_OUTPUT_DIR) + "/02_ssd_map.png",
                 normalised(photo_map, shared_peak), png);
    writer.write(std::string(VC_EXAMPLES_OUTPUT_DIR) + "/02_ssd_map_flat.png",
                 normalised(flat_map, shared_peak), png);
    std::cout << "wrote 02_ssd_map.png -- dark is a good match; the trough at "
                 "the patch's own position is the answer the search returns\n";

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
