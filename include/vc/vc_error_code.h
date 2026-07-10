// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>

#include "vc/utils/vc_strings.h"

namespace vc {

// Underlying type fixed to uint8_t: this enum is only ever compared and
// converted (to_int/to_error_code), never used in arithmetic — no
// promotion-warning risk, so the smaller type is free (see channel_count
// in vc_types.h for the opposite case, where arithmetic makes uint32_t
// the right choice).
enum class vc_error_code : std::uint8_t {
    file_not_found,   // path does not exist or process lacks read permission
    invalid_format,   // file header is not a recognised image format
    decode_error,     // format recognised but data is corrupt or unsupported
                      // variant
    encode_error,     // output could not be written (bad path, disk full,
                      // permissions)
    invalid_argument, // caller passed logically invalid data (zero dimensions,
                      // null buffer)
};

int to_int(vc_error_code code) noexcept;
vc_error_code
to_error_code(int value); // throws vc_exception if value is out of range

vc::utils::string to_string(vc_error_code code) noexcept;

} // namespace vc
