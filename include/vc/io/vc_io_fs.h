// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/io/vc_io_types.h"

namespace vc::io {

// Small, vc::io-scoped filesystem helpers — not a general-purpose wrapper
// around std::filesystem. Their reason to exist is the gap read()/write()/
// write_dump() each hit independently: std::filesystem's own functions
// report failure as std::filesystem::filesystem_error (or a
// std::error_code), a different currency than this project's single
// exception type (docs/coding_guidelines.md Sec 5.1). The two query
// functions below stay noexcept — a filesystem check that can't answer is
// "false", not a crash; the one function with a real failure to report
// (ensure_directory) throws vc::vc_exception like everything else here.
//
// Named ensure_directory, not create_directory: std::filesystem::path is
// an associated type of namespace std::filesystem, so an unqualified call
// with a path argument is subject to ADL — and std::filesystem already
// declares its own create_directory(const path&) (single-level, non-
// recursive; different semantics from this function). Reusing that name
// would make every call site ambiguous unless fully qualified every time;
// a distinct name sidesteps the collision entirely rather than relying on
// callers to remember to qualify it.

// Does `p` name an existing, readable regular file? Meant to be called by
// read() before stbi_load(), so a missing/unreadable input reports
// vc::vc_error_code::file_not_found instead of the same decode_error a
// corrupt-but-present file would produce. That error code is already
// declared (vc_error_code.h) and already has a to_string() mapping and a
// pinning test — it is just never thrown anywhere yet.
[[nodiscard]] bool file_exists(const path& p) noexcept;

// Does `p` name an existing directory? A plain existence check — not
// currently called anywhere in this codebase (write() calls
// ensure_directory() directly and unconditionally, since creating is
// already idempotent and needs no pre-check), kept for a future call site
// that wants to know before deciding what to do, without paying for a
// throw/catch just to find out.
[[nodiscard]] bool directory_exists(const path& p) noexcept;

// Create `p`, including any missing parent directories — same semantics as
// std::filesystem::create_directories: safe to call even if `p` already
// exists (a no-op in that case), so callers do not need to pair this with
// directory_exists() first. Throws vc::vc_exception(encode_error, ...) on
// failure — encode_error's existing comment ("output could not be written:
// bad path, disk full, permissions") already covers directory-creation
// failure the same way.
void ensure_directory(const path& p);

} // namespace vc::io
