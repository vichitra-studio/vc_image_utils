// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <span>

#include "vc/core/vc_scalar.h"   // vc::element_count -- a buffer length
#include "vc/utils/vc_strings.h" // vc::utils::string_view -- borrowed text

namespace vc::math {

// A grid extent along one axis, in samples.
//
// uint32_t, and that is load-bearing rather than incidental -- see the
// overflow arithmetic below. Sibling of vc::image_dim (vc_types.h), which is
// the same underlying type for the same reason; this one exists separately so
// that vc::math can name an extent WITHOUT including image vocabulary, which
// is the layering invariant vc_scalar.h was created to protect and that
// vc_dft.h's placement argument depends on. Role-scoped aliases over one
// primitive are the established pattern here: vc has image_dim and
// channel_count, vc::pipe has slot_index, all three uint32_t.
using grid_dim = std::uint32_t;

// A 2-D extent that is KNOWN to agree with a buffer's length.
//
// ---- WHY THIS IS A TYPE AND NOT A CHECK ----
//
// The only way to obtain a grid2d is through checked(), so a function taking
// one has nothing left to validate. That is the point, and it is not the same
// as factoring the check into a helper:
//
//     a helper you must remember to call  -- can be forgotten
//     a parameter you cannot construct    -- cannot be
//
// This matters because the failure it prevents already happened once. dft2d
// was written with NO dimension validation at all, in a function whose own
// header documented the exact check to write, including the overflow-safe
// form. Prose did not transmit it. A require() helper would not have either,
// because the way it failed was by never being called.
//
// The pattern is not new here. vc_image_writer::validated() is the same
// trick one layer up: the only way to get a vc_image_info is to pass four
// checks, so every function taking a vc_image inherits valid dimensions for
// free and writes no checks of its own. A survey of this repository found
// exactly 62 throw sites and only ONE unvalidated function pair -- not
// because the code is diligent about repeating itself, but because the
// guarantee lives in a type. dft2d sat outside that only because vc::math
// deliberately cannot see vc_image.
//
// ---- WHY uint32 AND NOT size_t ----
//
// So the product cannot overflow. This is the whole reason the dimensions
// are narrowed rather than validated more carefully:
//
//     widest product   (2^32-1)^2 = 18,446,744,065,119,617,025
//     SIZE_MAX                    = 18,446,744,073,709,551,615
//
// It fits, with about 8.6 billion to spare, so size_t(w) * size_t(h) is
// provably exact for every representable pair. With size_t dimensions there
// is no wider type to promote into and the product wraps freely -- width =
// 2^63 + 4 with height = 2 multiplies to exactly 8, so a buffer of 8
// elements passes a multiplying check and is then indexed as 2^63 wide.
// That is why vc_dft.h used to insist on dividing instead of multiplying.
// Narrowing the type removes the hazard rather than guarding against it, and
// the divide-not-multiply rule becomes unnecessary.
//
// 2^32-1 pixels per side is not a real limit for this library; vc::image_dim
// is uint32_t for the same reason.
//
// ---- ZERO DIMENSIONS ARE REJECTED, AND THIS DIFFERS FROM dft1d ----
//
// checked() refuses a zero width or height, matching
// vc_image_writer::validated(), which has refused zero-sized images since
// P1. So there is no "empty 2-D plane" case to handle: it is unconstructible.
//
// dft1d keeps its documented "empty in, empty out" contract, and the two are
// not inconsistent. A 1-D transform takes no dimension argument, so an empty
// input is unambiguous -- it means a zero-length signal, which has a
// zero-length spectrum. A 2-D transform needs a width and a height, and a
// zero in either is not an empty picture but an ill-formed one; there is no
// single sensible answer to "the DFT of a 0-by-7 image".
//
// ---- WHAT THIS STILL DOES NOT GUARANTEE ----
//
// grid2d travels as a separate argument from the buffer it describes, so
// passing a grid2d built against a DIFFERENT buffer of the same length is
// still constructible. Closing that would mean bundling the span and the
// extent into one type. The span-taking overload of checked() narrows it as
// far as two arguments allow -- it reads the length from the actual buffer
// rather than taking a count on trust -- which leaves only "a different
// buffer that happens to be the same length" as a reachable mistake.
class grid2d {
  public:
    // Throws vc_error_code::invalid_argument unless width and height are both
    // non-zero and width * height == count. `caller` is folded into the
    // message, matching the idiom vc_io_fs.h already uses, so the diagnostic
    // names whoever passed the bad dimensions rather than naming this file.
    [[nodiscard]] static grid2d checked(vc::element_count count,
                                        grid_dim width,
                                        grid_dim height,
                                        vc::utils::string_view caller);

    // Preferred overload: reads the length from the buffer itself, so the
    // count cannot be supplied wrongly. Generic over the element type because
    // the callers do not agree on one -- dft2d passes complex samples, the
    // Poisson solvers pass a real scalar field.
    template <typename T>
    [[nodiscard]] static grid2d checked(std::span<const T> data,
                                        grid_dim width,
                                        grid_dim height,
                                        vc::utils::string_view caller) {
        return checked(data.size(), width, height, caller);
    }

    [[nodiscard]] grid_dim width() const noexcept {
        return width_;
    }
    [[nodiscard]] grid_dim height() const noexcept {
        return height_;
    }

    // The element count, computed in size_t. Exact for every constructible
    // grid2d -- see the overflow note above.
    [[nodiscard]] vc::element_count count() const noexcept {
        return static_cast<vc::element_count>(width_) *
               static_cast<vc::element_count>(height_);
    }

  private:
    // Private, so checked() is the only route in. A public constructor would
    // make the whole type decorative.
    grid2d(grid_dim width, grid_dim height) noexcept
        : width_(width), height_(height) {
    }

    grid_dim width_;
    grid_dim height_;
};

} // namespace vc::math
