// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 03 — moving pixels around: affine warp by inverse mapping.
//
// Examples 01 and 02 changed pixel VALUES. This one changes their POSITIONS,
// and the distinction is exactly the one Szeliski draws between transforming
// the RANGE of an image and transforming its DOMAIN:
//
//     g(x) = h(f(x))     h acts on the value     — grayscale, brightness (01)
//     g(x) = f(h(x))     h acts on the position  — translate, scale, rotate
//
// Look at where h sits in the second form. To produce the output at x you
// evaluate h(x) and read the SOURCE there — so h maps DESTINATION coordinates
// back to source ones. h is the INVERSE of the transform you asked for, and
// that is not an implementation trick, it is what the notation says.
//
// ---- Why inverse mapping ----
//
// The obvious alternative is to walk source pixels and write each one where it
// lands. That fails three ways. A transform does not send integers to
// integers, so after rounding some destination pixels are never written at all
// (holes — visible as speckle). Scaling down, many source pixels round to the
// same destination and silently overwrite each other. And interpolating
// properly would mean splatting one value across several destination pixels
// with weights, accumulating a weight sum per destination and normalising at
// the end — two buffers and two passes.
//
// Walking DESTINATION pixels instead visits each one exactly once, by
// construction, so holes and collisions are not merely unlikely but
// impossible. The fractional coordinate moves to the READ side, where four
// known neighbours make interpolation well defined.
//
// The asymmetry in one line: fractional coordinates are easy to read from and
// hard to write to.
//
// ---- The two stages are independent ----
//
//     the matrix    coordinate -> coordinate. Pure geometry, never sees a pixel.
//     the sampler   coordinate -> value.      Never learns which transform produced it.
//
// That separation is why ONE sampler serves translate, scale, rotate and any
// affine composition of them, and why swapping bilinear for bicubic would
// improve every transform at once. It is also, precisely, why downscaling
// aliases here: the sampler is never told the scale factor, so it cannot widen
// its footprint to match. See the note at the end of main().
//
// ---- Suggested build order ----
//
// Do NOT write this top to bottom. Each step isolates one failure:
//
//   1. identity            — exercises the loop and indexing, no interpolation
//   2. integer translation, NEAREST sampling (round instead of blending)
//                          — proves the inverse-map DIRECTION. If the image
//                            moves the wrong way you applied M, not M^-1, and
//                            this is the cheapest place to find out.
//   3. sub-pixel translation, still nearest — visibly jagged, which is the
//                            evidence that interpolation is needed
//   4. swap in bilinear    — same geometry, so a new bug is in the sampler
//   5. scale, then rotate  — rotation last: it exercises both axes at once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
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
#include "vc/math/vc_linalg.h"
#include "vc/math/vc_transform.h"

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

// What to do when the inverse-mapped coordinate falls outside the source.
// There is no correct answer, only a decided one — so it is a parameter rather
// than a hidden constant.
enum class edge_policy : std::uint8_t {
    clamp, // clamp the coordinate to the image, replicating the edge pixel
    zero,  // treat everything outside as 0
};

// How big the output canvas is.
enum class output_size : std::uint8_t {
    same_as_source, // geometry preserved; anything transformed outside is clipped
    fit_transform, // canvas grown so nothing is clipped — see fitted_destination
};

// PIXEL CENTRE CONVENTION: THE AREA CONVENTION. Pixel (i, j) covers the square
// [i, i+1) x [j, j+1) and its CENTRE sits at (i + 0.5, j + 0.5). The image
// occupies [0, W] x [0, H], so its centre is (W/2, H/2) and its corners are
// (0,0), (W,0), (0,H), (W,H) — not (W-1, H-1).
//
// The alternative — pixel (i, j) IS a point sample at exactly (i, j), image
// centre at ((W-1)/2, (H-1)/2) — is equally common in vision code. Both are
// used in the wild; mixing them is what produces a mysterious half-pixel
// shift. The area convention is chosen here because it is the one a correct
// RESIZE needs (see below), and matching it now means the transform code does
// not have to be re-derived when resizing arrives.
//
// The consequence for warp(): a destination pixel's CENTRE is what gets
// transformed, and the result is an area coordinate that must be converted
// back to a source pixel index:
//
//     source_area  = M^-1 * (x + 0.5, y + 0.5)
//     source_index = source_area - 0.5
//
// Composed, that is the standard resize mapping src = (dst + 0.5)/s - 0.5 for
// a pure scale. Dropping the two halves and using M^-1 * (x, y) directly is
// the naive form, and it shifts the image by (s-1)/(2s) of a pixel on every
// resize — small, invisible once, and cumulative.
//
// The two halves live HERE, in warp(). sample_bilinear() below takes a plain
// source INDEX coordinate and knows nothing about the convention, which is
// what keeps it reusable if the convention is ever revisited.
//
// Note the identity case still lands exactly: (x + 0.5) - 0.5 == x, so the
// pixel-exact check below is unaffected.

