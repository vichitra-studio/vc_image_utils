// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_edit_document_io.h"

#include "nlohmann/json.hpp" // third_party/, SYSTEM PRIVATE (like stb) — kept
                             // OUT of vc_edit_document_io.h; see that
                             // header's comment.
#include "vc/io/vc_io_fs.h"
#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

using edit_user_setting_doc = nlohmann::json;

// NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Type, members...) generates
// to_json()/from_json() straight from a member LIST — no hand-written
// per-field code, no separate schema mechanism. WITH_DEFAULT makes
// from_json() a TOTAL get: a member missing from the JSON (a document saved
// before that tool-slice/field existed) keeps the struct's own default-
// member-initializer value; an unknown extra member (JSON saved by newer
// code, read by older code) is silently ignored. Nesting composes for free
// via nlohmann's ADL — vc_edit_document's own line just names "capture" and
// "exposure" as members; each nested struct's OWN line is what actually
// walks that struct's fields, recursively, all the way down.
//
// Adding a new tool-slice as a later curriculum phase adds a stage costs
// exactly: (1) a new member on vc_edit_document (vc_edit_document.h), (2)
// that struct's own line here, (3) its name added to vc_edit_document's own
// line below. save_edit_document()/load_edit_document() never change.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(vc_capture_settings,
                                                bracket_count, ev_spacing)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(vc_exposure_settings, enabled,
                                                ev, black)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(vc_edit_document, version,
                                                capture, exposure)

void save_edit_document(const std::filesystem::path& path,
                        const vc_edit_document& doc) {
    vc::io::ensure_parent_directory(path);
    const edit_user_setting_doc j = doc; // ADL to_json, generated above
    // Durable, atomic write: a failure partway through never truncates or
    // corrupts a document that was already saved at `path` — see
    // vc::io::write_file_atomically's own comment.
    vc::io::write_file_atomically(path, j.dump(2), "save_edit_document");
}

vc_edit_document load_edit_document(const std::filesystem::path& path) {
    vc::io::require_file_exists(path, "load_edit_document");
    auto in = vc::io::open_for_read(path, "load_edit_document");

    edit_user_setting_doc j;
    try {
        in >> j;
        return j.get<vc_edit_document>(); // ADL from_json, generated above
    } catch (const edit_user_setting_doc::exception& e) {
        // Covers both a syntax error (parse_error) and a syntactically valid
        // but wrong-shaped document (type_error/out_of_range from get<>()) —
        // both are "not a valid vc_edit_document", the one decode_error case
        // this function documents.
        throw vc::vc_exception(vc::vc_error_code::decode_error,
                               "load_edit_document: " + path.string() + ": " +
                                   e.what());
    }
}

} // namespace vc::edit
