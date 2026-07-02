// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/vc_error_code.h"

#include <string>

#include "vc/vc_exception.h"

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
    }
    return "unknown_error_code";
}

} // namespace vc
