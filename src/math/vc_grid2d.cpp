// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/math/vc_grid2d.h"

#include <string>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace vc::math {

// Outlined into the .cpp rather than left inline in the header, for the
// reason vc_image_info gives for its own throw: the throw machinery would
// otherwise be emitted into every translation unit that includes the header,
// and this one is meant to be included widely.
grid2d grid2d::checked(vc::element_count count,
                       grid_dim width,
                       grid_dim height,
                       vc::utils::string_view caller) {
    if (width == 0 || height == 0) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               std::string(caller) +
                                   ": grid2d dimensions must both be non-zero, "
                                   "got width " +
                                   std::to_string(width) + " height " +
                                   std::to_string(height));
    }

    // Multiplying is safe here BECAUSE the operands are uint32 and the
    // product is formed in size_t -- see the overflow arithmetic in the
    // header. This is the one place in the library where that reasoning has
    // to hold, which is the point of putting it here.
    const vc::element_count expected = static_cast<vc::element_count>(width) *
                                       static_cast<vc::element_count>(height);
    if (count != expected) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            std::string(caller) + ": buffer holds " + std::to_string(count) +
                " elements but width " + std::to_string(width) + " x height " +
                std::to_string(height) + " needs " + std::to_string(expected));
    }

    return grid2d{width, height};
}

} // namespace vc::math
