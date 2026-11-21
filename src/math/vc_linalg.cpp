// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/math/vc_linalg.h"
#include "vc/core/vc_scalar.h" // vc::float32 -- leaf header

#include <algorithm> // std::max
#include <cmath>
#include <cstddef>
#include <cstdlib>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace {

// The unit eigenvector of `a` for the eigenvalue `eigen`.
//
// (A - lambda*I) IS SINGULAR BY CONSTRUCTION -- that is what choosing an
// eigenvalue means -- so its two rows are linearly dependent and ONE row
// already fixes the direction. There is no system to solve.
//
//   [a - lambda, b         ] [x]   [0]
//   [c,          d - lambda] [y] = [0]
//
// Row 1, (a - lambda)x + b*y = 0, is satisfied by (b, lambda - a). Check it
// by substitution: (a - lambda)*b + b*(lambda - a) = 0.
// Row 2, c*x + (d - lambda)y = 0, is satisfied by (lambda - d, c), because
// c*(lambda - d) + (d - lambda)*c = 0.
//
// COMBINING THE TWO ROWS IS ALSO VALID -- the mediant of two equal ratios is
// that same ratio -- but it is fragile, and that is why this takes one row
// instead. When the two row vectors are ANTI-parallel the mediant cancels to
// (0, 0) even though both rows are perfectly good: on [[3,1],[1,3]] at
// lambda = 2, row 1 gives (1, -1) and row 2 gives (-1, 1).
//
// NEITHER ROW WORKS ALONE EITHER, and not only in the exactly-zero case:
//
//   - On a diagonal matrix at lambda = a, row 1 is (0, 0); at lambda = d,
//     row 2 is (0, 0).
//   - On [[1e4, 1],[1, 1]], lambda_major rounds to EXACTLY 1e4, so row 1
//     collapses to (1, 0) -- a residual of 1.0 against the acceptance suite's
//     bound of 0.1 -- while row 2 gives (9999, 1) with a residual of 0.
//     Both closed forms behave identically here, so this is about the choice
//     of ROW, not the choice of discriminant.
//
// So take the LONGER of the two, compared by SQUARED length to avoid a second
// square root. The longer vector is the better-conditioned one for the same
// reason it is longer: less of it was lost to cancellation.
//
// There is deliberately no symmetry check here. The only caller validates
// symmetry before it can reach this, so a check would be unreachable -- and
// an earlier version of it also carried `eigen_symmetric` in its message,
// which is week 9's "right error code, wrong function's name" defect.
vc::math::vc_vec2 get_eigen_norm(const vc::math::vc_mat2& a,
                                 vc::float32 eigen) {
    const vc::float32 row1_x = a.m[0][1];         // b
    const vc::float32 row1_y = eigen - a.m[0][0]; // lambda - a
    const vc::float32 row2_x = eigen - a.m[1][1]; // lambda - d
    const vc::float32 row2_y = a.m[1][0];         // c

    const vc::float32 len1_sq = row1_x * row1_x + row1_y * row1_y;
    const vc::float32 len2_sq = row2_x * row2_x + row2_y * row2_y;

    const bool use_row1 = len1_sq >= len2_sq;
    const vc::float32 x = use_row1 ? row1_x : row2_x;
    const vc::float32 y = use_row1 ? row1_y : row2_y;
    const vc::float32 len_sq = use_row1 ? len1_sq : len2_sq;

    // EXACTLY zero, not a tolerance. An earlier version tested this against
    // degenerate_gap_tolerance, which is a bound on an eigenvalue GAP being
    // used as a floor on a vector NORM -- the wrong tolerance on the wrong
    // quantity, and P2's recurring bug class. It also REJECTED valid input: a
    // structure tensor from a low-contrast region has a norm near 1e-6, well
    // under a 1e-5 cutoff, and there is no symmetric 2x2 matrix that lacks an
    // eigenvector.
    //
    // FOR FINITE INPUT THIS IS UNREACHABLE: (A - lambda*I) has rank 1 at a
    // simple eigenvalue, so at least one row is nonzero, and the rank-0 case
    // (A = lambda*I) is the degenerate branch the caller takes first.
    //
    // FOR NON-FINITE INPUT IT IS REACHABLE, and the three cases do not agree
    // with each other -- so the test is written `!(len_sq > 0)` rather than
    // `len_sq == 0` to catch the NaN, and the message does not guess which
    // cause it was:
    //
    //   sym(NaN, 0, 1) throws HERE. Every comparison against a NaN is false,
    //     so the asymmetry check passes, max(0, NaN) gives 0, and the gap test
    //     NaN <= NaN is false, which falls through to this helper.
    //   sym(Inf, 0, 1) does NOT throw. Inf - Inf clamps to 0, the longer row
    //     is (Inf, 0), and Inf/Inf returns a NaN eigenvector silently.
    //   sym(1, NaN, 1) does NOT throw either. It returns the standard basis
    //     and reports the directions arbitrary, ignoring the NaN entirely.
    //
    // OPEN CONTRACT QUESTION, deliberately not decided here: vc_linalg.h says
    // nothing about non-finite entries, and making this function reject them
    // would be a change to the contract rather than a fix to the body. Left
    // for the same pass that settles degenerate_gap_tolerance.
    if (!(len_sq > 0.0F)) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::get_eigen_norm: neither row of (a - lambda*I) gave a "
            "direction -- an entry is not finite, or the caller skipped its "
            "degenerate branch");
    }

    const vc::float32 length = std::sqrt(len_sq);
    return {.x = x / length, .y = y / length};
}

} // namespace

