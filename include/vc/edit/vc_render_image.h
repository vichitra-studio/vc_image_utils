// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/edit/vc_edit_session.h"
#include "vc/edit/vc_render_request.h"
#include "vc/pipe/vc_render_context.h"
#include "vc/vc_image.h"

namespace vc::edit {

// The spine diagram (docs/edit_model_scaffold_plan.md Sec 3), made callable:
// build_pipeline(session, request) -> vc_pipeline -> run() -> vc_image,
// composed here ONCE. Both a future preview path and export_image
// (vc_export.h) call this rather than re-deriving the composition
// themselves — export owns WHERE the pixels go (a file), never how they are
// produced.
//
// Calls build_pipeline(session, request) to get a vc_built_graph (its
// `pipeline` plus its already-populated `inputs` map — build_pipeline wired
// the graph, so it alone knows the ports; see vc_build_pipeline.h), run()s
// the pipeline with that map and `run_context` (vc_pipeline::run() checks
// `run_context` for cancellation between stages), requires exactly one
// entry in the returned output map (throwing vc::vc_exception otherwise —
// a render graph produces exactly one image, never zero or several), and
// returns that entry's image.
//
// `run_context` (trailing, defaulted) is what lets a caller cancel an
// in-flight render — it is forwarded, unmodified, straight into
// vc_pipeline::run()'s own `run_context` parameter. Defaulted so every
// existing call site is unaffected; a caller that wants cancellation
// (e.g. an interactive preview: drag => cancel in-flight => restart) holds
// a vc_cancellation_source, obtains its vc_cancellation_token, wraps it in a
// vc_render_context, and passes that here.
vc::vc_image render_image(const vc_edit_session& session,
                          const vc_render_request& request,
                          const vc::pipe::vc_render_context& run_context = {});

} // namespace vc::edit