// One source element, with the edge policy applied. This is the ONLY place a
// source index becomes a read, which is what makes the four-neighbour bounds
// problem structural rather than something to remember: no path reaches
// index() without the policy having been consulted.
//
// Clamping happens PER INDEX, never as one verdict on the whole sample. That
// distinction is the whole point: at the right-hand edge x collapses (there is
// no x+1 to weigh in) while y may still have both its neighbours present and
// must still interpolate. A single upfront gate discards the y blend along
// with the x one.
float fetch(std::span<const vc::buf_f32> px,
            const vc::vc_image& src,
            std::int64_t x,
            std::int64_t y,
            vc::channel_count ch,
            edge_policy policy) {
    const auto last_x = static_cast<std::int64_t>(src.width()) - 1;
    const auto last_y = static_cast<std::int64_t>(src.height()) - 1;

    if (x < 0 || x > last_x || y < 0 || y > last_y) {
        if (policy == edge_policy::zero) {
            return 0.0F;
        }
        x = std::clamp(x, std::int64_t{0}, last_x);
        y = std::clamp(y, std::int64_t{0}, last_y);
    }
    return px[src.meta().index(static_cast<vc::image_dim>(x),
                               static_cast<vc::image_dim>(y), ch)];
}

// Read `src` at a FRACTIONAL coordinate by blending the four surrounding
// pixels. The weights are the tent kernel evaluated at the fractional offsets,
// which is the same thing as two lerps along x followed by one along y.
//
// Note what `zero` now means: an out-of-range NEIGHBOUR contributes 0 while
// its in-range partners contribute normally, so the boundary fades over one
// pixel. The alternative — any neighbour outside means the whole sample is 0 —
// gives a hard edge but discards real data wherever the 2x2 window straddles
// the border. Per-neighbour is both the standard behaviour and the one that
// keeps the pixels it has.
float sample_bilinear(std::span<const vc::buf_f32> px,
                      const vc::vc_image& src,
                      float sx,
                      float sy,
                      vc::channel_count ch,
                      edge_policy policy) {
    // A non-finite coordinate is not an edge case, it is a broken transform
    // upstream — the policy answers "what lies outside the image", not "what
    // is garbage". Thrown rather than absorbed, for the same reason inverse()
    // refuses a singular matrix: fail where the cause is still visible. Also
    // load-bearing: every comparison with NaN is false, so a NaN would sail
    // through every check below and reach a cast that is undefined.
    if (!std::isfinite(sx) || !std::isfinite(sy)) {
        throw std::invalid_argument(
            "sample_bilinear: non-finite source coordinate");
    }

    const auto w = static_cast<float>(src.width());
    const auto h = static_cast<float>(src.height());

    // Outside this window all four neighbours are out of range, so the policy
    // already decides the answer and clamping changes nothing. It is here to
    // keep the int64 cast below in range: a legitimate but enormous coordinate
    // from a near-degenerate transform would otherwise overflow it, which is
    // undefined. Any coordinate that actually samples real pixels lies inside
    // the window and is untouched, so interior weights are exact.
    const float qx = std::clamp(sx, -1.0F, w);
    const float qy = std::clamp(sy, -1.0F, h);

    const float fx0 = std::floor(qx);
    const float fy0 = std::floor(qy);
    const auto x0 = static_cast<std::int64_t>(fx0);
    const auto y0 = static_cast<std::int64_t>(fy0);
    const float fx = qx - fx0;
    const float fy = qy - fy0;

    const float top = std::lerp(fetch(px, src, x0, y0, ch, policy),
                                fetch(px, src, x0 + 1, y0, ch, policy), fx);
    const float bot = std::lerp(fetch(px, src, x0, y0 + 1, ch, policy),
                                fetch(px, src, x0 + 1, y0 + 1, ch, policy), fx);
    return std::lerp(top, bot, fy);
}

// One-shot convenience for scattered sampling — a spot check, an assertion,
// anything that is not a loop. It hoists the span for a single sample, which
// is exactly the cost the loop form exists to avoid, so do not call this from
// inside a loop.
float sample_bilinear(const vc::vc_image& src,
                      float sx,
                      float sy,
                      vc::channel_count ch,
                      edge_policy policy) {
    float result = 0.0F;
    src.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        result = sample_bilinear(px, src, sx, sy, ch, policy);
    });
    return result;
}

