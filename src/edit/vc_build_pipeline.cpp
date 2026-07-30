// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_build_pipeline.h"

#include <memory>
#include <type_traits>
#include <utility>

#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::edit {

// Pinned the same way vc_edit_table.cpp pins its concept conformances:
// vc_built_graph must stay movable, since build_pipeline returns one by
// value and vc_pipeline (one of its two members) is move-only. NOTE: there
// is deliberately no companion `!is_copy_constructible_v<vc_built_graph>`
// assertion — std::vector<unique_ptr<i_pipe>> (vc_pipeline::stages_) makes
// vc_pipeline uncopyable in practice, but std::is_copy_constructible_v only
// checks that a copy-constructor SIGNATURE resolves, not that its body would
// actually instantiate; std::vector unconditionally declares that
// signature, so the trait reports `true` here regardless — asserting its
// negation would just be wrong, not merely redundant.
static_assert(std::is_move_constructible_v<vc_built_graph>);

vc_built_graph build_pipeline(const vc_edit_session& session,
                              const vc_render_request& request) {
    // `request` is currently inert: no stage is resolution-aware yet, so
    // level/ROI have nothing to influence. It becomes live once a
    // resolution-aware stage lands (see the class comment on
    // vc_render_request).
    (void)request;

    // SPINE: exactly one vc::pipe::vc_passthrough_stage, wired straight
    // through. Later reps read session.edits() NARROW SLICES and construct
    // real stages via vc_stage_registry instead of this direct construction.
    vc::pipe::vc_pipeline pipeline;
    const auto passthrough_name = pipeline.add(
        std::make_unique<vc::pipe::vc_passthrough_stage>("passthrough"));

    vc::pipe::render_io_map inputs;
    inputs.emplace(
        vc::pipe::stage_port{passthrough_name,
                             vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{session.source()});

    pipeline.validate();

    return vc_built_graph{.pipeline = std::move(pipeline),
                          .inputs = std::move(inputs)};
}

} // namespace vc::edit
