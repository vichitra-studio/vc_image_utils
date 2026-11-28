// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_scalar.h" // vc::float32 -- leaf header
#include <array>
#include <cmath> // std::isfinite, std::sqrt -- used by vc_vec2 below

namespace vc::math {

// Small, FIXED-SIZE linear algebra, in three layers:
//
//   - 2- and 3-component vectors, 2x2 and 3x3 matrices, and the three
//     operations an image transform actually needs: apply, compose, invert.
//   - A short analytic-geometry layer over those -- dot, the two norms,
//     project -- which the transforms need rather than merely offer.
//   - And, for the 2x2 SYMMETRIC case only, an eigendecomposition. That is
//     the one thing here not about transforms at all; see the block at the
//     end of this header for why it lives in a geometry file.
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
    vc::float32 x;
    vc::float32 y;

    // Every component finite. A predicate and not a norm: there is no free
    // function for this, whereas the length IS published below as norm() /
    // norm_squared() and is NOT duplicated here -- one spelling per quantity,
    // which is the rule the operator* and distance() notes further down state.
    [[nodiscard]] bool is_valid() const {
        return std::isfinite(x) && std::isfinite(y);
    }
};

struct vc_vec3 {
    vc::float32 x;
    vc::float32 y;
    vc::float32 z;

    [[nodiscard]] bool is_valid() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }
};

// std::array rather than a C array (modernize-avoid-c-arrays): it is a proper
// value type — copyable, comparable, with size() and bounds-checked at() —
// where a raw float[2][2] decays to a pointer at the first opportunity.
// Indexing reads identically: a.m[1][0] is row 1, column 0.
using mat2_rows = std::array<std::array<vc::float32, 2>, 2>;
using mat3_rows = std::array<std::array<vc::float32, 3>, 3>;

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

[[nodiscard]] vc::float32 determinant(const vc_mat2& a);

// The sum of the diagonal entries. Note "entries", not "eigenvalues": this
// is a straight sum over m[0][0] and m[1][1] and needs no decomposition,
// which is exactly what makes it cheap enough to be worth having.
//
// There is deliberately NO trace(vc_mat3), unlike determinant and inverse
// which are offered at both sizes. Nothing calls one -- same reasoning that
// keeps operator+, distance() and normalize() out of this header. Add it when
// something needs it.
[[nodiscard]] vc::float32 trace(const vc_mat2& a);

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

[[nodiscard]] vc::float32 determinant(const vc_mat3& a);

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
inline constexpr vc::float32 singular_tolerance = 1e-8f;

// ---- analytic geometry ----
//
// Vector arithmetic comes first because the geometry needs it, not as a
// convenience: project() returns a SCALED vector, and the property that
// defines a projection is stated as a DIFFERENCE (see below). Those two — `-`
// and scalar `*` — are the whole surface. There is no operator+ because
// nothing calls one; the same reasoning that keeps distance(), the L1 norm and
// normalize() out of this header.
//
// Only `v * s` is spelled, not `s * v`. Unlike the matrix products above there
// is no transposition hazard here — scalar multiplication commutes — so this
// is purely about the codebase having one spelling instead of two.

[[nodiscard]] vc_vec2 operator-(const vc_vec2& a, const vc_vec2& b);
[[nodiscard]] vc_vec2 operator*(const vc_vec2& v, vc::float32 s);

[[nodiscard]] vc_vec3 operator-(const vc_vec3& a, const vc_vec3& b);
[[nodiscard]] vc_vec3 operator*(const vc_vec3& v, vc::float32 s);

// The inner product: componentwise products, summed. Two things ride on it —
// it measures how much of one vector lies along another, and it is zero
// exactly when the two are orthogonal.
[[nodiscard]] vc::float32 dot(const vc_vec2& a, const vc_vec2& b);
[[nodiscard]] vc::float32 dot(const vc_vec3& a, const vc_vec3& b);

// Squared length — dot(v, v). Exposed next to norm() rather than hidden inside
// it because the square root is usually waste: sqrt is monotonic, so ordering
// and thresholding are unaffected by skipping it, and the squared form is
// exactly what patch matching sums (SSD). Call norm() only when the number
// itself must be a length.
//
// RANGE ASSUMPTION: components are of geometric magnitude — pixel coordinates,
// sub-pixel offsets, colour components. Squaring overflows to +inf somewhere
// past 1e19, and an infinite squared norm makes project() below divide by it
// and return the ZERO VECTOR silently rather than throwing. Nothing an image
// transform does comes within fifteen orders of magnitude of that, so it is
// left unguarded — but stated, for the same reason singular_tolerance's own
// assumption is stated rather than assumed.
[[nodiscard]] vc::float32 norm_squared(const vc_vec2& v);
[[nodiscard]] vc::float32 norm_squared(const vc_vec3& v);

