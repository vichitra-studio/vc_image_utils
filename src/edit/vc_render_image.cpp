// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_render_image.h"

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

vc::vc_image render_image(const vc_edit_session& session,
                          const vc_render_request& request) {
    // TODO(you): the render-composition rep. See the header for what this
    // needs to do (build_pipeline + run(), feeding/harvesting the SPINE's
    // one open input/output) and the port-discovery caveat.
    //
    // Throwing shell (NOT a default-constructed vc_image — vc_image has no
    // public default constructor anyway, see vc_image.h): build_pipeline()
    // itself still throws today, so this would fail transitively regardless;
    // an explicit throw here keeps the failure message specific to this rep.
    (void)session;
    (void)request;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "render_image not yet implemented");
}

} // namespace vc::edit
