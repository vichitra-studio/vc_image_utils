// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/core/vc_image_info.h"

#include <string>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace vc {

void vc_image_info::throw_out_of_range(std::string_view coord,
                                       std::size_t value,
                                       std::string_view bound_name,
                                       std::size_t bound) {
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "vc_image_info::index(): " + std::string(coord) +
                               " (" + std::to_string(value) +
                               ") out of range for " + std::string(bound_name) +
                               " " + std::to_string(bound));
}

} // namespace vc