// The destination geometry for a warp that must clip nothing, together with
// the transform adjusted to land inside it.
struct destination_geometry {
    vc::image_dim width;
    vc::image_dim height;
    // `m` with a translation composed in, so no output coordinate is negative.
    // Use THIS in the warp loop, not the original `m` — the size and the
    // offset are two halves of one answer and separating them is how content
    // ends up half off the canvas.
    vc::math::vc_mat3 adjusted;
};

// Size a canvas so the whole transformed image fits, and shift the transform
// to match. This is what "rotate and expand canvas" does in an editor.
//
// Only FOUR points are mapped, and that is sufficient rather than an
// approximation: an affine map sends the source rectangle to a parallelogram,
// and a parallelogram's extremes are its vertices. So the bounding box of the
// four mapped corners IS the bounding box of the whole image. (This is exactly
// where forward mapping is the right tool — four points, no pixels moved, so
// none of the holes-and-collisions problems apply.)
//
// Note that fitting makes a pure translation a no-op: translate(0.5, 0) shifts
// the bounding box by 0.5 and the fit shifts it straight back. That is correct
// rather than surprising — "fit the content" means the fit chooses the
// position, so any requested position is discarded.
destination_geometry fitted_destination(const vc::vc_image& src,
                                        const vc::math::vc_mat3& m) {
    const auto w = static_cast<float>(src.width());
    const auto h = static_cast<float>(src.height());

    // AREA convention: the image occupies [0, W] x [0, H], so these are its
    // corners. Under the point-sample convention they would be (W-1, H-1).
    const std::array<vc::math::vc_vec2, 4> corners = {{
        {.x = 0.0F, .y = 0.0F},
        {.x = w, .y = 0.0F},
        {.x = 0.0F, .y = h},
        {.x = w, .y = h},
    }};

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();

    for (const vc::math::vc_vec2& c : corners) {
        const vc::math::vc_vec2 p = vc::math::transform_point(m, c);
        // A near-singular or wildly scaled transform can produce inf/NaN, and
        // every comparison against NaN is false — so the min/max below would
        // silently keep their sentinels and the ceil() further down would
        // convert garbage to an integer, which is undefined behaviour. Caught
        // here, where the cause is still visible. ("No NaN/Inf leaking" is a
        // stated P1 quality checkpoint for exactly this toy.)
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            throw std::invalid_argument(
                "fitted_destination: the transform maps a corner to a "
                "non-finite coordinate");
        }
        min_x = std::min(min_x, p.x);
        min_y = std::min(min_y, p.y);
        max_x = std::max(max_x, p.x);
        max_y = std::max(max_y, p.y);
    }

    const float extent_x = max_x - min_x;
    const float extent_y = max_y - min_y;

    // Rejected HERE rather than left to vc_image_writer::validated(): a
    // collapsed extent means the TRANSFORM flattened the image onto a line or
    // a point, which is a different fault from "the caller asked for a
    // zero-width image", and validated()'s message would name the wrong cause.
    // Rounding up rather than clamping to 1, deliberately — a degenerate
    // transform should fail loudly, not quietly produce a one-pixel image.
    if (!(extent_x > 0.0F) || !(extent_y > 0.0F)) {
        throw std::invalid_argument(
            "fitted_destination: the transform collapses the image to zero "
            "width or height");
    }

    // ceil, so a fractional extent keeps the partly-covered last pixel rather
    // than shaving it off. Identity is unaffected: its extent is exactly W.
    return destination_geometry{
        .width = static_cast<vc::image_dim>(std::ceil(extent_x)),
        .height = static_cast<vc::image_dim>(std::ceil(extent_y)),
        .adjusted = vc::math::translate(-min_x, -min_y) * m,
    };
}

