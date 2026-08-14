// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Every expected value here is HAND-COMPUTABLE, on purpose. A test that only
// checked self-consistency (multiply by the inverse, get the identity) would
// pass for an implementation that had rows and columns swapped throughout —
// the same class of blind spot the index() sweep in test_vc_image.cpp had. So
// the products below are pinned to specific numbers worked out on paper, and
// the matrices are deliberately NON-SYMMETRIC so a transpose cannot hide.

#include "doctest/doctest.h"

#include <cmath>
#include <cstddef>

#include "vc/core/vc_exception.h"
#include "vc/math/vc_linalg.h"

namespace {

// Readability shims. The nested-brace initialisation std::array requires is
// noise in a test whose subject is the arithmetic, not the aggregate syntax.
vc::math::vc_mat2 mat2_of(float a, float b, float c, float d) {
    return vc::math::vc_mat2{{{{{a, b}}, {{c, d}}}}};
}

vc::math::vc_mat3 mat3_of(float a,
                          float b,
                          float c,
                          float d,
                          float e,
                          float f,
                          float g,
                          float h,
                          float i) {
    return vc::math::vc_mat3{{{{{a, b, c}}, {{d, e, f}}, {{g, h, i}}}}};
}

vc::math::vc_vec2 vec2_of(float x, float y) {
    return vc::math::vc_vec2{.x = x, .y = y};
}

vc::math::vc_vec3 vec3_of(float x, float y, float z) {
    return vc::math::vc_vec3{.x = x, .y = y, .z = z};
}

constexpr float tol = 1e-6f;

bool close(float a, float b) {
    return std::fabs(a - b) <= tol;
}

bool close(const vc::math::vc_vec2& a, const vc::math::vc_vec2& b) {
    return close(a.x, b.x) && close(a.y, b.y);
}

bool close(const vc::math::vc_vec3& a, const vc::math::vc_vec3& b) {
    return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z);
}

bool close(const vc::math::vc_mat2& a, const vc::math::vc_mat2& b) {
    for (std::size_t r = 0; r < 2; ++r) {
        for (std::size_t c = 0; c < 2; ++c) {
            if (!close(a.m[r][c], b.m[r][c])) {
                return false;
            }
        }
    }
    return true;
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

// Shared fixtures, all hand-checked:
//   A   invertible, non-symmetric, determinant 1 (so its inverse has tidy
//       integer entries and a wrong 1/det cannot hide behind rounding)
//   R90 a quarter turn counter-clockwise — the columns are where (1,0) and
//       (0,1) land, which is the definition, not a formula to memorise
const vc::math::vc_mat2 A = mat2_of(2.0f, 1.0f, 1.0f, 1.0f);
const vc::math::vc_mat2 R90 = mat2_of(0.0f, -1.0f, 1.0f, 0.0f);

} // namespace

// ---------------------------------------------------------------- 2x2 ----

TEST_CASE("vc_mat2: applying a map to a vector matches the hand product") {
    // [2 1][1]   [2*1 + 1*2]   [4]
    // [1 1][2] = [1*1 + 1*2] = [3]
    const auto v = vec2_of(1.0f, 2.0f);
    CHECK(close(A * v, vec2_of(4.0f, 3.0f)));
}

TEST_CASE("vc_mat2: composition matches the hand product") {
    // A * R90, entry [i][j] = row i of A dotted with column j of R90:
    //   [2 1][ 0 -1]   [ 1 -2]
    //   [1 1][ 1  0] = [ 1 -1]
    CHECK(close(A * R90, mat2_of(1.0f, -2.0f, 1.0f, -1.0f)));

    // Order matters — this is the same product the other way round, and it is
    // a DIFFERENT matrix. An implementation that swapped the row/column
    // indices would compute this one when asked for the previous one.
    CHECK(close(R90 * A, mat2_of(-1.0f, -1.0f, 2.0f, 1.0f)));
}

