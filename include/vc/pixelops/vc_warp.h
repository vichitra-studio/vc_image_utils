// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>

#include "vc/core/vc_image.h"
#include "vc/core/vc_types.h"
#include "vc/math/vc_linalg.h"

namespace vc::pixelops {

// Applying a 2-D affine transform to an image, by inverse mapping.
//
// This is a DOMAIN operation, and the distinction is worth stating because it
// is the one the whole file rests on:
//
//     g(x) = h(f(x))     h acts on the VALUE     grayscale, brightness, gamma
//     g(x) = f(h(x))     h acts on the POSITION  translate, scale, rotate
//
// Look at where h sits in the second form. To produce the output at x you
// evaluate h(x) and read the SOURCE there -- so h maps DESTINATION coordinates
// back to source ones. h is the INVERSE of the transform you asked for, and
// that is not an implementation trick, it is what the notation says.
//
// ---- Why inverse mapping ----
//
// The obvious alternative is to walk source pixels and write each one where it
// lands. That fails three ways. A transform does not send integers to
// integers, so after rounding some destination pixels are never written at all
// (holes -- visible as speckle). Scaling down, many source pixels round to the
// same destination and silently overwrite each other. And interpolating
// properly would mean splatting one value across several destination pixels
// with weights, accumulating a weight sum per destination and normalising at
// the end -- two buffers and two passes.
//
// Walking DESTINATION pixels instead visits each one exactly once, by
// construction, so holes and collisions are not merely unlikely but
// impossible. The fractional coordinate moves to the READ side, where four
// known neighbours make interpolation well defined.
//
// The asymmetry in one line: fractional coordinates are easy to read from and
// hard to write to.
//
// ---- Two independent stages ----
//
//     the matrix    coordinate -> coordinate. Pure geometry, never sees a pixel.
//     the sampler   coordinate -> value.      Never learns which transform produced it.
//
// That separation is why ONE sampler serves translate, scale, rotate and any
// affine composition of them, and why swapping bilinear for bicubic would
// improve every transform at once. It is also, precisely, why downscaling
// ALIASES here: the sampler is never told the scale factor, so it cannot widen
// its footprint to match. Correct downscaling needs a prefilter sized to the
// factor, which needs convolution -- P2. Aliasing cannot be removed after the
// fact, because two different scenes can produce byte-identical samples.
//
// ---- PIXEL CENTRE CONVENTION: THE AREA CONVENTION ----
//
// Pixel (i, j) covers the square [i, i+1) x [j, j+1) and its CENTRE sits at
// (i + 0.5, j + 0.5). The image occupies [0, W] x [0, H], so its centre is
// (W/2, H/2) and its corners are (0,0), (W,0), (0,H), (W,H) -- not (W-1, H-1).
//
// The alternative -- pixel (i, j) IS a point sample at exactly (i, j), image
// centre at ((W-1)/2, (H-1)/2) -- is equally common in vision code. Both are
// used in the wild; mixing them is what produces a mysterious half-pixel
// shift. The area convention is chosen because it is the one a correct RESIZE
// needs, so the transform code will not have to be re-derived when resizing
// arrives.
//
// The consequence for warp(): a destination pixel's CENTRE is transformed, and
// the result is an area coordinate converted back to a source pixel index:
//
//     source_area  = M^-1 * (x + 0.5, y + 0.5)
//     source_index = source_area - 0.5
//
// Composed, that is the standard resize mapping src = (dst + 0.5)/s - 0.5 for
// a pure scale. Dropping the two halves shifts the image by (s-1)/(2s) of a
// pixel on every resize -- small, invisible once, and cumulative. The naive
// form is also ASYMMETRIC: on a 100->50 downscale it samples 0..98 and never
// reads the last source pixel.
//
// Those two halves live in warp(). sample_bilinear() below takes a plain
// source INDEX coordinate and knows nothing about the convention, which is
// what keeps it reusable if the convention is ever revisited. Note the
// identity case still lands exactly: (x + 0.5) - 0.5 == x.

// What to do when the inverse-mapped coordinate falls outside the source.
// There is no correct answer, only a decided one -- so it is a parameter
// rather than a hidden constant.
//
// The policy is applied PER NEIGHBOUR, not as one verdict on the whole sample.
// That distinction is load-bearing: at the right-hand edge x collapses (there
// is no x+1 to weigh in) while y may still have both its neighbours present
// and must still interpolate. A single upfront bounds gate discards the y
// blend along with the x one, which silently point-samples the entire last row
// and column.
enum class vc_edge_policy : std::uint8_t {
    clamp, // clamp the coordinate to the image, replicating the edge pixel
    zero,  // an out-of-range NEIGHBOUR contributes 0, so the border fades over
           // one pixel. The alternative -- any neighbour outside means the
           // whole sample is 0 -- gives a hard edge but discards real data
           // wherever the 2x2 window straddles the border.
};

// How big the output canvas is.
enum class vc_output_size : std::uint8_t {
    same_as_source, // geometry preserved; anything transformed outside is clipped
    fit_transform,  // canvas grown so nothing is clipped -- see fitted_destination
};

// The destination geometry for a warp that must clip nothing, together with
// the transform adjusted to land inside it.
struct vc_destination_geometry {
    vc::image_dim width;
    vc::image_dim height;
    // `m` with a translation composed in, so no output coordinate is negative.
    // Use THIS in the warp loop, not the original `m` -- the size and the
    // offset are two halves of one answer, and separating them renders the
    // wrong region of the plane onto a correctly-sized canvas.
    vc::math::vc_mat3 adjusted;
};

// Size a canvas so the whole transformed image fits, and shift the transform
// to match. This is what "rotate and expand canvas" does in an editor.
//
// Only FOUR points are mapped, and that is sufficient rather than an
// approximation: an affine map sends the source rectangle to a parallelogram,
// and a parallelogram's extremes are its vertices. So the bounding box of the
// four mapped corners IS the bounding box of the whole image. (This is exactly
// where FORWARD mapping is the right tool -- four points, no pixels moved, so
// none of the holes-and-collisions problems apply.)
//
// Fitting makes a pure translation a no-op: translate(0.5, 0) shifts the
// bounding box by 0.5 and the fit shifts it straight back. Correct rather than
// surprising -- "fit the content" means the fit chooses the position, so any
// requested position is discarded.
//
// Throws vc_exception(invalid_argument) if the transform maps a corner to a
// non-finite coordinate, or collapses the image to zero width or height.
[[nodiscard]] vc_destination_geometry
fitted_destination(const vc::vc_image& src, const vc::math::vc_mat3& m);

// Read `src` at a FRACTIONAL coordinate by blending the four surrounding
// pixels. The weights are the tent kernel evaluated at the fractional offsets,
// which is the same thing as two lerps along x followed by one along y.
//
// One-shot form, for scattered sampling -- a spot check, an assertion.
// It hoists the pixel span for a single sample, which is exactly the cost the
// internal loop form exists to avoid, so do not call it from inside a loop;
// call warp() instead.
//
// Throws vc_exception(invalid_argument) on a non-finite coordinate. That is
// not an edge case the policy should absorb -- the policy answers "what lies
// outside the image", not "what is garbage" -- and it is load-bearing rather
// than defensive: every comparison with NaN is false, so a NaN would pass
// every bounds check and reach a cast that is undefined.
[[nodiscard]] float sample_bilinear(const vc::vc_image& src,
                                    float sx,
                                    float sy,
                                    vc::channel_count ch,
                                    vc_edge_policy policy);

// Apply an affine transform to an image.
//
// `m` is the FORWARD transform -- the one describing what you want to happen
// to the picture ("rotate 30 degrees"). It is inverted inside; callers never
// think in inverses. A singular `m` surfaces as vc::math::inverse() throwing,
// which is the right place for it: better to fail before touching a pixel than
// to fill an image with infinities.
//
// same_as_source clips whatever leaves the frame. fit_transform grows the
// canvas so nothing is clipped, taking BOTH halves of the answer from one
// vc_destination_geometry -- using the size without the offset renders the
// wrong region.
//
// Channel count and dtype always come from the source; a geometric transform
// moves pixels, it does not reinterpret them. Input must be f32.
[[nodiscard]] vc::vc_image
warp(const vc::vc_image& src,
     const vc::math::vc_mat3& m,
     vc_edge_policy policy,
     vc_output_size sizing = vc_output_size::same_as_source);

} // namespace vc::pixelops
