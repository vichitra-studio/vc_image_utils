// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Same discipline as test_vc_linalg.cpp: every expected value is
// HAND-COMPUTABLE. A suite that only checked self-consistency — compose a
// transform with its inverse, get the identity — would pass for an
// implementation that had put the translation in the third ROW instead of the
// third column, or that rotated the wrong way. So the products below are
// pinned to specific numbers, and the transforms are asymmetric (tx != ty,
// sx != sy) so a swap cannot hide.

#include "doctest/doctest.h"

#include <cmath>
#include <cstddef>
#include <numbers>

#include "vc/core/vc_exception.h"
#include "vc/math/vc_linalg.h"
#include "vc/math/vc_transform.h"

namespace {

constexpr float tol = 1e-6f;

// pi/2 and pi/6 — the two angles below. Named rather than spelled inline so a
// reader can see at a glance that these are 90 and 30 degrees in radians.
constexpr float half_pi = std::numbers::pi_v<float> / 2.0f;
constexpr float sixth_pi = std::numbers::pi_v<float> / 6.0f;

// cos/sin of 30 degrees, to more digits than float carries. Written out rather
// than computed so the expected value does not come from the same std::cos
// call the implementation will use — a shared mistake would cancel.
constexpr float cos30 = 0.86602540f;
constexpr float sin30 = 0.5f;

vc::math::vc_vec2 vec2_of(float x, float y) {
    return vc::math::vc_vec2{.x = x, .y = y};
}

vc::math::vc_vec3 vec3_of(float x, float y, float z) {
    return vc::math::vc_vec3{.x = x, .y = y, .z = z};
}

bool close(float a, float b) {
    return std::fabs(a - b) <= tol;
}

bool close(const vc::math::vc_vec2& a, const vc::math::vc_vec2& b) {
    return close(a.x, b.x) && close(a.y, b.y);
}

bool close(const vc::math::vc_vec3& a, const vc::math::vc_vec3& b) {
    return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z);
}

bool close(const vc::math::vc_mat3& a, const vc::math::vc_mat3& b) {
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (!close(a.m[r][c], b.m[r][c])) {
                return false;
            }
        }
    }
    return true;
}

// The property that makes a matrix AFFINE rather than projective: the bottom
// row leaves the homogeneous 1 alone, so no perspective divide is ever needed.
bool has_affine_bottom_row(const vc::math::vc_mat3& m) {
    return close(m.m[2][0], 0.0f) && close(m.m[2][1], 0.0f) &&
           close(m.m[2][2], 1.0f);
}

} // namespace

TEST_CASE("vc::math::translate: the offset lives in the third column") {
    const vc::math::vc_mat3 t = vc::math::translate(5.0f, 3.0f);

    // Pinned entry by entry, not just by behaviour: an implementation that put
    // the offset in the third ROW would still move points, just wrongly, and a
    // behaviour-only check on a symmetric example would not notice.
    CHECK(close(t.m[0][2], 5.0f));
    CHECK(close(t.m[1][2], 3.0f));
    CHECK(close(t.m[0][0], 1.0f));
    CHECK(close(t.m[1][1], 1.0f));
    CHECK(close(t.m[0][1], 0.0f));
    CHECK(close(t.m[1][0], 0.0f));
    CHECK(has_affine_bottom_row(t));

    // (2, 7) + (5, 3) = (7, 10)
    CHECK(close(t * vec3_of(2.0f, 7.0f, 1.0f), vec3_of(7.0f, 10.0f, 1.0f)));
}

TEST_CASE("vc::math::scale: scales about the origin") {
    const vc::math::vc_mat3 s = vc::math::scale(2.0f, 3.0f);

    CHECK(close(s.m[0][0], 2.0f));
    CHECK(close(s.m[1][1], 3.0f));
    CHECK(close(s.m[0][2], 0.0f)); // no translation component
    CHECK(close(s.m[1][2], 0.0f));
    CHECK(has_affine_bottom_row(s));

    // (3, 4) scaled by (2, 3) = (6, 12). sx != sy so a swap cannot hide.
    CHECK(close(s * vec3_of(3.0f, 4.0f, 1.0f), vec3_of(6.0f, 12.0f, 1.0f)));

    // The origin is fixed by any scaling about the origin.
    CHECK(close(s * vec3_of(0.0f, 0.0f, 1.0f), vec3_of(0.0f, 0.0f, 1.0f)));
}

TEST_CASE("vc::math::rotate: the columns are where the basis vectors land") {
    // 90 degrees: (1, 0) -> (0, 1) and (0, 1) -> (-1, 0). This is the
    // y-axis-UP convention; on screen, with y pointing down, it reads as a
    // clockwise turn (see the header's direction note).
    const vc::math::vc_mat3 r90 = vc::math::rotate(half_pi);
    CHECK(close(r90 * vec3_of(1.0f, 0.0f, 1.0f), vec3_of(0.0f, 1.0f, 1.0f)));
    CHECK(close(r90 * vec3_of(0.0f, 1.0f, 1.0f), vec3_of(-1.0f, 0.0f, 1.0f)));
    CHECK(has_affine_bottom_row(r90));

    // 30 degrees: (1, 0) -> (cos 30, sin 30). The asymmetric angle is what
    // catches a sign error that 90 degrees would not.
    const vc::math::vc_mat3 r30 = vc::math::rotate(sixth_pi);
    CHECK(close(r30 * vec3_of(1.0f, 0.0f, 1.0f), vec3_of(cos30, sin30, 1.0f)));
    CHECK(close(r30 * vec3_of(0.0f, 1.0f, 1.0f), vec3_of(-sin30, cos30, 1.0f)));

    // A rotation never translates.
    CHECK(close(r30.m[0][2], 0.0f));
    CHECK(close(r30.m[1][2], 0.0f));

    // Zero rotation is the identity — cheap, and it catches a swapped cos/sin
    // that the cases above could survive if both were wrong the same way.
    CHECK(close(vc::math::rotate(0.0f), vc::math::vc_mat3::identity()));
}