TEST_CASE("vc_mat2: a quarter turn composed with itself is a half turn") {
    // Two 90-degree rotations make 180 degrees, which negates every vector.
    CHECK(close(R90 * R90, mat2_of(-1.0f, 0.0f, 0.0f, -1.0f)));

    const auto v = vec2_of(1.0f, 2.0f);
    CHECK(close((R90 * R90) * v, vec2_of(-1.0f, -2.0f)));
}

TEST_CASE("vc_mat2: determinant is the signed area scale") {
    CHECK(close(vc::math::determinant(A), 1.0f));   // 2*1 - 1*1
    CHECK(close(vc::math::determinant(R90), 1.0f)); // a rotation preserves area

    // Second row is twice the first: the plane collapses onto a line.
    CHECK(close(vc::math::determinant(mat2_of(1.0f, 2.0f, 2.0f, 4.0f)), 0.0f));
}

TEST_CASE("vc_mat2: inverse matches the closed form and undoes the map") {
    // det(A) = 1, so inverse(A) = [[d, -b], [-c, a]] = [[1, -1], [-1, 2]].
    CHECK(close(vc::math::inverse(A), mat2_of(1.0f, -1.0f, -1.0f, 2.0f)));

    // The acceptance criterion: A * A^-1 == I to within 1e-6, both orders.
    CHECK(close(A * vc::math::inverse(A), vc::math::vc_mat2::identity()));
    CHECK(close(vc::math::inverse(A) * A, vc::math::vc_mat2::identity()));

    // Undoing a rotation is rotating back.
    CHECK(close(vc::math::inverse(R90), mat2_of(0.0f, 1.0f, -1.0f, 0.0f)));
}

TEST_CASE("vc_mat2: inverse throws on a singular matrix, and only then") {
    // Fails loudly rather than returning a matrix of infinities that would
    // silently destroy whatever image it was applied to.
    CHECK_THROWS_AS((void)vc::math::inverse(mat2_of(1.0f, 2.0f, 2.0f, 4.0f)),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)vc::math::inverse(mat2_of(0.0f, 0.0f, 0.0f, 0.0f)),
                    vc::vc_exception);

    // Singular, with entries that are NOT exactly representable in binary
    // floating point. Row 1 is exactly 3x row 0, so the matrix collapses the
    // plane onto a line — but the COMPUTED determinant is about 8.9e-10, not
    // zero, because 0.1f*0.6f and 0.2f*0.3f round differently.
    //
    // The two cases above use integer-valued entries, where the products are
    // exact and the determinant really does come out as 0.0f. That made them
    // too kind: they pass for a guard that tests `det == 0.0f`, which is
    // exactly the guard that fails here and hands back an inverse whose
    // entries are around 1e9.
    CHECK_THROWS_AS((void)vc::math::inverse(mat2_of(0.1f, 0.2f, 0.3f, 0.6f)),
                    vc::vc_exception);

    // The other half, and the reason this case is not vacuous: an invertible
    // matrix must NOT throw. Without this line the test passes for any
    // implementation that throws unconditionally — including the shell it is
    // meant to be red against.
    CHECK_NOTHROW((void)vc::math::inverse(A));
}

TEST_CASE("vc_mat2: composition is associative") {
    const vc::math::vc_mat2 C = mat2_of(1.0f, 2.0f, 0.0f, 1.0f); // a shear
    CHECK(close((A * R90) * C, A * (R90 * C)));
}

TEST_CASE("vc_mat2: identity is the unit of composition") {
    const auto I = vc::math::vc_mat2::identity();
    const auto v = vec2_of(1.0f, 2.0f);

    // Pin identity()'s own entries first. Every other case in this file uses
    // it as the REFERENCE value, so a wrong identity() would fail them all
    // with messages pointing at whichever function was under test instead.
    CHECK(close(I, mat2_of(1.0f, 0.0f, 0.0f, 1.0f)));

    CHECK(close(I * A, A));
    CHECK(close(A * I, A));
    CHECK(close(I * v, v));
}

// ---------------------------------------------------------------- 3x3 ----

