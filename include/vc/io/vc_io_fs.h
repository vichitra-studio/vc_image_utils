// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <fstream>
#include <string_view>

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

// Does `p` name an existing, readable regular file? A plain, noexcept query
// — require_file_exists() below is the throwing check most readers actually
// want.
[[nodiscard]] bool file_exists(const path& p) noexcept;

// Throws vc::vc_exception(file_not_found) unless `p` names an existing,
// readable regular file — the check every reader (stb_image_reader::read,
// load_edit_document, ...) needs before attempting to decode, so a missing/
// unreadable input reports file_not_found instead of the same decode_error a
// corrupt-but-present file would produce. `caller` is folded into the
// message (matching every other throw site in this file) so a failure still
// names which reader tripped it.
void require_file_exists(const path& p, std::string_view caller);

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

// Create `p`'s parent directory (a no-op if `p` has no parent, e.g. a bare
// filename) — the ensure_directory()-before-writing prep every writer
// (stb_image_writer::write, save_edit_document, ...) needs, factored out so
// it is written once rather than re-derived (parent_path() + empty-check +
// ensure_directory()) at each call site.
void ensure_parent_directory(const path& p);

// Open `p` for writing (truncating any existing content), throwing
// vc::vc_exception(encode_error) if the stream fails to open — the same
// open-or-throw check any std::ofstream-based writer needs (image I/O goes
// through stb's own C API and its integer return codes instead, so this has
// no caller there today; save_edit_document is the first of what is meant to
// be several text/binary writers built directly on std::ofstream).
[[nodiscard]] std::ofstream open_for_write(const path& p,
                                           std::string_view caller);

// Open `p` for reading, throwing vc::vc_exception(file_not_found) if the
// stream fails to open. A companion to require_file_exists(): that call
// catches the common case (the path does not exist at all) with a clean
// message; this one catches the rarer case where a path that DOES exist
// still can't be opened (permissions, a race between the two checks, ...) —
// distinct failure, same error code (file_not_found's own comment already
// covers "process lacks read permission").
[[nodiscard]] std::ifstream open_for_read(const path& p,
                                          std::string_view caller);

// Write `content` to `p` corruption-safely: write to a uniquely-named temp
// file beside `p` (same directory, so the final rename stays on one
// filesystem — a cross-filesystem rename is not atomic and may fail
// outright), flush it, then atomically rename it onto `p`. Failure at any
// point before the rename — including a `!out` write failure and the rename
// itself — leaves whatever was previously at `p` untouched and removes the
// temp file rather than leaving it behind; there is no window where `p` is
// truncated or half-written. NOTE: `flush()` only drains the C++ stream
// buffer into the OS; there is no `fsync`/`fdatasync` in this chain, so this
// is not a durability (survives-a-crash) guarantee — only the
// corruption-safety described above. Throws vc::vc_exception(encode_error)
// on failure. Caller is still responsible for ensure_parent_directory(p)
// first, matching every other writer in this file.
void write_file_atomically(const path& p,
                           std::string_view content,
                           std::string_view caller);

} // namespace vc::io
