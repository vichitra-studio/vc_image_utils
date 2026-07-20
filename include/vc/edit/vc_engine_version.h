// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

namespace vc::edit {

// The render-path version, recorded so an edit reproduces the same net
// render. This is a SEPARATE axis from the document version
// (vc_edit_document::version) and from per-stage module versions: a
// reproduction target selects a matching render path or upgrades knowingly.
//
// Reserved; there is no migration logic now ([LATER]). Per-stage
// module-version (darktable module_version) is also [LATER] — it has no home
// until a stage params struct exists (a user rep not yet scaffolded), so it
// is not reserved here.
inline constexpr int render_engine_version = 1;

} // namespace vc::edit