namespace {

// Non-symmetric and invertible: determinant -3, worked out by cofactor
// expansion along the first row —
//   1*(5*10 - 6*8) - 2*(4*10 - 6*7) + 3*(4*8 - 5*7)
// = 1*2 - 2*(-2) + 3*(-3) = 2 + 4 - 9 = -3
const vc::math::vc_mat3 M =
    mat3_of(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f);

} // namespace

TEST_CASE("vc_mat3: applying a map to a vector matches the hand product") {
    // Picking v = (1, 0, 1) selects column 0 plus column 2, so a
    // column-ordering error shows up as a wrong answer rather than a
    // coincidence.
    const auto v = vec3_of(1.0f, 0.0f, 1.0f);
    CHECK(close(M * v, vec3_of(4.0f, 10.0f, 17.0f)));
}

TEST_CASE("vc_mat3: determinant matches the hand-computed value") {
    CHECK(close(vc::math::determinant(M), -3.0f));

    // Row 1 is twice row 0 — singular.
    CHECK(close(vc::math::determinant(mat3_of(1.0f, 2.0f, 3.0f, 2.0f, 4.0f,
                                              6.0f, 7.0f, 8.0f, 9.0f)),
                0.0f));
}

TEST_CASE("vc_mat3: inverse round-trips to the identity") {
    const auto I = vc::math::vc_mat3::identity();

    // Same reasoning as the 2x2 identity case: pin the reference itself.
    CHECK(close(I,
                mat3_of(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f)));

    CHECK(close(M * vc::math::inverse(M), I));
    CHECK(close(vc::math::inverse(M) * M, I));

    // Applying the map and then its inverse returns the original point. M is
    // non-symmetric, so a missing transpose in the adjugate fails here.
    const auto v = vec3_of(1.0f, 0.0f, 1.0f);
    CHECK(close(vc::math::inverse(M) * (M * v), v));
}

TEST_CASE("vc_mat3: inverse throws on a singular matrix, and only then") {
    CHECK_THROWS_AS((void)vc::math::inverse(mat3_of(
                        1.0f, 2.0f, 3.0f, 2.0f, 4.0f, 6.0f, 7.0f, 8.0f, 9.0f)),
                    vc::vc_exception);

    // Same trap as the 2x2 case: row 1 is exactly 3x row 0, but the entries
    // are not exactly representable, so the computed determinant is a small
    // non-zero value rather than 0.
    CHECK_THROWS_AS((void)vc::math::inverse(mat3_of(
                        0.1f, 0.2f, 0.3f, 0.3f, 0.6f, 0.9f, 1.0f, 2.0f, 4.0f)),
                    vc::vc_exception);

    // As with the 2x2 case: pin that an invertible matrix does not throw, so
    // the case cannot be satisfied by throwing always.
    CHECK_NOTHROW((void)vc::math::inverse(M));
}

TEST_CASE("vc_mat3: carries a 2-D translation in homogeneous coordinates") {
    // The reason mat3 exists here at all. Translation is NOT linear — it
    // moves the origin — so no 2x2 can express it. Embedding the plane at
    // z = 1 and putting the offset in the third column makes it a matrix
    // product like any other.
    const vc::math::vc_mat3 T =
        mat3_of(1.0f, 0.0f, 5.0f, 0.0f, 1.0f, -3.0f, 0.0f, 0.0f, 1.0f);

    const auto p = vec3_of(2.0f, 7.0f, 1.0f); // the point (2, 7)
    CHECK(close(T * p, vec3_of(7.0f, 4.0f, 1.0f)));

    // A DIRECTION, not a point: w = 0, so the translation must NOT apply — a
    // displacement has no position to move. This is the case that separates
    // "translation in the third COLUMN" from "translation in the third ROW";
    // both conventions move a point, only the correct one leaves a direction
    // alone. It matters at S5, where warp maps corner offsets as well as
    // corner positions.
    CHECK(close(T * vec3_of(1.0f, 0.0f, 0.0f), vec3_of(1.0f, 0.0f, 0.0f)));

    // w stays 1, which is what keeps the result a POINT rather than a
    // direction — and composing two translations adds their offsets, the
    // property that makes transform chains just matrix products.
    const vc::math::vc_mat3 U =
        mat3_of(1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f, 1.0f);
    CHECK(close(
        T * U, mat3_of(1.0f, 0.0f, 6.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f)));
}

