// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/edit/vc_edit_session.h"
#include "vc/edit/vc_render_request.h"
#include "vc/pipe/vc_pipeline.h"

namespace vc::edit {

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
// TODO(you): the assembly is a learning rep. For the SPINE, assemble a single
// vc::pipe::vc_passthrough_stage and return the pipeline (its open input/output
// become the graph's open ports); no add/connect of a second stage is needed. So
// the moment this body plus the pipe-layer run()/validate() reps are written, the
// spine renders end-to-end: vc_edit_session -> build_pipeline -> run() -> vc_image.
// Later reps read session.edits() slices and build real stages, keyed through the
// vc_stage_registry (vc_stage_registry.h). The scaffold ships a THROWING shell (see
// vc_build_pipeline.cpp): vc_pipeline exposes no public stage accessor, so a spec
// test can only observe a built pipeline THROUGH run() (itself an unimplemented
// rep) — a throwing shell isolates the red to build_pipeline instead of
// entangling it with run().
vc::pipe::vc_pipeline build_pipeline(const vc_edit_session& session,
                                     const vc_render_request& request);

} // namespace vc::edit
