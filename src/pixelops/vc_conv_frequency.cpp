// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_conv_frequency.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/math/vc_dft.h"

#include <cmath>
#include <cstddef>
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

vc::math::grid2d frequency_extent(const vc::vc_image& src,
                                  const vc_kernel& kernel,
                                  vc_edge_policy policy) {
    (void)src;
    (void)kernel;
    (void)policy;
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "frequency_extent: not implemented");
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
