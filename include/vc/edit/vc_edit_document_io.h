// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <filesystem>

#include "vc/edit/vc_edit_document.h"

// Persist a vc_edit_document across sessions: you edit it in memory as you
// work through a phase, then save/load it as a file to carry that state to
// the next one.
//
// JSON (nlohmann) is an IMPLEMENTATION DETAIL, entirely confined to
// vc_edit_document_io.cpp — this header never mentions it, so a caller who
// just wants to save/load a document never pays for or sees the ~900KB
// nlohmann header. Adding a new tool-slice as a later curriculum phase adds
// a stage never touches this header at all — see vc_edit_document_io.cpp
// for the one line it needs there.
namespace vc::edit {

// Write `doc` to `path`, creating missing parent directories (mirrors
// vc::io::stb_image_writer::write()). Written durably and atomically
// (vc::io::write_file_atomically): a failure at any point never truncates
// or corrupts a document already saved at `path` — either the old content
// stays exactly as it was, or the new content lands there whole. Throws
// vc::vc_exception(encode_error) if the write fails or the file cannot be
// put in place.
void save_edit_document(const std::filesystem::path& path,
                        const vc_edit_document& doc);

// Read a document back from `path`. Throws vc::vc_exception(file_not_found)
// if `path` does not exist, or vc::vc_exception(decode_error) if it is not
// valid JSON, or if it is valid JSON but the wrong shape (e.g. a field holds
// the wrong type, or the top level isn't an object) — the same two-way
// file-not-found/decode_error split vc::io::stb_image_reader::read() uses
// (a missing file and a corrupt one are different failures worth telling
// apart). A well-formed file that is merely missing/extra keys relative to
// the current vc_edit_document shape is NOT an error: a field absent from an
// older save keeps its default, and an unknown extra field from a newer save
// is silently dropped.
vc_edit_document load_edit_document(const std::filesystem::path& path);

} // namespace vc::edit