TEST_CASE("vc::math::rotate_about: the centre is a fixed point") {
    constexpr float cx = 12.0f;
    constexpr float cy = 5.0f; // cx != cy so an x/y swap cannot hide

    const vc::math::vc_mat3 m = vc::math::rotate_about(cx, cy, sixth_pi);

    // THE test for this function. Getting the sandwich order backwards
    // produces a matrix that still rotates and still looks plausible; only the
    // fixed point distinguishes them.
    CHECK(close(m * vec3_of(cx, cy, 1.0f), vec3_of(cx, cy, 1.0f)));
    CHECK(has_affine_bottom_row(m));

    // And it really is the sandwich, not something that merely fixes (cx, cy).
    const vc::math::vc_mat3 composed = vc::math::translate(cx, cy) *
                                       vc::math::rotate(sixth_pi) *
                                       vc::math::translate(-cx, -cy);
    CHECK(close(m, composed));

    // Rotating about the origin is the plain rotation.
    CHECK(close(vc::math::rotate_about(0.0f, 0.0f, sixth_pi),
                vc::math::rotate(sixth_pi)));
}

TEST_CASE("vc::math::scale_about: the centre is a fixed point") {
    constexpr float cx = 12.0f;
    constexpr float cy = 5.0f;

    const vc::math::vc_mat3 m = vc::math::scale_about(cx, cy, 2.0f, 3.0f);

    CHECK(close(m * vec3_of(cx, cy, 1.0f), vec3_of(cx, cy, 1.0f)));
    CHECK(has_affine_bottom_row(m));

    const vc::math::vc_mat3 composed = vc::math::translate(cx, cy) *
                                       vc::math::scale(2.0f, 3.0f) *
                                       vc::math::translate(-cx, -cy);
    CHECK(close(m, composed));
}

TEST_CASE("composing two translations adds their offsets") {
    // The whole reason for the homogeneous embedding: composition is ordinary
    // matrix multiplication, with no bespoke rule for the offset.
    const vc::math::vc_mat3 first = vc::math::translate(5.0f, 3.0f);
    const vc::math::vc_mat3 second = vc::math::translate(2.0f, 1.0f);

    // `a * b` applies b FIRST (vc_linalg's convention), so this is
    // "translate by (5,3), then by (2,1)" = (7, 4).
    CHECK(close(second * first, vc::math::translate(7.0f, 4.0f)));

    // Translations commute; rotations and scales in general do not, which is
    // why the ordering convention has to be stated rather than inferred.
    CHECK(close(second * first, first * second));
}

TEST_CASE("closed-form inverses agree with the general 3x3 inverse") {
    // Each of these transforms has an inverse you can write down by
    // inspection. Checking them against vc_linalg's general inverse() tests
    // BOTH sides at once: the builders here, and the determinant/adjugate
    // arithmetic there, against answers derived independently of either.
    const vc::math::vc_mat3 r = vc::math::rotate(sixth_pi);
    CHECK(close(vc::math::inverse(r), vc::math::rotate(-sixth_pi)));

    const vc::math::vc_mat3 s = vc::math::scale(2.0f, 4.0f);
    CHECK(close(vc::math::inverse(s), vc::math::scale(0.5f, 0.25f)));

    const vc::math::vc_mat3 t = vc::math::translate(5.0f, 3.0f);
    CHECK(close(vc::math::inverse(t), vc::math::translate(-5.0f, -3.0f)));

    // A rotation is orthogonal, so its inverse is also its transpose.
    const vc::math::vc_mat3 r_inv = vc::math::inverse(r);
    CHECK(close(r_inv.m[0][1], r.m[1][0]));
    CHECK(close(r_inv.m[1][0], r.m[0][1]));
}

TEST_CASE("a degenerate scale is singular and has no inverse") {
    // Constructed OUTSIDE the CHECK_THROWS_AS on purpose: while scale() is
    // still a throwing stub, putting it inside the macro would catch the
    // stub's own exception and report a false pass. Built here, the stub makes
    // the whole case throw — visibly RED — and once scale() is implemented
    // this checks the thing it is meant to.
    const vc::math::vc_mat3 flat = vc::math::scale(0.0f, 1.0f);

    // Collapsing the plane onto a line is not invertible. The rejection lives
    // in vc_linalg's inverse() (singular_tolerance), deliberately not
    // duplicated in the builder — see the header.
    CHECK_THROWS_AS(static_cast<void>(vc::math::inverse(flat)),
                    vc::vc_exception);
}

TEST_CASE(
    "vc::math::transform_point maps a 2-D point through an affine matrix") {
    const vc::math::vc_mat3 t = vc::math::translate(5.0f, 3.0f);
    CHECK(close(vc::math::transform_point(t, vec2_of(2.0f, 7.0f)),
                vec2_of(7.0f, 10.0f)));

    // It must agree with doing the embedding by hand — this is the function's
    // whole contract, and a transposed multiply inside it would show up here
    // even though the translation case above would still pass.
    const vc::math::vc_mat3 m = vc::math::rotate_about(12.0f, 5.0f, sixth_pi);
    const vc::math::vc_vec2 p = vec2_of(3.0f, 9.0f);
    const vc::math::vc_vec3 by_hand = m * vec3_of(p.x, p.y, 1.0f);
    CHECK(
        close(vc::math::transform_point(m, p), vec2_of(by_hand.x, by_hand.y)));
}
