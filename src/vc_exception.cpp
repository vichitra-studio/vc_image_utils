// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/vc_exception.h"

#include <utility>

namespace vc {

// clang-format off
vc_exception::vc_exception(vc_error_code code,
                           vc::utils::message message)
    : code_(code), message_(std::move(message)) {}
// clang-format on

} // namespace vc