// ------------------------------------------------ analytic geometry ----
//
// RED by design until dot/norm_squared/norm/project are written.
//
// Note the two KINDS of test below, because the split is the point. The
// hand-value cases pin specific arithmetic. The property cases state what a
// projection IS — parallel to the target, with an orthogonal residual — and
// those two properties together pin project() uniquely, without naming a
// formula. A wrong coefficient fails them instantly; a right one cannot.

TEST_CASE("vc_vec2: dot is the sum of componentwise products") {
    CHECK(close(vc::math::dot(vec2_of(3.0f, 4.0f), vec2_of(2.0f, 1.0f)),
                10.0f)); // 3*2 + 4*1
}

TEST_CASE("vc_vec2: dot is zero exactly when the vectors are perpendicular") {
    CHECK(close(vc::math::dot(vec2_of(1.0f, 0.0f), vec2_of(0.0f, 1.0f)), 0.0f));
    // Not axis-aligned, so this cannot pass by accident: (3,4) turned a
    // quarter turn is (-4,3), and the dot product vanishes.
    CHECK(
        close(vc::math::dot(vec2_of(3.0f, 4.0f), vec2_of(-4.0f, 3.0f)), 0.0f));
    // ...and is NOT zero for a pair that is merely close to perpendicular,
    // which is what stops a stub `return 0` from passing this test case.
    CHECK(
        !close(vc::math::dot(vec2_of(3.0f, 4.0f), vec2_of(-4.0f, 3.1f)), 0.0f));
}

TEST_CASE("vc_vec2: norm_squared skips the square root that norm takes") {
    const auto v = vec2_of(3.0f, 4.0f);
    CHECK(close(vc::math::norm_squared(v), 25.0f));
    CHECK(close(vc::math::norm(v), 5.0f));
}

TEST_CASE("vc_vec2: the dot product recovers the 3-4-5 triangle's angle") {
    // The 3-4-5 right triangle, as two vectors from the shared vertex: the
    // adjacent side (3,0) and the hypotenuse (3,4). cos of the angle between
    // them is adjacent/hypotenuse = 3/5, which the dot product must agree
    // with — dot / (|a| * |b|) = 9 / (3*5).
    const auto adjacent = vec2_of(3.0f, 0.0f);
    const auto hypotenuse = vec2_of(3.0f, 4.0f);
    const float cos_angle =
        vc::math::dot(adjacent, hypotenuse) /
        (vc::math::norm(adjacent) * vc::math::norm(hypotenuse));
    CHECK(close(cos_angle, 0.6f));
}

TEST_CASE("vc_vec2: projecting onto an axis keeps only that component") {
    CHECK(close(vc::math::project(vec2_of(3.0f, 4.0f), vec2_of(1.0f, 0.0f)),
                vec2_of(3.0f, 0.0f)));
    // The target's LENGTH must not matter — only its direction. A projection
    // onto (5,0) is the same vector as onto (1,0); if it is not, the
    // coefficient is missing its division by the target's squared norm.
    CHECK(close(vc::math::project(vec2_of(3.0f, 4.0f), vec2_of(5.0f, 0.0f)),
                vec2_of(3.0f, 0.0f)));
}

TEST_CASE("vc_vec2: project is parallel to the target with an orthogonal "
          "residual") {
    // THE acceptance criterion for this row, and it names no formula: these
    // two properties hold for the projection and for nothing else.
    const auto v = vec2_of(4.0f, 3.0f);
    const auto onto = vec2_of(2.0f, 1.0f);
    const auto p = vc::math::project(v, onto);

    // Parallel: in 2-D, the cross-product z-component of two parallel vectors
    // is zero.
    CHECK(close(p.x * onto.y - p.y * onto.x, 0.0f));

    // Orthogonal residual — what makes it the CLOSEST point on the line
    // through `onto`, rather than just some multiple of it.
    CHECK(close(vc::math::dot(v - p, onto), 0.0f));
}

