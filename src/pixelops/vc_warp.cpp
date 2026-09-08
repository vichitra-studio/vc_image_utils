// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_warp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/math/vc_transform.h"

namespace vc::pixelops {
namespace {

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
            vc_edge_policy policy) {
    const auto last_x = static_cast<std::int64_t>(src.width()) - 1;
    const auto last_y = static_cast<std::int64_t>(src.height()) - 1;

    if (x < 0 || x > last_x || y < 0 || y > last_y) {
        if (policy == vc_edge_policy::zero) {
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
                      vc_edge_policy policy) {
    // A non-finite coordinate is not an edge case, it is a broken transform
    // upstream — the policy answers "what lies outside the image", not "what
    // is garbage". Thrown rather than absorbed, for the same reason inverse()
    // refuses a singular matrix: fail where the cause is still visible. Also
    // load-bearing: every comparison with NaN is false, so a NaN would sail
    // through every check below and reach a cast that is undefined.
    if (!std::isfinite(sx) || !std::isfinite(sy)) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::pixelops::sample_bilinear: non-finite source coordinate");
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

} // namespace

// One-shot convenience for scattered sampling — a spot check, an assertion,
// anything that is not a loop. It hoists the span for a single sample, which
// is exactly the cost the loop form exists to avoid, so do not call this from
// inside a loop.
float sample_bilinear(const vc::vc_image& src,
                      float sx,
                      float sy,
                      vc::channel_count ch,
                      vc_edge_policy policy) {
    float result = 0.0F;
    src.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        result = sample_bilinear(px, src, sx, sy, ch, policy);
    });
    return result;
}

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
vc_destination_geometry fitted_destination(const vc::vc_image& src,
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
            throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                                   "vc::pixelops::fitted_destination: the "
                                   "transform maps a corner to a "
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
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc::pixelops::fitted_destination: the "
                               "transform collapses the image to zero "
                               "width or height");
    }

    // ceil, so a fractional extent keeps the partly-covered last pixel rather
    // than shaving it off. Identity is unaffected: its extent is exactly W.
    return vc_destination_geometry{
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
// OUTPUT SIZE — see vc_output_size. same_as_source preserves geometry, which
// is what a rotate-then-unrotate comparison needs (it compares against the
// original, so the two must match); fit_transform is what an editor's "rotate
// without cropping" does.
//
// Note that the two halves of a fit — the canvas size and the shifted matrix —
// are taken together from one vc_destination_geometry. Using the size without the
// offset renders the wrong region of the plane.
//
// Channel count and dtype always come from the source; a geometric transform
// moves pixels, it does not reinterpret them.
vc::vc_image warp(const vc::vc_image& src,
                  const vc::math::vc_mat3& m,
                  vc_edge_policy policy,
                  vc_output_size sizing) {
    const vc_destination_geometry geom =
        sizing == vc_output_size::fit_transform
            ? fitted_destination(src, m)
            : vc_destination_geometry{
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

} // namespace vc::pixelops
