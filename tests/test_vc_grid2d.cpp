// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// grid2d exists so that dft2d, idft2d and (shortly) the Poisson solvers have
// nothing to validate. That makes this file the ONLY place the validation is
// tested, for all of them — so it is tested here rather than through any one
// caller, and the edge cases live here rather than in examples/07_dft2d.cpp,
// per the examples-are-for-reading rule in CMakeLists.txt.
//
// The case that is NOT here, deliberately: an overflowing product. width and
// height are uint32_t, so the widest representable product is
// (2^32-1)^2 = 18,446,744,065,119,617,025 against a SIZE_MAX of
// 18,446,744,073,709,551,615 — it fits with 8.6 billion to spare, so
// size_t(w)*size_t(h) is exact for every pair that can be passed. There is no
// wrapping arithmetic left to test. The predecessor of this type took
// std::size_t dimensions, where width = 2^63+4 with height = 2 multiplies to
// exactly 8 and a multiplying check accepts an 8-element buffer as 2^63 wide.
// Narrowing the type deleted the bug class; MAX_DIM_PRODUCT below pins the
// arithmetic that makes that true.

#include "doctest/doctest.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_scalar.h"
#include "vc/math/vc_grid2d.h"

namespace {

using vc::math::grid2d;
using vc::math::grid_dim;

// The arithmetic the type's safety rests on, asserted at compile time so it
// cannot quietly stop being true if the dimension type is ever widened.
constexpr vc::element_count k_max_dim = std::numeric_limits<grid_dim>::max();
static_assert(k_max_dim * k_max_dim <=
                  std::numeric_limits<vc::element_count>::max(),
              "uint32 dimensions must not be able to overflow a size_t "
              "product — if this fires, grid2d needs a divide-based check "
              "again");

} // namespace

TEST_CASE("grid2d::checked accepts dimensions that match the count") {
    const grid2d g = grid2d::checked(vc::element_count{8}, 4, 2, "test");
    CHECK(g.width() == 4);
    CHECK(g.height() == 2);
    CHECK(g.count() == 8);
}

TEST_CASE("grid2d::checked rejects a count that disagrees") {
    // The (void) casts below are required, not stylistic: checked() is
    // [[nodiscard]] and CHECK_THROWS_AS discards its result, which raises
    // -Wunused-result five times across this file. Caught by building a fresh
    // worktree -- an incremental build had already compiled this translation
    // unit, so the warnings never reappeared locally.
    //
    // 8 elements cannot be 3 wide by 3 high.
    CHECK_THROWS_AS((void)grid2d::checked(vc::element_count{8}, 3, 3, "test"),
                    vc::vc_exception);
    try {
        (void)grid2d::checked(vc::element_count{8}, 3, 3, "test");
    } catch (const vc::vc_exception& e) {
        CHECK(e.code() == vc::vc_error_code::invalid_argument);
    }
}

TEST_CASE("grid2d::checked rejects a zero dimension") {
    // Matching vc_image_writer::validated(), which has refused zero-sized
    // images since P1. A zero in either axis is ill-formed, not empty — and
    // note this DIFFERS from dft1d's "empty in, empty out", which is correct
    // there because a 1-D transform takes no dimension argument.
    CHECK_THROWS_AS((void)grid2d::checked(vc::element_count{0}, 0, 0, "test"),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)grid2d::checked(vc::element_count{8}, 0, 8, "test"),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)grid2d::checked(vc::element_count{8}, 8, 0, "test"),
                    vc::vc_exception);
}

TEST_CASE("grid2d::checked accepts degenerate-but-valid single rows and "
          "columns") {
    // 1xN and Nx1 are legitimate: a single scanline has a spectrum. Only ZERO
    // is refused, and the distinction matters because dft2d's own checks 3 and
    // 4 transform exactly these shapes to tie themselves to dft1d.
    const grid2d row = grid2d::checked(vc::element_count{4}, 4, 1, "test");
    CHECK(row.width() == 4);
    CHECK(row.height() == 1);
    const grid2d col = grid2d::checked(vc::element_count{4}, 1, 4, "test");
    CHECK(col.width() == 1);
    CHECK(col.height() == 4);
    const grid2d one = grid2d::checked(vc::element_count{1}, 1, 1, "test");
    CHECK(one.count() == 1);
}

TEST_CASE("grid2d::checked is NOT fooled by a transposed pair") {
    // 4x2 and 2x4 both satisfy count == 8, so a count check alone cannot tell
    // them apart — and must not try to. Both are valid; it is the CALLER's job
    // to pass the right orientation, which is why 07_dft2d checks both
    // orientations against an independent reference.
    CHECK(grid2d::checked(vc::element_count{8}, 4, 2, "test").width() == 4);
    CHECK(grid2d::checked(vc::element_count{8}, 2, 4, "test").width() == 2);
}

TEST_CASE("grid2d::checked names the caller in its message") {
    // The `caller` parameter follows the idiom vc_io_fs.h already established,
    // so the diagnostic points at whoever passed bad dimensions rather than at
    // vc_grid2d.cpp — which would be the same for all four eventual callers
    // and therefore useless.
    try {
        (void)grid2d::checked(vc::element_count{8}, 3, 3,
                              "some_specific_caller");
        FAIL("expected a throw");
    } catch (const vc::vc_exception& e) {
        const std::string msg = e.what();
        CHECK(msg.find("some_specific_caller") != std::string::npos);
        // and the offending numbers, so the message is actionable
        CHECK(msg.find("8") != std::string::npos);
        CHECK(msg.find("3") != std::string::npos);
        CHECK(msg.find("9") != std::string::npos); // 3 x 3, what it needed
    }
}

TEST_CASE("grid2d::checked reads the length from a span, so the count cannot "
          "be mis-stated") {
    // The preferred overload. Passing a count by hand leaves room to pass the
    // wrong one; passing the buffer does not.
    const std::vector<float> buf(12, 0.0F);
    const std::span<const float> view{buf};
    const grid2d g = grid2d::checked(view, 4, 3, "test");
    CHECK(g.count() == 12);
    CHECK_THROWS_AS((void)grid2d::checked(view, 5, 3, "test"),
                    vc::vc_exception);
}

TEST_CASE("grid2d::count() is exact at the widest legal dimensions") {
    // The pair that would overflow if the dimensions were size_t. Here it is
    // merely large, and exact — no buffer is allocated, only the arithmetic is
    // checked, which is the only thing count() does.
    constexpr grid_dim max_dim = std::numeric_limits<grid_dim>::max();
    const vc::element_count expected = static_cast<vc::element_count>(max_dim) *
                                       static_cast<vc::element_count>(max_dim);
    const grid2d g = grid2d::checked(expected, max_dim, max_dim, "test");
    CHECK(g.count() == expected);
    CHECK(g.count() == vc::element_count{18446744065119617025ULL});
}
