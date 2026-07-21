// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_export.h"

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

void export_image(const vc_edit_session& session,
                  const vc_export_config& config) {
    // TODO(you): the render-to-file rep. See the header for the two calls
    // this joins (render_image + vc::io::stb_image_writer::write).
    (void)session;
    (void)config;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "export_image not yet implemented");
}

} // namespace vc::edit
