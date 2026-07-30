// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/edit/vc_edit_session.h"
#include "vc/edit/vc_render_request.h"
#include "vc/pipe/vc_pipeline.h"

namespace vc::edit {

// Bundles a built vc_pipeline together with the packets to feed its open
// inputs. build_pipeline knows those ports BY CONSTRUCTION — it is the one
// that wired them — so no caller needs a port-query API on vc_pipeline
// (which does not exist) to feed/harvest the graph correctly; render_image
// just run()s `pipeline` with `inputs` and reads back run()'s open-output
// map. A plain aggregate with public fields: there is no invariant between
// the two members to defend (feeding `inputs` into a DIFFERENT pipeline
// would be a caller bug, not a state this type needs to prevent), so
// accessors would be pass-throughs guarding nothing — the same reasoning
// already applied to vc_render_request and vc_export_config.
//
// vc_pipeline is MOVE-ONLY (it holds `std::vector<std::unique_ptr<i_pipe>>`
// stages, so copying it is ill-formed in practice — see the static_assert
// note in vc_build_pipeline.cpp for why that is NOT the same thing as
// `std::is_copy_constructible_v` reporting false); vc_built_graph has no
// user-declared special members, so its move constructor is implicitly
// generated and moves `pipeline` + `inputs` member-wise — this struct is
// therefore itself movable, and build_pipeline can return one by value
// (mandatory copy elision for the returned prvalue, or a move for a named
// local, either way no copy of `pipeline` is ever needed).
struct vc_built_graph {
    vc::pipe::vc_pipeline pipeline;
    vc::pipe::render_io_map inputs; // keyed by stage_port; already populated
                                    // onto the graph's open input ports
};

// The translation layer: the single place that speaks BOTH vocabularies —
// user settings <-> machine graph. It reads NARROW SLICES from the session,
// decides which stages exist (including paramless plumbing stages a tool
// depends on — e.g. deghost => align + compute_mask), how many
// (bracket_count => N aligns), wires them, and derives each stage's config.
// Structural/dependency knowledge lives HERE, imperatively (RawTherapee
// ImProcCoordinator) — legitimate for a fixed pipeline; a stage cannot decide
// how many of itself exist. It is CHEAP and rebuilt on demand: the pipeline
// is DISPOSABLE — never where edits live. `request` selects the render
// destination (resolution/ROI, inert now; and the terminal display transform
// appended per output target — [LATER]).
//
// SPINE (current rep, vc_build_pipeline.cpp): a single
// vc::pipe::vc_passthrough_stage, `inputs` populated with session.source()
// on that stage's open input port (slots::in), pipeline.validate() called
// before returning — so a vc_built_graph that exists is guaranteed to be a
// VALIDATED graph with its inputs already populated; no add/connect of a
// second stage is needed. render_image() runs the returned graph end to
// end: vc_edit_session -> build_pipeline -> run() -> vc_image.
//
// Later reps read session.edits() slices and build real stages, keyed
// through the vc_stage_registry (vc_stage_registry.h) instead of this
// direct construction.
vc_built_graph build_pipeline(const vc_edit_session& session,
                              const vc_render_request& request);

} // namespace vc::edit
