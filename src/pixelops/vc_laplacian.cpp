// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_laplacian.h"

#include <stdexcept>

namespace vc::pixelops {

vc_kernel make_laplacian() {
    // YOURS. A vc_kernel of width 3, height 3, with the nine weights of the
    // stencil you derived -- row-major, so the order is top row, middle row,
    // bottom row.
    throw std::logic_error("make_laplacian: not implemented");
}

vc::vc_image laplacian(const vc::vc_image& src, vc_edge_policy policy) {
    // YOURS, and it is one line. convolve() already does the work, already
    // resolves the boundary through `policy`, and already rejects a non-f32
    // source -- none of that is re-done here.
    (void)src;
    (void)policy;
    throw std::logic_error("laplacian: not implemented");
}

} // namespace vc::pixelops