TEST_CASE("vc_vec2: project throws on a degenerate target, and only then") {
    CHECK_THROWS_AS(
        (void)vc::math::project(vec2_of(1.0f, 2.0f), vec2_of(0.0f, 0.0f)),
        vc::vc_exception);

    // Both sides of the threshold, close to it. The tolerance is on the
    // SQUARED norm (1e-12), so these are lengths of 5e-7 and 2e-6 — squaring
    // to 2.5e-13 (below, throws) and 4e-12 (above, must not). A probe several
    // orders of magnitude clear of the boundary would pass for almost any
    // threshold and so would test nothing.
    CHECK_THROWS_AS(
        (void)vc::math::project(vec2_of(1.0f, 2.0f), vec2_of(5e-7f, 0.0f)),
        vc::vc_exception);
    CHECK_NOTHROW(
        (void)vc::math::project(vec2_of(1.0f, 2.0f), vec2_of(2e-6f, 0.0f)));
}

// The 3-component overloads are a SEPARATE implementation, not the 2-D one
// reused, so they can drift independently — a typo in the third term, or a
// copy-pasted guard with a flipped comparison, would be caught by nothing the
// vec2 cases above assert. They therefore get the same coverage rather than a
// token smoke test.

TEST_CASE("vc_vec3: dot and the two norms match their hand values") {
    CHECK(close(
        vc::math::dot(vec3_of(1.0f, 2.0f, 3.0f), vec3_of(4.0f, 5.0f, 6.0f)),
        32.0f)); // 4 + 10 + 18 — a dropped third term would give 14

    // (1,2,2) has length 3 — the 3-D analogue of a 3-4-5 triangle, chosen so
    // the expected value is exact in float rather than a rounded surd.
    CHECK(close(vc::math::norm_squared(vec3_of(1.0f, 2.0f, 2.0f)), 9.0f));
    CHECK(close(vc::math::norm(vec3_of(1.0f, 2.0f, 2.0f)), 3.0f));
}

TEST_CASE("vc_vec3: project is parallel to the target with an orthogonal "
          "residual") {
    // Deliberately NOT axis-aligned: projecting onto (0,0,2) would still look
    // right if two of the three components were being dropped.
    const auto v = vec3_of(1.0f, 2.0f, 3.0f);
    const auto onto = vec3_of(2.0f, 1.0f, 2.0f);
    const auto p = vc::math::project(v, onto);

    // Parallel in 3-D is a zero CROSS product — all three components.
    CHECK(close(p.y * onto.z - p.z * onto.y, 0.0f));
    CHECK(close(p.z * onto.x - p.x * onto.z, 0.0f));
    CHECK(close(p.x * onto.y - p.y * onto.x, 0.0f));

    CHECK(close(vc::math::dot(v - p, onto), 0.0f));
}

TEST_CASE("vc_vec3: only the target's direction matters, not its length") {
    const auto v = vec3_of(1.0f, 2.0f, 3.0f);
    const auto onto = vec3_of(2.0f, 1.0f, 2.0f);
    CHECK(close(vc::math::project(v, onto), vc::math::project(v, onto * 3.0f)));
}

TEST_CASE("vc_vec3: project throws on a degenerate target, and only then") {
    const auto v = vec3_of(1.0f, 2.0f, 3.0f);
    CHECK_THROWS_AS((void)vc::math::project(v, vec3_of(0.0f, 0.0f, 0.0f)),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)vc::math::project(v, vec3_of(0.0f, 0.0f, 5e-7f)),
                    vc::vc_exception);
    CHECK_NOTHROW((void)vc::math::project(v, vec3_of(0.0f, 0.0f, 2e-6f)));
}
