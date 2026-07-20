// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

namespace vc::edit {

// The saveable, structured record of edit settings. Plain, typed, user-facing
// data — NOT stage param structs. It is organised by user-facing tool (one
// member per tool), the RawTherapee ProcParams shape MINUS the whole-struct
// coupling: stages receive a NARROW SLICE of this document, never the whole
// thing, which keeps the cache key precise and each stage decoupled.
//
// It is what undo/redo snapshots, what portability bundles, what the UI binds
// to, and what build_pipeline reads slices from to derive stages. There are
// no reps here — it is data; the reps are the DERIVATIONS in build_pipeline
// that read these slices. Serialization is hand-written JSON, added later;
// nothing here is serialized yet.

// One HOME for a cross-cutting value many stages read. bracket_count lives
// here ONCE rather than being threaded per-stage; build_pipeline reads it to
// decide how many align stages exist (bracket_count => N aligns).
struct vc_capture_settings {
    int bracket_count = 5;
    double ev_spacing = 2.0;
};

// One member per user-facing tool. A representative first tool slice; the
// document grows ADDITIVELY as stages arrive.
struct vc_exposure_settings {
    bool enabled = true;
    double ev = 0.0;
    double black = 0.0;
};

// The document itself. `version` is doc_version, reserved for migration; no
// migration logic exists yet ([LATER]). The engine/process version is a
// SEPARATE axis and lives on render_engine_version (vc_engine_version.h), not
// here.
struct vc_edit_document {
    int version = 1;                // doc_version — reserved, unused now
    vc_capture_settings capture;    // cross-cutting values (bracket_count, ...)
    vc_exposure_settings exposure;  // one member per tool; grows additively
    // more tool slices added as stages arrive; hand-written JSON later.
};

} // namespace vc::edit