// Euclidean (L2) length. The L1 norm is deliberately absent: its use in this
// codebase is per-pixel patch distance, which belongs with the patch code, not
// in a geometry header. No distance(a, b) either — it is norm(a - b) at the
// call site, and one spelling of a one-line composition is enough.
//
// norm() DOES NOT SHARE norm_squared()'s RANGE ASSUMPTION. It is computed with
// std::hypot, not sqrt(norm_squared(v)), so it is correct across the whole
// float32 range at both ends. That matters outside image geometry: a gradient
// is not a pixel coordinate and has no magnitude bound, and sqrt-of-sum-of-
// squares fails BOTH ways on one -- components past ~1e19 square to +inf, and
// components below ~1e-22 square to zero, so the length of a short but
// perfectly representable vector comes back as exactly 0. An optimizer that
// scales its stopping test by such a length then reports success without
// moving. Use norm_squared() when ordering or thresholding (it is cheaper and
// the comparison is unaffected); use norm() when the number is a length.
[[nodiscard]] vc::float32 norm(const vc_vec2& v);
[[nodiscard]] vc::float32 norm(const vc_vec3& v);

// The component of `v` lying along `onto` — equivalently, the point on the
// line through `onto` closest to `v`. Returns a VECTOR (some multiple of
// `onto`), not the scalar coefficient.
//
// The defining property, and the test that actually matters: the residual
// `v - project(v, onto)` is ORTHOGONAL to `onto`. That is what makes it a
// projection rather than just some multiple, and it holds for no other one.
//
// Throws vc_exception(invalid_argument) when `onto` is the zero vector: it
// spans no direction, so there is no line to project onto. Same reasoning as
// inverse()'s singular check — fail where the cause is, rather than handing
// back NaNs that surface somewhere downstream.
[[nodiscard]] vc_vec2 project(const vc_vec2& v, const vc_vec2& onto);
[[nodiscard]] vc_vec3 project(const vc_vec3& v, const vc_vec3& onto);

// `onto` counts as degenerate when its SQUARED norm falls below this — the
// squared form because that is the quantity project() divides by. Testing the
// number that can actually destroy the result beats testing its square root.
//
// 1e-12 is a LENGTH of 1e-6, squared. At the scales this header serves — pixel
// coordinates and sub-pixel offsets — a target a millionth of a pixel long
// carries no usable direction: its components are down among the rounding
// error of whatever produced it, so the "direction" the projection would be
// computed against is noise, and dividing by it amplifies that noise by 1e12.
//
// Absolute rather than relative to `v`, and for the same reason
// singular_tolerance is: a scale-aware test is the numerical work this header
// exists not to attempt. Revisit alongside singular_tolerance if a caller ever
// works at a scale where a 1e-6 vector is meaningful.
inline constexpr vc::float32 degenerate_norm_squared_tolerance = 1e-12f;

// ---- symmetric 2x2 eigendecomposition ----
//
// WHY THIS IS IN A GEOMETRY HEADER. Everything above is about transforms:
// apply, compose, invert. This is not. It is here because the thing that
// needs it is the STRUCTURE TENSOR -- the 2x2 matrix of Gaussian-smoothed
// gradient products whose eigenvalues say whether a neighbourhood is flat,
// an edge, or a corner -- and that matrix takes no image and no kernel, so
// by this library's own placement rule ("does it take a vc_image?") it is
// not a pixelops operation. The preamble's note that 2x2 is the structure
// tensor's size was written before this existed; this is that note cashed in.
//
// SYMMETRIC ONLY, and the restriction is load-bearing rather than laziness.
// A general 2x2 can have COMPLEX eigenvalues -- a rotation has no real
// eigenvector at all, since it knocks every direction off its own line. A
// symmetric matrix cannot: its eigenvalues are always real and its
// eigenvectors always orthogonal. Both guarantees are used below, and
// neither survives dropping the symmetry.
//
// So an asymmetric argument is REJECTED rather than coerced. Reading just one
// triangle and ignoring the other would be the tempting cheap option and is
// the wrong one: a caller who fills only one off-diagonal slot of a structure
// tensor -- a genuinely easy slip -- would get a plausible, wrong answer and
// no indication of it. Note that Eigen's own SelfAdjointEigenSolver DOES take
// the cheap option: its documentation states that "only the lower triangular
// part of the matrix is referenced". That is a reasonable choice for a
// general-purpose library and a bad one here, and it means Eigen cannot be
// used as the oracle for this particular check.

