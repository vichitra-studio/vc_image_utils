// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <array>

namespace vc::math {

// Small, FIXED-SIZE linear algebra: 2- and 3-component vectors, 2x2 and 3x3
// matrices, and the three operations an image transform actually needs
// (apply, compose, invert).
//
// ---- Why fixed sizes, and not a general vc_mat<R, C> ----
//
// 1. The shapes are checked BY THE COMPILER. `vc_mat3 * vc_vec2` does not
//    compile; a runtime-dimension matrix class could only throw. Every
//    dimension mismatch this library can suffer is a build error, not a test
//    failure.
// 2. The inverse at these sizes is a CLOSED FORM (a determinant and a handful
//    of products). A general NxN inverse is a different algorithm — LU with
//    pivoting — carrying conditioning and stability concerns these sizes
//    simply do not have.
// 3. Nothing in the imaging pipeline needs more. An affine or projective 2-D
//    transform is 3x3, a colour matrix is 3x3, a structure tensor is 2x2.
//    Where the dimensions genuinely grow (least-squares fits, SVD/null-space
//    work) the answer is a real linear-algebra library, not a home-grown
//    general one — this header is deliberately not on that path.
//
// So this is geometry for transforms, not a linear-algebra facility. If a
// caller ever wants an NxN solve, that is the signal to reach for Eigen, not
// to generalise these types.
//
// ---- Convention: COLUMN vectors, matrix on the LEFT ----
//
//     y = A x        A is (rows x cols), x has `cols` components,
//                    y has `rows` components
//
// There is deliberately NO `operator*(vc_vec2, vc_mat2)` overload. The
// row-vector convention (x A, with every matrix transposed) is equally valid
// and used by some graphics APIs, but mixing the two silently transposes
// everything it touches. Offering only one direction makes the wrong one fail
// to compile.
//
// Storage is `m[row][col]`, so `a.m[1][0]` is row 1, column 0 — matching the
// order the maths is written in.
//
// Unlike vc_image_info::index(), these accept no runtime bounds check: matrix
// element indices in real code are literals (`a.m[0][1]`), not caller-supplied
// coordinates, so the array bounds are already a compile-time property. The
// checks in index() exist because x/y/ch arrive from outside; nothing
// analogous happens here.

struct vc_vec2 {
    float x;
    float y;
};

struct vc_vec3 {
    float x;
    float y;
    float z;
};

// std::array rather than a C array (modernize-avoid-c-arrays): it is a proper
// value type — copyable, comparable, with size() and bounds-checked at() —
// where a raw float[2][2] decays to a pointer at the first opportunity.
// Indexing reads identically: a.m[1][0] is row 1, column 0.
using mat2_rows = std::array<std::array<float, 2>, 2>;
using mat3_rows = std::array<std::array<float, 3>, 3>;

struct vc_mat2 {
    mat2_rows m; // m[row][col]

    // Defined inline rather than left as an exercise: the tests use the
    // identity as their reference value (A * inverse(A) == identity), so a
    // throwing stub here would fail every other test for the wrong reason.
    // The brace depth is std::array's, not ours: each std::array wraps an
    // internal C array, so a fully-explicit initialiser needs one pair per
    // level. Eliding them compiles to the WRONG thing here (it fills row 0
    // and reports the second row as excess), so the nesting is spelled out.
    // It appears only in this factory and the test's builders — every other
    // use is plain m[row][col] indexing.
    [[nodiscard]] static constexpr vc_mat2 identity() noexcept {
        return vc_mat2{{{{{1.0f, 0.0f}}, {{0.0f, 1.0f}}}}};
    }
};

struct vc_mat3 {
    mat3_rows m; // m[row][col]

    [[nodiscard]] static constexpr vc_mat3 identity() noexcept {
        return vc_mat3{{{{{1.0f, 0.0f, 0.0f}},
                         {{0.0f, 1.0f, 0.0f}},
                         {{0.0f, 0.0f, 1.0f}}}}};
    }
};

// ---- 2x2 ----

// Apply the map to a vector: the j-th column of `a` says where basis vector
// j lands, and the result is that combination weighted by v's components.
[[nodiscard]] vc_vec2 operator*(const vc_mat2& a, const vc_vec2& v);

// Compose two maps. `a * b` is "apply b FIRST, then a" — matrix products read
// right to left, matching how they are applied to a vector on the right.
[[nodiscard]] vc_mat2 operator*(const vc_mat2& a, const vc_mat2& b);

[[nodiscard]] float determinant(const vc_mat2& a);

// Throws vc_exception(invalid_argument) when `a` is singular — a matrix that
// collapses the plane onto a line has no inverse, and returning a matrix full
// of infinities would push the failure downstream into whatever image the
// caller was about to transform.
[[nodiscard]] vc_mat2 inverse(const vc_mat2& a);

// ---- 3x3 ----
//
// Note what a 3x3 is FOR here. It is not primarily "a transform of 3-D
// space": its main job is carrying a 2-D AFFINE transform in homogeneous
// coordinates, acting on (x, y, 1) with the translation living in the third
// column. Translation is not a linear map — it moves the origin — so no 2x2
// can express it; embedding the plane at z = 1 is the standard trick that
// makes translation composable by ordinary matrix multiplication.

[[nodiscard]] vc_vec3 operator*(const vc_mat3& a, const vc_vec3& v);

[[nodiscard]] vc_mat3 operator*(const vc_mat3& a, const vc_mat3& b);

[[nodiscard]] float determinant(const vc_mat3& a);

[[nodiscard]] vc_mat3 inverse(const vc_mat3& a);

// A matrix is treated as singular when |determinant| falls below this.
//
// Deliberately a crude, absolute threshold: the honest test for "can this be
// inverted usefully" is a CONDITION NUMBER, which compares the determinant
// against the matrix's own scale, and computing one properly is exactly the
// kind of numerical work this header exists not to attempt. At the scales an
// image transform uses (rotations, modest scalings, pixel-magnitude
// translations) an absolute cutoff separates "genuinely degenerate" from
// "fine" without ceremony. Revisit if a caller ever needs transforms whose
// entries span many orders of magnitude.
inline constexpr float singular_tolerance = 1e-8f;

} // namespace vc::math
