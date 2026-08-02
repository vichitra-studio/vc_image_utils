// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_render_image.h"

#include <string>
#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/edit/vc_build_pipeline.h"

namespace vc::edit {

vc::vc_image render_image(const vc_edit_session& session,
                          const vc_render_request& request,
                          const vc::pipe::vc_render_context& run_context) {
    auto built = build_pipeline(session, request);
    auto outputs = built.pipeline.run(std::move(built.inputs), run_context);

    // The SPINE has exactly one open output; a later, multi-stage graph
    // could still legitimately have several, but render_image's contract is
    // a single rendered image, so enforce that here rather than silently
    // picking one.
    if (outputs.size() != 1) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "render_image: expected exactly one open output, got " +
                std::to_string(outputs.size()));
    }

    return outputs.begin()->second.get<vc::vc_image>();
}

} // namespace vc::edit
