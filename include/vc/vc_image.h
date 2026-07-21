// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <utility>

#include "vc/vc_image_info.h"
#include "vc/vc_image_writer.h"
#include "vc/vc_types.h"

namespace vc {

// An immutable, shareable image — the value that flows between pipeline stages.
// It is uniformly READ-ONLY: there is no mutable pixel accessor anywhere on this
// type, and it holds its pixels as a shared_ptr<const vc_pixel_buffer>, so once
// an image exists nothing can change it (docs/coding_guidelines.md Sec 4.2). A
// copy is a cheap refcount bump that shares the same const buffer — safe by
// construction, so images are passed by value through vc_pipe_packet without a
// deep copy.
//
// Pixels are produced by filling a vc_image_writer and sealing it; that seal is
// the single construction path — zeros()/with_fill() below are named factories
// that go through the very same writer, not a second path. The image composes a
// vc_image_info descriptor (geometry now, the vc_image_spec seed later) exposed
// via meta(), plus the const buffer.
class vc_image {
  public:
    // Named, explicit-intent factories. A single (w, h, c) CONSTRUCTOR read as
    // general-purpose while only ever producing zeros — a quiet mismatch
    // between what it looked like and what it did. Two named factories say
    // exactly what content you get, and both funnel through with_fill() ->
    // vc_image_writer -> seal(), so validation/allocation logic lives in
    // exactly one place (vc_image_writer::validated()).

    // A valid, IMMUTABLE image of the given geometry, every element T{0}. T is
    // constrained by vc_pixel_element, same as with_fill() below — the caller
    // picks the dtype instead of it being hardcoded to buf_f32.
    template <vc_pixel_element T>
    [[nodiscard]] static vc_image
    zeros(image_dim width, image_dim height, channel_count channels) {
        return with_fill(width, height, channels, static_cast<T>(0));
    }

    // A valid, IMMUTABLE image of the given geometry, every element `fill`.
    // T is constrained by vc_pixel_element (vc_pixel_buffer.h) — the SAME
    // concept vc_pixel_buffer's and vc_image_writer's constructors are
    // constrained by, so an unsupported fill type (anything other than
    // buf_f32/buf_u8/buf_u16) is a compile error with a clear
    // constraints-not-satisfied diagnostic. The concept IS the compile-time
    // type check here — no separate static_assert needed.
    template <vc_pixel_element T>
    [[nodiscard]] static vc_image with_fill(image_dim width,
                                            image_dim height,
                                            channel_count channels,
                                            T fill) {
        return vc_image_writer(width, height, channels, fill).seal();
    }

    // ---- descriptor ----

    // The image's descriptor as a value — the thing the pipeline matches and
    // propagates. New descriptor fields become reachable here with no change to
    // vc_image itself.
    const vc_image_info& meta() const noexcept {
        return meta_;
    }

    // Terse forwarders for the stable core (dimensions never change shape);
    // everything that grows is reached through meta().
    image_dim width() const noexcept {
        return meta_.width();
    }
    image_dim height() const noexcept {
        return meta_.height();
    }
    channel_count channels() const noexcept {
        return meta_.channels();
    }

    // Total element count in the buffer, independent of dtype.
    std::size_t pixel_count() const noexcept {
        return meta_.element_count();
    }

    // ---- pixels (read-only) ----

    // Read-only handle to the pixel buffer. Callers read typed data through
    // pixels()->as<T>() (span<const T>) and locate elements with
    // meta().index(x, y, ch). There is deliberately no mutable counterpart.
    const_pixel_buffer_ptr pixels() const noexcept {
        return pixels_;
    }

  private:
    // The one true minter: vc_image_writer::seal() moves its filled buffer in
    // here (qualified to const) alongside the descriptor. No other code can
    // construct an image from a raw buffer.
    friend class vc_image_writer;
    vc_image(vc_image_info meta, const_pixel_buffer_ptr pixels)
        : meta_(meta), pixels_(std::move(pixels)) {
    }

    vc_image_info meta_;
    const_pixel_buffer_ptr pixels_;
};

} // namespace vc
