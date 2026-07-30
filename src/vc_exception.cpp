// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/vc_exception.h"

#include <utility>

namespace vc {

vc_exception::vc_exception(vc_error_code code, vc::utils::message message)
    : code_(code),
      message_(std::make_shared<vc::utils::message>(std::move(message))) {
}

} // namespace vc
