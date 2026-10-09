// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_conv_frequency.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/math/vc_dft.h"
#include "vc/utils/vc_strings.h"

#include <string>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace vc::pixelops {

// ---- RED. Both bodies are yours to write. ----------------------------------
//
// 11_conv_theorem.cpp holds the acceptance checks and currently fails at the
// first one. The header states the contract; this is the order the checks will
// come green in:
//
//   1. frequency_extent  -- the policy rejection, then the N+M-1 arithmetic.
//      Checks 1-7. Nothing else can be tested until this is right.
//
//   2. convolve_frequency -- per channel:
//        a. build a complex plane of the source, zero-filled to the extent
//        b. build a complex plane of the kernel, CENTRED AND WRAPPED:
//           tap (kx,ky) -> ( ((kx % Lx)+Lx)%Lx , ((ky % Ly)+Ly)%Ly )
//        c. dft2d both, multiply pointwise, idft2d
//        d. CHECK max|imag| against the result's RMS before taking .real()
//        e. copy the top-left W x H out -- no offset, if (b) was done right
//
// The one that will bite is (b). A causal placement compiles, runs, and gives
// a correct-looking image shifted by (radius_x, radius_y).

namespace {

// One axis of the zero-padded extent: W + kw - 1.
//
// Computed in uint64 and then range-checked, rather than in the uint32 the
// inputs arrive as. vc::image_dim and grid_dim are both uint32_t, so
// `src.width() + kernel.width() - 1` is evaluated in 32-bit unsigned
// arithmetic and WRAPS -- quietly, with no warning. vc_grid2d.h's whole
// argument is that a grid2d cannot be wrong, and the narrowing is there
// specifically so the width*height product is provably exact; handing it a
// sum that already wrapped defeats that, and it would surface from checked()
// as a confusing count-vs-dimension mismatch rather than as the real cause.
//
// No real image reaches this limit. The check is here because the type
// permits it and the diagnostic would otherwise point at the wrong place.
vc::math::grid_dim padded_axis(vc::image_dim extent,
                               vc::image_dim taps,
                               const char* axis) {
    // taps >= 1 always: vc_kernel's constructor rejects zero and even
    // dimensions, so the smallest legal kernel is 1x1 and taps - 1 >= 0.
    const std::uint64_t sum =
        std::uint64_t{extent} + std::uint64_t{taps} - 1U;

    if (sum > std::numeric_limits<vc::math::grid_dim>::max()) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            std::string("frequency_extent: padded ") + axis + " extent " +
                std::to_string(extent) + " + " + std::to_string(taps) +
                " - 1 does not fit in a grid dimension");
    }
    return static_cast<vc::math::grid_dim>(sum);
}

// The count must be computed in element_count (size_t), not in the uint32 the
// dimensions are. Same arithmetic grid2d::count() does internally, and exact
// for every representable pair for the reason vc_grid2d.h records.
vc::math::grid2d extent_of(vc::math::grid_dim w, vc::math::grid_dim h) {
    return vc::math::grid2d::checked(
        static_cast<vc::element_count>(w) * static_cast<vc::element_count>(h),
        w, h, "frequency_extent");
}

} // namespace

vc::math::grid2d frequency_extent(const vc::vc_image& src,
                                  const vc_kernel& kernel,
                                  vc_edge_policy policy) {
    // A switch over ALL FOUR enumerators, not an `if` guard followed by one
    // computation. The two supported policies need DIFFERENT extents, so a
    // single guard that admits both and then computes one of them can only
    // ever be right for one -- and a fifth policy added later becomes a
    // compiler warning here instead of silently falling into whichever
    // branch happened to be last.
    switch (policy) {
    case vc_edge_policy::wrap:
        // No padding. wrap is what the DFT already does, so the transform
        // happens at the source extent and the result needs no crop.
        return extent_of(src.width(), src.height());

    case vc_edge_policy::zero:
        // The kernel reaches kw-1 along x, so that many empty slots are
        // needed before the periodic copies can touch -- hence W + kw - 1,
        // which is a buffer width rather than a formula. Per axis, because a
        // 1xN kernel pads one axis and not the other.
        return extent_of(padded_axis(src.width(), kernel.width(), "x"),
                         padded_axis(src.height(), kernel.height(), "y"));

    case vc_edge_policy::clamp:
    case vc_edge_policy::reflect:
        // Not a missing feature. The DFT's basis functions are periodic, so a
        // spectrum can only describe a signal that tiles the plane; there is
        // no spectrum whose inverse transform replicates an edge pixel or
        // mirrors about it. Substituting wrap would differ only in a few
        // border pixels, which is the hardest kind of wrong to notice.
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "frequency_extent: the DFT can express only wrap and zero; "
            "clamp and reflect have no frequency-domain equivalent");
    }

    // Unreachable for a valid enumerator, and deliberately not a `default:`
    // above -- a default would suppress the warning that is the point of the
    // switch. This catches a value cast in from outside the enumeration.
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "frequency_extent: unknown edge policy");
}

vc::vc_image convolve_frequency(const vc::vc_image& src,
                                const vc_kernel& kernel,
                                vc_edge_policy policy) {
    (void)src;
    (void)kernel;
    (void)policy;
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "convolve_frequency: not implemented");
}

} // namespace vc::pixelops
