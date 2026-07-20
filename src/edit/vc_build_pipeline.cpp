// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_build_pipeline.h"

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

vc::pipe::vc_pipeline build_pipeline(const vc_edit_session& session,
                                     const vc_render_request& request) {
    // TODO(you): the settings->graph assembly rep.
    //   SPINE version: build a vc::pipe::vc_pipeline, add() one
    //   vc::pipe::vc_passthrough_stage, and return the pipeline — its `in`/`out`
    //   slots become the graph's open input/output. Later: read
    //   session.edits() NARROW SLICES, instantiate the stages each tool
    //   implies (via vc_stage_registry), wire them, and derive each stage's config;
    //   append the terminal display transform per `request` output target
    //   ([LATER]). `request`'s resolution/ROI is inert until stages are
    //   resolution-aware.
    //
    // Throwing shell (NOT return {}): a spec test can only observe a built
    // pipeline through run(), an unimplemented rep — a throwing shell isolates
    // the red to build_pipeline. Reuses invalid_argument per the shared
    // error-model decision; no new enumerator is added.
    (void)session;
    (void)request;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "build_pipeline not yet implemented");
}

} // namespace vc::edit
