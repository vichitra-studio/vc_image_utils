// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/math/vc_linalg.h"

namespace vc::math {

// Builders for the 2-D AFFINE transforms an image warp needs, each returning a
// vc_mat3 in homogeneous coordinates.
//
// ---- Why 3x3 for a 2-D transform ----
//
// A 2x2 matrix applied to (x, y) can only ever produce sums of multiples of x
// and y — there is no slot for a constant, which is the same fact as
// "phi(0) = 0 for every linear map". Translation needs that constant, so no
// 2x2 can express it.
//
// The fix is to give the matrix an input that is always 1: represent the point
// (x, y) as (x, y, 1), and the translation becomes an ordinary coefficient in
// the third COLUMN:
//
//     [ a  b  tx ] [ x ]   [ a*x + b*y + tx ]
//     [ c  d  ty ] [ y ] = [ c*x + d*y + ty ]
//     [ 0  0   1 ] [ 1 ]   [       1        ]
//
// The bottom row [0 0 1] exists to keep that 1 a 1, so the result is again of
// the form (x', y', 1) and can be fed straight into another matrix. Affine in
// 2 dimensions is linear in 3.
//
// The payoff is composition: "rotate about the image centre" is
// translate(c) * rotate(t) * translate(-c) — three matrices multiplied ONCE
// into a single matrix that is then applied per pixel. Without the embedding,
// each transform would be an (A, t) pair composed by a bespoke rule.
//
// PROJECTIVE transforms (homographies) are deliberately absent. They have a
// non-trivial bottom row, produce w != 1, and require a perspective divide;
// every builder here produces w == 1 by construction. That is a genuinely
// different object and the point at which this header would need revisiting.
//
// ---- Conventions ----
//
// ANGLES ARE RADIANS. There is no degree-taking overload: two spellings of an
// angle is exactly how a factor of 57.3 gets into a transform unnoticed.
//
// ROTATION DIRECTION. rotate() is the standard counter-clockwise rotation for
// a coordinate system whose y axis points UP. Image coordinates put y DOWN, so
// the same matrix appears to rotate CLOCKWISE on screen. Nothing is wrong when
// that happens — it is the y axis, not the matrix. Stated here because it is
// the usual reason a hand-traced corner disagrees with the code.
//
// COMPOSITION ORDER is vc_linalg's: `a * b` applies b FIRST. Products read
// right to left, matching the order they reach a column vector on the right.
//
// DEGENERATE INPUTS ARE NOT REJECTED HERE. scale(0.0F, 1.0F) is a perfectly
// constructible matrix; it simply has no inverse, and inverse() already
// refuses it against singular_tolerance (vc_linalg.h). Re-checking here would
// mean a second threshold that could disagree with that one — so the failure
// is left at the single place that already owns it, which is also the place a
// warp actually needs it (an inverse-mapped warp inverts the matrix before it
// touches a pixel).

// The translation that moves every point by (tx, ty).
[[nodiscard]] vc_mat3 translate(float tx, float ty);

// Scaling about the ORIGIN — for an image, its top-left corner, which is
// rarely what a caller means. See scale_about().
[[nodiscard]] vc_mat3 scale(float sx, float sy);

// Rotation about the ORIGIN by `radians` (see the direction note above). For
// an image this swings almost all content out of frame, which looks like a bug
// and is not — see rotate_about().
[[nodiscard]] vc_mat3 rotate(float radians);

// Rotation about the point (cx, cy): translate that point to the origin,
// rotate, translate it back.
//
//     translate(cx, cy) * rotate(radians) * translate(-cx, -cy)
//
// The defining property, and the test worth writing: (cx, cy) is a FIXED
// POINT — it maps to itself. Getting the composition order backwards still
// produces a plausible-looking matrix, and that check is what catches it.
[[nodiscard]] vc_mat3 rotate_about(float cx, float cy, float radians);

// Scaling about (cx, cy), by the same sandwich, with the same fixed-point
// property.
[[nodiscard]] vc_mat3 scale_about(float cx, float cy, float sx, float sy);

// Apply an affine transform to a 2-D point: embed as (x, y, 1), multiply, and
// drop the third component.
//
// PRECONDITION: `m` is affine — its bottom row is [0 0 1], so the result's w
// is 1 and dropping it is exact. Every builder above satisfies this. There is
// deliberately NO perspective divide: a hand-built projective matrix passed
// here would silently return the un-divided (x, y), and adding the divide
// "just in case" would be inventing support for a transform family this
// header does not otherwise provide. If homographies arrive, they arrive with
// their own point-mapping function and their own tests.
[[nodiscard]] vc_vec2 transform_point(const vc_mat3& m, const vc_vec2& p);

} // namespace vc::math