namespace vc::math {

// TODO: collapse vec2/3 and mat2/3 impl into a single template
// once the library has a template system in place

vc_vec2 operator*(const vc_mat2& a, const vc_vec2& v) {
    return vc_vec2{.x = a.m[0][0] * v.x + a.m[0][1] * v.y,
                   .y = a.m[1][0] * v.x + a.m[1][1] * v.y};
}

vc_mat2 operator*(const vc_mat2& a, const vc_mat2& b) {
    vc_mat2 result{};
    result.m[0][0] = a.m[0][0] * b.m[0][0] + a.m[0][1] * b.m[1][0];
    result.m[0][1] = a.m[0][0] * b.m[0][1] + a.m[0][1] * b.m[1][1];
    result.m[1][0] = a.m[1][0] * b.m[0][0] + a.m[1][1] * b.m[1][0];
    result.m[1][1] = a.m[1][0] * b.m[0][1] + a.m[1][1] * b.m[1][1];
    return result;
}

vc::float32 determinant(const vc_mat2& a) {
    return a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0];
}

vc::float32 trace(const vc_mat2& a) {
    return a.m[0][0] + a.m[1][1];
}

vc_eigen_2x2 eigen_symmetric(const vc_mat2& a) {
    // RELATIVE to the largest entry, as vc_linalg.h specifies, because a
    // structure tensor from a bright image has entries orders of magnitude
    // larger than one from a dim image. An ABSOLUTE cutoff is wrong in both
    // directions: at a diagonal of 1e4 it rejects matrices whose only
    // asymmetry is float32 rounding in the off-diagonal product, and at small
    // scale it accepts genuinely asymmetric ones.
    //
    // The zero matrix gives scale == 0, and 0 > 0 is false, so a flat patch is
    // accepted rather than rejected.
    const vc::float32 scale =
        std::max(std::max(std::fabs(a.m[0][0]), std::fabs(a.m[0][1])),
                 std::max(std::fabs(a.m[1][0]), std::fabs(a.m[1][1])));
    if (std::fabs(a.m[0][1] - a.m[1][0]) > asymmetry_tolerance * scale) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::eigen_symmetric: matrix is not symmetric");
    }

    const vc::float32 mean = trace(a) * 0.5F;
    const vc::float32 product = determinant(a);

    // CLAMPED AT ZERO, and the clamp can only ever discard a rounding
    // artefact. Expand the discriminant once:
    //
    //   mean^2 - det = ((a + d)/2)^2 - (a*d - b^2) = (a - d)^2 / 4 + b^2
    //
    // a sum of two squares, so for a symmetric matrix it is never negative in
    // exact arithmetic. A negative value here is therefore always rounding.
    //
    // WHERE THE NEGATIVE COMES FROM, because it is not what it looks like. On
    // (a, b, d) = (30000, 0.1, 30001) both mean^2 and det round to the same
    // stored float32, 900030016, so the true radicand is 0. Written as one
    // expression, the compiler may contract it into a fused multiply-add that
    // keeps mean^2 unrounded and subtracts the ROUNDED determinant; the -15.75
    // that comes out is just the determinant's rounding error with its sign
    // flipped, and sqrt of it is NaN. Splitting the statement is NOT a fix,
    // because -ffp-contract=fast produces the NaN either way. The clamp does
    // not depend on a flag.
    //
    // WHAT THE CLAMP DOES NOT FIX: squaring and then subtracting costs half
    // the available digits, so this form carries a relative error of about
    // 2*sqrt(eps), near 7e-4 for float32 -- roughly 70x
    // degenerate_gap_tolerance. The eigenvalues stay finite and correct to
    // that error, but a small gap can be destroyed, which is why the
    // acceptance suite REPORTS precision at scale rather than gating it. The
    // sum-of-squares form above, sqrt((a - d)^2 / 4 + b^2), has no such term
    // and needs no clamp.
    const vc::float32 radicand = mean * mean - product;
    const vc::float32 offset = std::sqrt(std::max(0.0F, radicand));

    // offset is non-negative, so these are already ordered and no sort or
    // comparison is needed to tell which is which.
    const vc::float32 eigen_major = mean + offset;
    const vc::float32 eigen_minor = mean - offset;

    // RELATIVE to the larger magnitude, as vc_linalg.h specifies, and `<=`
    // rather than `<` because the header says the gap "falls to" this.
    //
    // BOTH OF THOSE MATTER. Absolute, the test declares any small-magnitude
    // matrix directionless regardless of its conditioning: diag(1e-6, 2e-6)
    // has a gap of 1e-6, under a 1e-5 cutoff, yet its eigenvalue RATIO is 2 --
    // a clean edge whose orientation would be thrown away. And `<` would break
    // the zero matrix, which is every flat patch in an image: its scale is 0,
    // so the test reads 0 <= 0, true here and false with `<`. False would send
    // a flat patch down the eigenvector path to divide by zero.
    //
    // eigen_major - eigen_minor is 2*offset and so never negative; no abs.
    const vc::float32 eigen_scale =
        std::max(std::fabs(eigen_major), std::fabs(eigen_minor));
    const bool directions_arbitrary =
        (eigen_major - eigen_minor) <= degenerate_gap_tolerance * eigen_scale;

    if (directions_arbitrary) {
        return vc_eigen_2x2{.lambda_major = eigen_major,
                            .lambda_minor = eigen_minor,
                            .v_major = {.x = 1.0F, .y = 0.0F},
                            .v_minor = {.x = 0.0F, .y = 1.0F},
                            .directions_arbitrary = true};
    }

    const vc_vec2 v_major = get_eigen_norm(a, eigen_major);

    // PERPENDICULAR, not a second solve. A symmetric matrix has exactly
    // orthogonal eigenvectors, so rotating v_major by 90 degrees is both
    // cheaper than solving again and orthonormal by construction -- two
    // independent solves can each round away from the other.
    return vc_eigen_2x2{.lambda_major = eigen_major,
                        .lambda_minor = eigen_minor,
                        .v_major = v_major,
                        .v_minor = {.x = -v_major.y, .y = v_major.x},
                        .directions_arbitrary = false};
}

