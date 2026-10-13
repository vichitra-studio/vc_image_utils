// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_laplacian.h"

namespace vc::pixelops {

vc_kernel make_laplacian() {
    return vc_kernel{
        3, 3, {0.0F, 1.0F, 0.0F, 1.0F, -4.0F, 1.0F, 0.0F, 1.0F, 0.0F}};
}

vc::vc_image laplacian(const vc::vc_image& src, vc_edge_policy policy) {
    return convolve(src, make_laplacian(), policy);
}

} // namespace vc::pixelops
