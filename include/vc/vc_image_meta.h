// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>

#include "vc/vc_types.h"

namespace vc {

// An image's descriptor: its geometry (dimensions) plus the element-index math
// those dimensions imply. Held by BOTH vc_image and vc_image_writer as a plain
// member (composition), so the layout/index/size logic lives in exactly ONE
// place — neither the read side nor the write side re-derives it, and a new
// descriptor field (a future color_space / channel_kind / layout — the
// vc_image_spec seed, docs/pipe_design.md Sec 5.3) is added here and reachable
// everywhere through vc_image::meta() / vc_image_writer::meta() with no edits to
// either owner.
//
// A value type: cheap to copy, comparable, and — crucially — a thing the
// pipeline can pass around and match BEFORE any pixels exist (spec propagation,
// Sec 5.4). That "descriptor is a standalone value" is exactly why it is
// composed and exposed, not privately inherited.
class vc_image_meta {
  public:
    vc_image_meta() = default;

    vc_image_meta(image_dim width, image_dim height, channel_count channels)
        : width_(width), height_(height), channels_(channels) {
    }

    image_dim width() const noexcept {
        return width_;
    }
    image_dim height() const noexcept {
        return height_;
    }
    channel_count channels() const noexcept {
        return channels_;
    }

    // Total element count for this geometry — the count a vc_pixel_buffer must
    // hold, and what vc_image::pixel_count() reports.
    std::size_t element_count() const noexcept {
        // TODO(you): width_ * height_ * channels_, overflow-safe — widen the
        // FIRST operand to std::size_t before the multiply (image_dim /
        // channel_count are uint32, so the product can exceed 32 bits before a
        // later cast would help). Until this is written it returns 0, so
        // vc_image_writer allocates an empty buffer and the "validates and
        // allocates" test stays red on purpose. See coding_guidelines.md Sec 6.3.
        return 0;
    }

    // Element offset of channel `ch` at pixel (x, y). THE layout decision, made
    // in one place: interleaved (…r,g,b,r,g,b…). A planar layout would change
    // only this function — nothing else in the codebase indexes pixels directly.
    //
    // PROVISIONAL (2026-07-16): interleaved, to match stb I/O (stb_image hands
    // back interleaved data). The pipeline's working buffers are planned PLANAR
    // per curriculum ARCH-3 — revisit and switch this to
    //   (static_cast<std::size_t>(ch) * height_ + y) * width_ + x
    // when planar working buffers actually arrive. Kept interleaved now because
    // no code depends on the layout yet and the first consumer is stb I/O.
    std::size_t index(image_dim x, image_dim y, channel_count ch) const noexcept {
        return (static_cast<std::size_t>(y) * width_ + x) * channels_ + ch;
    }

    bool operator==(const vc_image_meta&) const noexcept = default;

  private:
    image_dim width_ = 0;
    image_dim height_ = 0;
    channel_count channels_ = 0;
};

} // namespace vc