vc_mat2 inverse(const vc_mat2& a) {
    const vc::float32 det = determinant(a);
    // A tolerance, deliberately NOT `det == 0.0f`. A mathematically singular
    // matrix usually computes a determinant NEAR zero rather than at it: rows
    // (0.1, 0.2) and (0.3, 0.6) collapse the plane onto a line, yet the
    // determinant lands around 8.9e-10 because 0.1f*0.6f and 0.2f*0.3f round
    // differently. An exact comparison admits those and hands back an
    // "inverse" with entries near 1e9, which destroys whatever image the
    // transform is applied to instead of failing here, where the cause is.
    if (std::fabs(det) < singular_tolerance) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::inverse(vc_mat2): matrix is singular");
    }
    const vc::float32 inv_det = 1.0f / det;
    vc_mat2 result{};
    result.m[0][0] = a.m[1][1] * inv_det;
    result.m[0][1] = -a.m[0][1] * inv_det;
    result.m[1][0] = -a.m[1][0] * inv_det;
    result.m[1][1] = a.m[0][0] * inv_det;
    return result;
}

vc_vec3 operator*(const vc_mat3& a, const vc_vec3& v) {
    vc_vec3 result{};
    result.x = a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z;
    result.y = a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z;
    result.z = a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z;
    return result;
}

