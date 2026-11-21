// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// THE VENDORED EIGEN, CHECKED AS A DEPENDENCY -- not as linear algebra.
//
// This file tests nothing about eigenvalues. It exists because the licence
// conclusion that allowed Eigen into this repository at all is TRUE OF ONE
// VERSION AND FALSE OF ITS PREDECESSOR, and nothing else in the build would
// notice if the wrong one were in the include path.
//
// THE HISTORY, BECAUSE IT EXPLAINS THE SHAPE OF THIS FILE. Eigen's 3.x line
// ships LGPL-licensed files and documents a preprocessor symbol,
// EIGEN_MPL2_ONLY, that turns including one into a compile error. That symbol
// was the mechanism this project intended to rely on: a licence rule the
// compiler enforces rather than a promise in a document. IT DOES NOT EXIST IN
// 5.x. Eigen 5.0 removed all LGPL code outright (!1197), so the flag was
// removed with it, and defining it now does exactly nothing -- it would read
// like enforcement while enforcing nothing at all.
//
// So the enforcement has to be the version itself, which is what the
// static_asserts below do.
//
// WHY THE OBVIOUS VERSION CHECK IS THE WRONG ONE. Eigen/Version defines four
// numbers, and the first is a trap:
//
//     EIGEN_WORLD_VERSION    3      <- NOT the version. See below.
//     EIGEN_MAJOR_VERSION    5
//     EIGEN_MINOR_VERSION    0
//     EIGEN_PATCH_VERSION    1
//
// Upstream's own comment on the first line reads: "The WORLD version will
// forever remain 3 for the Eigen3 library." It is a name, not a number --
// Eigen 5.0.1 and Eigen 3.4.1 both report WORLD = 3. A check written as
// `EIGEN_WORLD_VERSION >= 5` would therefore fail on the very version it was
// meant to accept, and `EIGEN_WORLD_VERSION == 3` would pass on both. The
// real version lives in the other three, which moved to semantic versioning
// as of 5.0.0.
//
// HOW THIS FAILS IF 3.4.x EVER SHADOWS THE VENDORED COPY. In 3.4.1 there is
// no Eigen/Version file at all; the numbers live in Core/util/Macros.h, there
// is no EIGEN_PATCH_VERSION and no EIGEN_VERSION_STRING, and MAJOR is 4.
// Both assertions below therefore fail on 3.4.1 -- the triple on a value
// mismatch, the string on an undefined identifier. Either way the build stops,
// which is the whole point.
//
// WHAT THIS FILE DELIBERATELY DOES NOT DO. It does not construct a
// SelfAdjointEigenSolver. Cross-checking a hand-rolled symmetric 2x2
// decomposition against Eigen's is S2's graded exercise, and spending it here
// to prove the headers parse would be spending the wrong thing.

#include <string_view>

#include <Eigen/Eigenvalues>
#include <Eigen/QR>
#include <Eigen/SVD>

#include "doctest/doctest.h"

// THE PIN, ENFORCED AT COMPILE TIME. Two independent spellings of the same
// fact: if a version bump is ever made deliberately, both lines have to be
// edited, and editing them is the moment to re-run the licence and standard
// checks recorded in the curriculum's P3 section F.4a.
static_assert(EIGEN_MAJOR_VERSION == 5 && EIGEN_MINOR_VERSION == 0 &&
                  EIGEN_PATCH_VERSION == 1,
              "Vendored Eigen is not 5.0.1. The MPL2-only licence conclusion "
              "in THIRD_PARTY_LICENSES.md was verified against 5.0.1 and does "
              "NOT hold for the 3.4 line, which ships LGPL files. Re-run the "
              "licence check before changing this.");

static_assert(std::string_view{EIGEN_VERSION_STRING} == "5.0.1",
              "EIGEN_VERSION_STRING disagrees with the semantic version "
              "triple above, which should be impossible in a clean checkout.");

TEST_CASE("vendored Eigen: the headers we depend on parse and link") {
    // The three includes above are the actual subject of this test case. They
    // drag in 204 distinct files from third_party/eigen -- Core, Jacobi,
    // Householder, LU, Cholesky, Geometry and the arch/ back-end for whatever
    // this machine is -- and compile them under this tree's real warning
    // flags and sanitizers.
    //
    // 204 is the count of DISTINCT FILES. `c++ -M` prints 217 path strings,
    // because it reaches some headers by more than one spelling and a plain
    // `sort -u` dedupes strings rather than files; THIRD_PARTY_LICENSES.md
    // carries the recipe that resolves them. An earlier version of this
    // comment said "roughly 190", which was wrong in both senses.
    //
    // Worth knowing: <Eigen/Eigenvalues> alone accounts for all 217, because
    // it transitively includes Eigen/QR and Eigen/SVD. So naming those two
    // separately buys no extra parse coverage -- they are listed because
    // P3 F.4's dependency-closure analysis named them, and an explicit
    // include documents the intent even where it is redundant.
    //
    // The body only has to prove the result is usable, so it stays trivial and
    // stays away from anything a later exercise is graded on. A 2x2 identity
    // has trace 2 and determinant 1, by inspection and with no algorithm
    // involved.
    const Eigen::Matrix2d identity = Eigen::Matrix2d::Identity();

    CHECK(identity.trace() == doctest::Approx(2.0));
    CHECK(identity.determinant() == doctest::Approx(1.0));

    // Fixed-size 2x2 of doubles: 4 doubles, no heap, no indirection. Checked
    // because a vendored header set that silently disagreed with the compiler
    // about layout would be a far worse problem than a wrong number.
    CHECK(identity.rows() == 2);
    CHECK(identity.cols() == 2);
    CHECK(sizeof(Eigen::Matrix2d) == 4 * sizeof(double));
}
