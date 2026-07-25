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

void require_file_exists(const path& p, std::string_view caller) {
    if (!file_exists(p)) {
        throw vc::vc_exception(vc::vc_error_code::file_not_found,
                               std::string(caller) + ": " + p.string() +
                                   ": file does not exist");
    }
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

void ensure_parent_directory(const path& p) {
    const auto parent = p.parent_path();
    if (!parent.empty()) {
        ensure_directory(parent);
    }
}

std::ofstream open_for_write(const path& p, std::string_view caller) {
    std::ofstream out(p);
    if (!out) {
        throw vc::vc_exception(vc::vc_error_code::encode_error,
                               std::string(caller) + ": failed to open " +
                                   p.string() + " for writing");
    }
    return out;
}

std::ifstream open_for_read(const path& p, std::string_view caller) {
    std::ifstream in(p);
    if (!in) {
        throw vc::vc_exception(vc::vc_error_code::file_not_found,
                               std::string(caller) + ": failed to open " +
                                   p.string() + " for reading");
    }
    return in;
}

} // namespace vc::io
