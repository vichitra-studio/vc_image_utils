// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/edit/vc_edit_session.h"
#include "vc/io/vc_io_types.h"

namespace vc::edit {

// Everything export owns and nothing render_image already owns: WHERE the
// rendered image goes (path) and HOW it is encoded (write). Render
// destination (resolution/ROI) is deliberately absent — vc_render_request's
// fields are either a no-op today (no stage is resolution-aware yet) or an
// unsettled coordinate frame ([OPEN], vc_render_request.h), so there is
// nothing there yet for a caller to meaningfully set; export always renders
// at the default (full-res, whole image) until that lands for real.
struct vc_export_config {
    vc::io::path path;
    vc::io::write_config write;
};

// Render `session` (full-res, whole image — see vc_export_config above) and
// write the result to config.path. Owns no rendering logic of its own
// (render_image.h does that) and no encoding logic of its own (vc::io does
// that) — this is purely the join between the two.
//
// TODO(you): render_image(session, vc_render_request{}) gets the pixels;
// construct a vc::io::stb_image_writer and writer.write(config.path, image,
// config.write) puts them on disk. stb_image_writer is constructed HERE, not
// injected: it is vc::io's only real implementation and no caller chooses
// between two (the substitutability test, docs/edit_model.md Sec 7) — same
// convention main.cpp's read/write round trip already uses.
void export_image(const vc_edit_session& session,
                  const vc_export_config& config);

} // namespace vc::edit
