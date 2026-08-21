// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <utility>

#include "vc/core/vc_image_info.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_types.h"

// vc_image and vc_image_writer are mutually dependent BY DESIGN, not an
// accidental cycle: this header #includes vc_image_writer.h (the inline
// zeros()/with_fill() factories below construct a writer and call seal() to
// get their return value), while vc_image_writer.h only forward-declares
// vc_image (it needs the type to name seal()'s return type, but not its full
// definition) and vc_image itself declares `friend class vc_image_writer` so
// seal() reaches the private constructor below. Contrast with the analogous
// forward declaration in vc_image_info.h (~lines 11-25 there): that one is a
// ONE-WAY dependency (vc::edit depends on core, never the reverse), so a
// forward declaration there is a genuine, if currently untested, layering
// boundary — it would become load-bearing rather than just tidy if the
// edit/core split ever gets its own CMake targets. This pair is different in
// kind, not degree: the dependency runs BOTH ways (vc_image.h needs the
// writer's full definition to call seal(); vc_image_writer.h needs vc_image
// as a friend's return type), so there is no direction in which one could be
// built without the other — nothing is gained by trying to keep them
// physically separate. The immutability guarantee (Sec 4.2) is unaffected by
// this coupling either way — it comes from vc_image's public interface
// exposing no mutable accessor, not from which files include which. If
// `core` (the vc_image/vc_image_writer/vc_pixel_buffer cluster) is ever
// split into its own build target, these two would merge into one physical
// component rather than being split across the boundary.
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
    // constrained by vc_pixel_element_req, same as with_fill() below — the caller
    // picks the dtype instead of it being hardcoded to buf_f32.
    template <vc_pixel_element_req T>
    [[nodiscard]] static vc_image
    zeros(image_dim width, image_dim height, channel_count channels) {
        return with_fill(width, height, channels, static_cast<T>(0));
    }

    // A valid, IMMUTABLE image of the given geometry, every element `fill`.
    // T is constrained by vc_pixel_element_req (vc_pixel_buffer.h) — the SAME
    // concept vc_pixel_buffer's and vc_image_writer's constructors are
    // constrained by, so an unsupported fill type (anything other than
    // buf_f32/buf_u8/buf_u16) is a compile error with a clear
    // constraints-not-satisfied diagnostic. The concept IS the compile-time
    // type check here — no separate static_assert needed.
    template <vc_pixel_element_req T>
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

    // Read-only handle to the pixel buffer. Callers read typed data through
    // pixels()->as<T>() (span<const T>) and locate elements with
    // meta().index(x, y, ch). There is deliberately no mutable counterpart.
    const_pixel_buffer_ptr pixels() const noexcept {
        return pixels_;
    }

    // Typed 2D element READ, the mirror of vc_image_writer::at<T>(). Returns
    // `const T&` — there is still no mutable pixel accessor on this type, so
    // Sec 4.2's guarantee is unchanged; this only spares callers spelling out
    // pixels()->as<T>()[meta().index(...)] for a single element.
    template <vc_pixel_element_req T>
    const T& at(image_dim x, image_dim y, channel_count ch) const {
        return pixels_->as<T>()[meta_.index(x, y, ch)];
    }

    // Scoped read access — the counterpart to vc_image_writer::with_pixels<T>()
    // and, for the same reason, the PREFERRED accessor for hot loops.
    //
    // at<T>() above calls as<T>() per element, and as<T>() re-checks the
    // variant and materialises a fresh span every time. The stored dtype is
    // fixed at construction and the buffer is const, so that question cannot
    // change between two reads of one image — reading a million elements
    // through at<T>() re-answers it a million times. This answers it once, for
    // the whole loop, which is exactly the split the writer already makes:
    // at<T>() for scattered access, with_pixels<T>() when the access is a loop.
    //
    // The writer's retention caveat is weaker here but not absent: nothing can
    // mutate through a span<const T>, but a span outliving the vc_image that
    // owns the buffer still dangles, as any view does.
    template <vc_pixel_element_req T, typename F>
    void with_pixels(F&& f) const {
        std::forward<F>(f)(pixels_->as<T>());
    }

  private:
    // The one true minter: vc_image_writer::seal() moves its filled buffer in
    // here (qualified to const) alongside the descriptor. No other code can
    // construct an image from a raw buffer.
    friend class vc_image_writer;
    vc_image(vc_image_info meta, const_pixel_buffer_ptr pixels)
        : meta_(std::move(meta)), pixels_(std::move(pixels)) {
    }

    vc_image_info meta_;
    const_pixel_buffer_ptr pixels_;
};

} // namespace vc
