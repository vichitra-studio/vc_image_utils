// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/edit/vc_edit_session.h"
#include "vc/edit/vc_render_request.h"
#include "vc/vc_image.h"

namespace vc::edit {

// The spine diagram (docs/edit_model_scaffold_plan.md Sec 3), made callable:
// build_pipeline(session, request) -> vc_pipeline -> run() -> vc_image,
// composed here ONCE. Both a future preview path and export_image
// (vc_export.h) call this rather than re-deriving the composition
// themselves — export owns WHERE the pixels go (a file), never how they are
// produced.
//
// TODO(you): build_pipeline(session, request) gives you the graph; feed
// session.source() to whatever open input the SPINE exposes and return the
// single open output's image. How a caller discovers those open ports in
// general is still open — vc_pipeline has no public port query today, and
// the SPINE's ports are only knowable because build_pipeline currently
// assembles exactly one vc_passthrough_stage (fixed slots::in/slots::out).
// A general mechanism (a port-query API on vc_pipeline, or build_pipeline
// returning pre-wired inputs alongside the pipeline) is a decision for
// whoever writes this body once it's actually needed — not pre-decided here.
vc::vc_image render_image(const vc_edit_session& session,
                          const vc_render_request& request);

} // namespace vc::edit
