// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>

namespace vc::edit {

// The render REQUEST: it carries {target resolution / pyramid level, ROI} —
// the render DESTINATION, consumed by build_pipeline. It is deliberately NOT
// source metadata: the source image's vc_image_info describes ITS geometry,
// and a preview is a different image at a different resolution.
//
// The fields are DESIGNED now but INERT: no stage is resolution-aware yet and
// spatial params do not scale with the pyramid level, so a request never
// changes what a render produces today. Scale-aware param interpretation —
// the crux of preview<->export parity — is [LATER]. run() never sees a
// render request; only build_pipeline does.
struct vc_render_request {
    // Pyramid level: 0 = full resolution. The default (0) makes a preview
    // identical to an export until scale-aware params land.
    int level = 0;

    // Region of interest in the canonical reference frame. All-zero (the
    // default) means the whole image. [OPEN] the reference frame is not yet
    // pinned: this is a plain pixel rectangle PLACEHOLDER a later
    // coordinate-space decision can reshape — do not treat its shape as
    // settled.
    struct roi {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    } region;
};

} // namespace vc::edit