// The result of eigen_symmetric(). Eigenvalues in DESCENDING order, which is
// the convention Deisenroth 4.2 records as usual in the literature -- and is
// the opposite of Eigen, whose SelfAdjointEigenSolver documents its output as
// "sorted in increasing order". Cross-checking the two therefore means
// MAPPING major-to-last and minor-to-first, not sorting both and hoping.
//
// `v_major` is the eigenvector for `lambda_major`, `v_minor` for
// `lambda_minor`. Both are UNIT length. Both are returned even though the
// corner response R = det - k.tr^2 needs neither: the directions are what
// distinguish an edge from a corner when you want to LOOK at the field
// rather than threshold it.
struct vc_eigen_2x2 {
    vc::float32 lambda_major; // the larger eigenvalue
    vc::float32 lambda_minor; // the smaller
    vc_vec2 v_major;          // unit; eigenvector of lambda_major
    vc_vec2 v_minor;          // unit; eigenvector of lambda_minor
    // Set when the two directions above are PLACEHOLDERS rather than
    // measurements -- see the degenerate-case note below.
    //
    // NOT named `degenerate`, and the rename is the point. "Degenerate"
    // reads as a property of the MATRIX, and this is a property of the two
    // VECTORS: it answers "did I compute these directions, or did I give up
    // and hand you the axes?". The eigenvalues are unaffected either way.
    //
    // It also stops a specific misreading. For a structure tensor, a FLAT
    // region and a symmetric CORNER both have equal eigenvalues, so both set
    // this flag -- it is not the flat-versus-corner signal, and anyone who
    // reads it as "flat" will be wrong half the time. What separates those
    // two is the MAGNITUDE of the eigenvalues, not the gap between them.
    bool directions_arbitrary;
};

// Eigen-decompose a SYMMETRIC 2x2.
//
// Throws vc_exception(invalid_argument) if `a` is not symmetric to within
// asymmetry_tolerance (see above for why this is not silently coerced).
//
// THE DEGENERATE CASE, which is the one worth reading. When the two
// eigenvalues are equal, EVERY nonzero vector is an eigenvector -- the
// identity matrix is the clean example: I.v = 1.v for any v at all, so the
// eigenspace is the whole plane rather than two particular directions. There
// is then no such thing as "the" eigenvector, and any code that reports one
// anyway is inventing a direction out of nothing.
//
// This is not an error, and it is not rare. A FLAT IMAGE REGION produces it
// every time: no gradient in any direction, so no direction is preferred.
// Throwing would make a corner detector throw on blank sky.
//
// So when `directions_arbitrary` is set, the function returns the basis --
// (1,0) and (0,1) -- which are genuinely valid eigenvectors in that case,
// are unit length, and are orthogonal, so every invariant below still holds.
// What the caller must NOT do is read meaning into the directions. The
// eigenvalues remain correct and meaningful; only the directions are
// arbitrary.
//
// Deliberately NOT returned as zero vectors, even though "no answer" is the
// honest sentiment. A zero vector satisfies the residual identity
// ||a.v - lambda.v|| = 0 trivially, so it would pass an eigenvector test
// while carrying no information. That is the same CLASS of defect this
// library has already shipped once -- a check that goes green while the
// thing it checks is broken -- though not the same shape: there it was
// gradient_magnitude's dtype validation passing because a DIFFERENT
// function's exception happened to carry the expected error code. The write-up
// is at the top of src/pixelops/vc_gradients.cpp, beside
// require_matched_pair(); it is in the .cpp, not the header.
//
// INVARIANTS the result satisfies. Read every `==` below as "to within
// float32's own rounding" -- these are floats and exact equality is not on
// offer, except for the ordering, which is a comparison rather than an
// arithmetic result.
//
//   lambda_major >= lambda_minor              (exact: an ordering)
//   ||v_major|| == ||v_minor|| == 1
//   dot(v_major, v_minor) == 0
//   trace(a)       == lambda_major + lambda_minor
//   determinant(a) == lambda_major * lambda_minor
//   a * v_major == v_major * lambda_major     (and likewise for minor)
//
// THE LAST ONE CARRIES A CAVEAT when `directions_arbitrary` is set, and it
// is not obvious. If the gap is EXACTLY zero -- the identity, the zero
// matrix, any scaled identity -- the returned basis is an exact eigenpair
// and the residual is zero. If the gap is merely SMALL, it is not: with
// b = 0 and c just above a, the true major axis is (0,1) while the contract
// hands back (1,0), so v_major is paired with the wrong eigenvalue and the
// residual comes out at about the GAP rather than zero. Bounded, because the
// gap is exactly what put the flag there -- but not zero. See
// degenerate_gap_tolerance below for the coupling that keeps that bounded.
[[nodiscard]] vc_eigen_2x2 eigen_symmetric(const vc_mat2& a);

