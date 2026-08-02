// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/vc_pipe_errors.h"

#include <string>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace vc::pipe {

void throw_if_cancelled(const vc_render_context& run_context) {
    if (run_context.cancelled()) {
        throw vc::vc_exception(vc::vc_error_code::user_cancelled,
                               "Operation cancelled by user");
    }
}

void throw_pipe_run_error(const std::string& message) {
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "vc_pipeline::run() " + message);
}

} // namespace vc::pipe