vc_mat3 operator*(const vc_mat3& a, const vc_mat3& b) {
    vc_mat3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            result.m[row][col] = a.m[row][0] * b.m[0][col] +
                                 a.m[row][1] * b.m[1][col] +
                                 a.m[row][2] * b.m[2][col];
        }
    }
    return result;
}

vc::float32 determinant(const vc_mat3& a) {
    return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) -
           a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) +
           a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
}

vc_mat3 inverse(const vc_mat3& a) {
    const vc::float32 det = determinant(a);
    // Tolerance rather than an exact zero, for the reason given in the 2x2
    // case above.
    if (std::fabs(det) < singular_tolerance) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::inverse(vc_mat3): matrix is singular");
    }
    const vc::float32 inv_det = 1.0f / det;
    vc_mat3 result{};
    result.m[0][0] = (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) * inv_det;
    result.m[0][1] = -(a.m[0][1] * a.m[2][2] - a.m[0][2] * a.m[2][1]) * inv_det;
    result.m[0][2] = (a.m[0][1] * a.m[1][2] - a.m[0][2] * a.m[1][1]) * inv_det;
    result.m[1][0] = -(a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) * inv_det;
    result.m[1][1] = (a.m[0][0] * a.m[2][2] - a.m[0][2] * a.m[2][0]) * inv_det;
    result.m[1][2] = -(a.m[0][0] * a.m[1][2] - a.m[0][2] * a.m[1][0]) * inv_det;
    result.m[2][0] = (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]) * inv_det;
    result.m[2][1] = -(a.m[0][0] * a.m[2][1] - a.m[0][1] * a.m[2][0]) * inv_det;
    result.m[2][2] = (a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0]) * inv_det;
    return result;
}

// ---- analytic geometry ----
//
// Only the arithmetic that has a caller is defined: a difference (the
// residual, v - project(v, onto)) and a scalar multiple (the projection
// itself). There is deliberately no vector operator+ — nothing needs one, and
// this surface is kept to what is used, the same reason the header offers no
// distance(), no L1 norm and no normalize().

vc_vec2 operator-(const vc_vec2& a, const vc_vec2& b) {
    return vc_vec2{.x = a.x - b.x, .y = a.y - b.y};
}

vc_vec2 operator*(const vc_vec2& v, vc::float32 s) {
    return vc_vec2{.x = v.x * s, .y = v.y * s};
}

vc_vec3 operator-(const vc_vec3& a, const vc_vec3& b) {
    return vc_vec3{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

vc_vec3 operator*(const vc_vec3& v, vc::float32 s) {
    return vc_vec3{.x = v.x * s, .y = v.y * s, .z = v.z * s};
}

vc::float32 dot(const vc_vec2& a, const vc_vec2& b) {
    return a.x * b.x + a.y * b.y;
}

vc::float32 dot(const vc_vec3& a, const vc_vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vc::float32 norm_squared(const vc_vec2& v) {
    return dot(v, v);
}

vc::float32 norm_squared(const vc_vec3& v) {
    return dot(v, v);
}

vc::float32 norm(const vc_vec2& v) {
    return std::sqrt(norm_squared(v));
}

vc::float32 norm(const vc_vec3& v) {
    return std::sqrt(norm_squared(v));
}

vc_vec2 project(const vc_vec2& v, const vc_vec2& onto) {
    const vc::float32 onto_norm_sq = norm_squared(onto);
    if (onto_norm_sq < degenerate_norm_squared_tolerance) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::project(vc_vec2): `onto` vector is degenerate");
    }
    const vc::float32 k = dot(v, onto) / onto_norm_sq;
    return onto * k;
}

vc_vec3 project(const vc_vec3& v, const vc_vec3& onto) {
    const vc::float32 onto_norm_sq = norm_squared(onto);
    if (onto_norm_sq < degenerate_norm_squared_tolerance) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::project(vc_vec3): `onto` vector is degenerate");
    }
    const vc::float32 k = dot(v, onto) / onto_norm_sq;
    return onto * k;
}

} // namespace vc::math
