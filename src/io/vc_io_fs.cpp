// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/io/vc_io_fs.h"

#include <filesystem>
#include <system_error>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::io {

bool file_exists(const path& p) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

bool directory_exists(const path& p) noexcept {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

void ensure_directory(const path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec) {
        throw vc::vc_exception(vc::vc_error_code::encode_error,
                               "vc::io::ensure_directory: " + p.string() +
                                   ": " + ec.message());
    }
}

} // namespace vc::io