// How far the two off-diagonal entries may differ before `a` is rejected as
// asymmetric. RELATIVE to the matrix's largest entry, not absolute: a
// structure tensor built from a bright image has entries orders of magnitude
// larger than one built from a dim image, and an absolute cutoff would be
// wrong for at least one of them.
//
// Loose enough that a caller computing the same off-diagonal product twice,
// by two different roundings, is not punished. Tight enough that a genuinely
// transposed or half-filled matrix is caught.
inline constexpr vc::float32 asymmetry_tolerance = 1e-6f;

// When the eigenvalue GAP, relative to the larger magnitude, falls to this,
// the eigenvectors are reported as degenerate.
//
// WHY 1e-5 AND NOT SOMETHING SMALLER. The sensitivity of an eigenvector to a
// perturbation of the matrix goes as (size of perturbation) / (eigenvalue
// gap). Entries here are float32, so they already carry a relative error of
// about 1e-7 simply by existing. A gap of 1e-5 of the matrix scale therefore
// leaves the computed DIRECTION uncertain at the 1e-2 level -- about a
// hundredth of a radian -- and below that gap the direction is noise dressed
// as an answer. The eigenVALUES stay accurate throughout; it is only the
// directions that dissolve.
//
// One expression covers both degenerate cases, which is why there is no
// separate absolute floor: for the identity the gap is 0 and the scale is 1,
// and for the zero matrix the gap is 0 and the scale is 0, so `gap <= tol *
// scale` catches both.
//
// THIS TOLERANCE IS COUPLED TO THE ACCEPTANCE SUITE'S -- DORMANTLY, TODAY --
// and the coupling is invisible from here, so it is written down. When the gap
// is small but nonzero the returned basis is paired with the wrong eigenvalue
// by about the gap (see the caveat on eigen_symmetric's invariants). IF that
// residual were ever checked, the bound would be the suite's tolerance times
// the eigenvalue magnitude, and the contract would hold only while
//
//     degenerate_gap_tolerance <= (the suite's residual tolerance)
//
// with both at 1e-5 today, which is equality and no headroom.
//
// It is DORMANT because no check currently depends on it: the suite's gated
// arbitrary-direction fixtures all have a gap of exactly zero, so their
// residual is zero whatever these numbers are, and the near-degenerate case
// is printed rather than asserted. The relation would bite the moment that
// print becomes an assertion.
//
// THE SUITE DOES NOT ENFORCE THAT RELATION, and saying so is the point of
// this paragraph. It cannot: every fixture's gap is a fixed constant, so a
// looser threshold only flags MORE matrices, and the suite probes none of
// those. An earlier draft of this comment claimed group 4 would catch a
// violation. It does not, and raising the implementation's tolerance tenfold
// leaves the suite's score untouched. The relation above is therefore a
// thing to preserve by reading, not a thing the tests defend.
//
// A fixture WOULD catch it -- one whose relative gap sits just above the
// threshold, which must come back un-flagged. It is deliberately absent,
// because a gap that small is not reliably computable by every legitimate
// derivation (see the note below), so such a fixture would quietly convert
// this tolerance into a precision gate.
//
// The clean way out, if this ever needs loosening, is to return the basis
// ORDERED BY THE LARGER DIAGONAL rather than always (1,0) first -- which
// removes the mispairing instead of bounding it. That is a contract change,
// so it is noted rather than done.
//
// ONE MORE THING, AND IT CONSTRAINS WHICH CLOSED FORM MEETS THIS CONTRACT.
// A discriminant computed as sqrt(mean^2 - det) squares the inputs and then
// subtracts, which costs HALF the available digits: the resulting gap
// carries a relative error of order 2*sqrt(eps), about 7e-4 for float32,
// which is roughly SEVENTY TIMES this tolerance. Note what kind of claim
// that is: it bounds the ERROR, so the computed gap can come out too small
// (collapsing to zero, and the directions wrongly called arbitrary) or too
// large (a genuinely degenerate matrix reported as having directions). It is
// noise of that size, not a floor underneath the answer.
//
// A discriminant built as a SUM OF SQUARES has no such term -- nothing is
// squared and then subtracted -- and resolves gaps down to float32's own
// ~1e-7. So 1e-5 here is comfortably achievable by one derivation and not by
// the other. A genuinely form-agnostic tolerance would have to be looser
// than 7e-4, which is a different contract from the one above, and choosing
// between them is a decision rather than a detail.
inline constexpr vc::float32 degenerate_gap_tolerance = 1e-5f;

} // namespace vc::math
