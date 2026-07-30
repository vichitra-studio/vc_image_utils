// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/io/vc_io_fs.h"

#include <filesystem>
#include <random>
#include <system_error>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::io {

namespace {

// Pick a temp path beside `p` that does not already exist — same directory
// as `p` (required for the later rename to be atomic), suffixed with a
// random token so two overlapping writers, or a stale leftover from an
// earlier crash, can't collide with THIS write's temp file. (The final
// destination `p` itself is never a collision risk: the whole point of the
// later rename is to replace whatever is there.)
path make_unique_temp_path(const path& p) {
    static std::mt19937_64 rng{std::random_device{}()};
    for (;;) {
        path candidate = p;
        candidate += ".tmp" + std::to_string(rng());
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        // Occupied (or the check itself failed) — try another random
        // suffix rather than risk colliding with it.
    }
}

} // namespace

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

void write_file_atomically(const path& p,
                           std::string_view content,
                           std::string_view caller) {
    const path temp = make_unique_temp_path(p);

    try {
        std::ofstream out = open_for_write(temp, caller);
        out << content;
        out.flush(); // Drains the C++ stream's own buffer into the OS now, so
                     // a write failure surfaces here rather than silently
                     // inside operator<<'s buffering. This is NOT fsync/
                     // fdatasync — it gives no guarantee against data loss on
                     // an OS crash or power failure (neither the temp file
                     // nor its parent directory is synced). What this
                     // function actually guarantees is corruption-safety —
                     // `p` is never left truncated or half-written — and
                     // that guarantee comes from the temp-file + atomic
                     // rename below, not from this flush(). Don't attribute
                     // an invariant to a gate that doesn't establish it
                     // (docs/coding_guidelines.md Sec 6.5).
        if (!out) {
            throw vc::vc_exception(vc::vc_error_code::encode_error,
                                   std::string(caller) + ": failed to write " +
                                       temp.string());
        }
    } catch (const vc::vc_exception&) {
        // open_for_write() and the !out check above are this block's only
        // documented failure surface (this project's single exception
        // currency, docs/coding_guidelines.md Sec 5.1) — catching that type
        // specifically, not `...`, keeps this explicit rather than silently
        // absorbing and cleaning up after some unrelated exception this
        // code was never meant to handle. Clean up the half-written temp
        // file, then rethrow unchanged (bare `throw;` preserves the caught
        // exception's exact type).
        std::error_code ec;
        std::filesystem::remove(temp, ec); // best-effort; ignore the result
        throw;
    } // close `out` before renaming — some platforms refuse to rename a
    // file that is still open.

    std::error_code ec;
    std::filesystem::rename(temp, p, ec);
    if (ec) {
        std::filesystem::remove(temp, ec); // best-effort cleanup
        throw vc::vc_exception(vc::vc_error_code::encode_error,
                               std::string(caller) + ": failed to replace " +
                                   p.string() + " with " + temp.string() +
                                   ": " + ec.message());
    }
    // rename() on success atomically repoints p's directory entry at the
    // new content and removes temp's — there is no separate check needed
    // to confirm temp is gone; that is rename's own postcondition.
}

} // namespace vc::io