// Apply an affine transform to an image.
//
// `m` is the FORWARD transform — the one describing what you want to happen to
// the picture ("rotate 30 degrees"). It is inverted inside; callers never
// think in inverses. A singular `m` surfaces as vc::math::inverse() throwing,
// which is the right place for it: better to fail before touching a pixel than
// to fill an image with infinities.
//
// OUTPUT SIZE — see the enum above. same_as_source is what the checks in
// main() need (the rotate-+30-then--30 comparison requires matching geometry);
// fit_transform is what an editor's "rotate without cropping" does.
//
// Note that the two halves of a fit — the canvas size and the shifted matrix —
// are taken together from one destination_geometry. Using the size without the
// offset renders the wrong region of the plane.
//
// Channel count and dtype always come from the source; a geometric transform
// moves pixels, it does not reinterpret them.
vc::vc_image warp(const vc::vc_image& src,
                  const vc::math::vc_mat3& m,
                  edge_policy policy,
                  output_size sizing = output_size::same_as_source) {
    const destination_geometry geom =
        sizing == output_size::fit_transform
            ? fitted_destination(src, m)
            : destination_geometry{
                  .width = src.width(), .height = src.height(), .adjusted = m};

    vc::vc_image_writer out{geom.width, geom.height, src.channels(),
                            vc::buf_f32{0.0F}};

    // Inverted ONCE, outside the loop — the matrix does not change per pixel.
    // Note it is geom.adjusted, never the caller's `m`: on the fitted path
    // those differ by the offset that puts the content on the canvas.
    const vc::math::vc_mat3 inv = vc::math::inverse(geom.adjusted);

    // The whole loop runs inside one with_pixels<T>(), so the buffer's dtype
    // is checked ONCE per warp rather than once per element read — the same
    // split vc_image_writer makes between at<T>() and with_pixels<T>().
    src.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        for (vc::image_dim y = 0; y < out.height(); ++y) {
            for (vc::image_dim x = 0; x < out.width(); ++x) {
                // AREA convention: transform the pixel's CENTRE, then convert
                // the area coordinate back to a source index.
                const vc::math::vc_vec2 centre{
                    .x = static_cast<float>(x) + 0.5F,
                    .y = static_cast<float>(y) + 0.5F};
                const vc::math::vc_vec2 area =
                    vc::math::transform_point(inv, centre);
                const float sx = area.x - 0.5F;
                const float sy = area.y - 0.5F;

                for (vc::channel_count ch = 0; ch < out.channels(); ++ch) {
                    out.at<vc::buf_f32>(x, y, ch) =
                        sample_bilinear(px, src, sx, sy, ch, policy);
                }
            }
        }
    });
    return std::move(out).seal();
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
        ok = check(approx_equal(
                       sample_bilinear(quad, 0.0F, 0.0F, 0, edge_policy::clamp),
                       0.0F, 1e-6F),
                   "sampling at (0, 0) returns the stored pixel") &&
             ok;
        ok = check(approx_equal(
                       sample_bilinear(quad, 1.0F, 1.0F, 0, edge_policy::clamp),
                       30.0F, 1e-6F),
                   "sampling at (1, 1) returns the stored pixel") &&
             ok;
        ok = check(approx_equal(
                       sample_bilinear(quad, 0.5F, 0.5F, 0, edge_policy::clamp),
                       15.0F, 1e-5F),
                   "sampling at (0.5, 0.5) matches the hand value 15") &&
             ok;
        ok = check(approx_equal(
                       sample_bilinear(quad, 0.3F, 0.8F, 0, edge_policy::clamp),
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
        ok = check(approx_equal(sample_bilinear(edge_ramp, 4.2F, 1.5F, 0,
                                                edge_policy::clamp),
                                41.5F, 1e-4F),
                   "right edge: x collapses, y still interpolates") &&
             ok;
        // The mirror case. src(1,2) = 12, src(2,2) = 22, so at (1.5, 2.4) the
        // y-blend collapses against the bottom row and x gives 17.
        ok = check(approx_equal(sample_bilinear(edge_ramp, 1.5F, 2.4F, 0,
                                                edge_policy::clamp),
                                17.0F, 1e-4F),
                   "bottom edge: y collapses, x still interpolates") &&
             ok;

        // ---- 2. identity is pixel-exact ----
        const vc::vc_image ramp = make_ramp(5, 3, 3);
        const vc::vc_image identity_warped =
            warp(ramp, vc::math::vc_mat3::identity(), edge_policy::clamp);

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
            warp(ramp, vc::math::translate(1.0F, 0.0F), edge_policy::clamp);
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

        const vc::vc_image rotated = warp(
            photo, vc::math::rotate_about(cx, cy, deg30), edge_policy::clamp);
        const vc::vc_image restored =
            warp(rotated, vc::math::rotate_about(cx, cy, -deg30),
                 edge_policy::clamp);

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
        // instead. edge_policy::zero so the padding is visibly black rather
        // than smeared edge pixels.
        const vc::vc_image rotated_fitted =
            warp(photo, vc::math::rotate_about(cx, cy, deg30),
                 edge_policy::zero, output_size::fit_transform);
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
        const vc::vc_image downscaled =
            warp(photo, vc::math::scale_about(cx, cy, 0.25F, 0.25F),
                 edge_policy::clamp);
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
                     warp(card, vc::math::rotate_about(card_cx, card_cy, deg30),
                          edge_policy::zero),
                     png);
        writer.write(out_dir + "/03_card_rot30_fitted.png",
                     warp(card, vc::math::rotate_about(card_cx, card_cy, deg30),
                          edge_policy::zero, output_size::fit_transform),
                     png);
        writer.write(out_dir + "/03_card_downscale_quarter.png",
                     warp(card,
                          vc::math::scale_about(card_cx, card_cy, 0.25F, 0.25F),
                          edge_policy::zero),
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
