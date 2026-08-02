// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/core/vc_error_code.h"

#include <string>

#include "vc/core/vc_exception.h"

namespace vc {

int to_int(vc_error_code code) noexcept {
    return static_cast<int>(code);
}

vc_error_code to_error_code(int value) {
    switch (static_cast<vc_error_code>(value)) {
    case vc_error_code::file_not_found:
    case vc_error_code::invalid_format:
    case vc_error_code::decode_error:
    case vc_error_code::encode_error:
    case vc_error_code::invalid_argument:
    case vc_error_code::stage_not_found:
    case vc_error_code::stage_already_added:
    case vc_error_code::slot_not_found:
    case vc_error_code::slot_already_declared:
    case vc_error_code::pipe_connection_mismatch:
    case vc_error_code::pipe_invalid_topology:
    case vc_error_code::pipe_input_already_connected:
    case vc_error_code::user_cancelled:
        return static_cast<vc_error_code>(value);
    }
    throw vc_exception(vc_error_code::invalid_argument,
                       "to_error_code: value " + std::to_string(value) +
                           " does not map to a known vc_error_code");
}

vc::utils::string to_string(vc_error_code code) noexcept {
    switch (code) {
    case vc_error_code::file_not_found:
        return "file_not_found";
    case vc_error_code::invalid_format:
        return "invalid_format";
    case vc_error_code::decode_error:
        return "decode_error";
    case vc_error_code::encode_error:
        return "encode_error";
    case vc_error_code::invalid_argument:
        return "invalid_argument";
    case vc_error_code::stage_not_found:
        return "stage_not_found";
    case vc_error_code::stage_already_added:
        return "stage_already_added";
    case vc_error_code::slot_not_found:
        return "slot_not_found";
    case vc_error_code::slot_already_declared:
        return "slot_already_declared";
    case vc_error_code::pipe_connection_mismatch:
        return "pipe_connection_mismatch";
    case vc_error_code::pipe_invalid_topology:
        return "pipe_invalid_topology";
    case vc_error_code::pipe_input_already_connected:
        return "pipe_input_already_connected";
    case vc_error_code::user_cancelled:
        return "user_cancelled";
    }
    return "unknown_error_code";
}

} // namespace vc
