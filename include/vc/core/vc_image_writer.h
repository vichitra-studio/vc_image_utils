// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_info.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"

namespace vc {

// seal() produces a vc_image;
// This header and vc_image.h are mutually dependent by design — see the
// comment beside vc_image.h's #include "vc/core/vc_image_writer.h" for the full
// rationale (why the cycle exists, why it's harmless, and when it would stop
// being two files).
class vc_image;

// The MUTABLE, under-construction form of an image — and the ONLY type in the
// system with write access to image pixels. Its single responsibility is to
// build an image's pixels and then seal them into an immutable vc_image.
//
// Move-only by design: a writer is uniquely owned while it is being filled, so a
// half-written image can never be shared or aliased — the "shared + mutable"
// hazard is simply not representable, and a downstream stage that receives a
// vc_image can never mutate it. Its
// lifecycle ENDS at seal(): the pixels move out, const-qualified, into a
// vc_image, and the writer is spent.
//
// The writer owns nothing clever: it composes a vc_image_info (geometry,
// indexing) with a vc_pixel_buffer (storage, typing, the size invariant) and
// adds exactly the two things those don't have — mutation and the one-way seal.
class vc_image_writer {
  public:
    // Allocate a writable image of the given geometry. The dtype is inferred
    // from the fill value's type (mirrors vc_pixel_buffer's ctor — no separate
    // enum that could disagree). Every element starts at `fill`.
    //   vc_image_writer w{width, height, channels, vc::buf_f32{0.0f}};
    template <vc_pixel_element_req T>
    vc_image_writer(image_dim width,
                    image_dim height,
                    channel_count channels,
                    T fill)
        : meta_(validated(width, height, channels)),
          pixels_(
              std::make_shared<vc_pixel_buffer>(meta_.element_count(), fill)) {
    }

    // Move-only: uniquely owned during construction, never copied or shared.
    vc_image_writer(vc_image_writer&&) noexcept = default;
    vc_image_writer& operator=(vc_image_writer&&) noexcept = default;
    vc_image_writer(const vc_image_writer&) = delete;
    vc_image_writer& operator=(const vc_image_writer&) = delete;

    // ---- geometry (read-only; delegates to the composed descriptor) ----
    const vc_image_info& meta() const noexcept {
        return meta_;
    }
    image_dim width() const noexcept {
        return meta_.width();
    }
    image_dim height() const noexcept {
        return meta_.height();
    }
    channel_count channels() const noexcept {
        return meta_.channels();
    }
    std::size_t pixel_count() const noexcept {
        return meta_.element_count();
    }

    // Attach a ready-built i_image_meta to this image's descriptor before
    // seal(). Written plumbing, not a rep: the writer composes a READY
    // vc_image_info and never parses EXIF itself — some loader/backend
    // builds the i_image_meta and hands it in here. Forwards to
    // vc_image_info::set_metadata().
    //
    // REJECTS null. "No metadata" (a mask — see vc_image_info's metadata_
    // member) remains fully reachable, but as the DEFAULT rather than as an
    // argument: a caller that wants a mask simply never calls this. What is
    // gone is the second spelling of that same state, where set_metadata(p)
    // with an accidentally-empty p silently produced a mask instead of the
    // metadata-carrying image the caller believed it was building — the loader
    // handing p in is exactly where an empty shared_ptr comes from. One way to
    // say "no metadata", and it is the way you cannot reach by accident.
    //
    // Not noexcept, and it cannot be: a throw from a noexcept function calls
    // std::terminate rather than unwinding.
    void set_metadata(const_image_meta_ptr metadata) {
        if (!metadata) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                "vc_image_writer::set_metadata(): metadata must not be null; "
                "leave it unset for an image with no metadata");
        }
        meta_.set_metadata(std::move(metadata));
    }

    // ---- write access (this type's reason to exist) ----

    // Typed 2D element write. T must match the buffer's dtype, else as<T>()
    // throws vc::vc_exception — the same contract as vc_pixel_buffer. Scattered
    // writes use at(); a hot loop normally takes pixels<T>() below and indexes
    // via meta().index(...).
    template <vc_pixel_element_req T>
    T& at(image_dim x, image_dim y, channel_count ch) {
        assert(pixels_ && "vc_image_writer used after seal() (spent writer)");
        return pixels_->as<T>()[meta_.index(x, y, ch)];
    }

    // Whole-buffer typed span — the fast path for pixel loops. Mutable: this is
    // the writer. CAUTION: the returned span is a non-owning view that stays
    // valid only until seal() — see the pixels_ member comment below and
    // with_pixels() just after this. Prefer with_pixels() unless the span
    // genuinely needs to outlive this call (e.g. handing it to a callee that
    // returns before this writer is sealed); pixels() exists for that case and
    // for callers already structured around holding the span themselves.
    template <vc_pixel_element_req T> std::span<T> pixels() {
        assert(pixels_ && "vc_image_writer used after seal() (spent writer)");
        return pixels_->as<T>();
    }

    // Scoped pixel access — the PREFERRED accessor for hot loops. Calls
    // f(std::span<T>{...}) and returns nothing, which removes the common
    // accidental-retention path: there is no returned handle for a caller to
    // casually assign into a variable that outlives this call, the way
    // `auto px = writer.pixels<T>();` invites. That is a real ergonomic win
    // over pixels()/at() above, whose whole return value IS such a handle.
    // It is NOT a structural guarantee, though: a lambda that captures a
    // reference (`[&]`) can still copy the span out through that capture,
    // and the underlying hazard is unchanged either way — any span retained
    // past seal(), by whatever path, still aliases the sealed image's buffer
    // (see the pixels_ member comment below for why).
    template <vc_pixel_element_req T, typename F> void with_pixels(F&& f) {
        assert(pixels_ && "vc_image_writer used after seal() (spent writer)");
        std::forward<F>(f)(pixels_->as<T>());
    }

    // Consume this writer and hand back an immutable vc_image over the SAME
    // pixels — no copy. The buffer is MOVED and its element type qualified to
    // const (shared_ptr<vc_pixel_buffer> -> shared_ptr<const vc_pixel_buffer>).
    // Rvalue-qualified because sealing spends the writer:
    //   vc_image img = std::move(w).seal();
    // After this the writer is spent and must not be touched again: it holds a
    // null buffer, so at()/pixels() would dereference null, a second seal()
    // would mint an image with null pixels, and any span/ref obtained earlier is
    // now a view into the sealed image (see the pixels_ note below).
    [[nodiscard]] vc_image seal() &&;

  private:
    // Validate the requested geometry, then hand back the descriptor the member
    // init list stores. Kept as a static so it can run in the init list, before
    // pixels_ is allocated.
    static vc_image_info
    validated(image_dim width, image_dim height, channel_count channels);

    vc_image_info meta_;
    // Mutable and uniquely held (use_count == 1) until seal() moves it out, so
    // the move is a cheap ownership transfer, not a copy. CAUTION: the spans/
    // refs handed out by pixels<T>()/at<T>() are NON-OWNING views into this
    // buffer; seal() moves the shared_ptr but NOT the buffer (it keeps its
    // address, now owned const by the vc_image), so a view RETAINED across
    // seal() still aliases — and could mutate — the sealed immutable image.
    // Moving the shared_ptr does not help either: moving a std::vector
    // transfers the same heap block, so an escaped span stays valid (and
    // stays a live back door into the sealed image) either way — this is a
    // view-lifetime hazard, not an ownership one, and no amount of smart-
    // pointer bookkeeping fixes it. with_pixels() (above) removes the
    // accidental path — there is no returned handle to assign into an
    // outliving variable — but a caller that deliberately captures a
    // reference can still smuggle the span out through it, so this remains
    // a convention, not a structural guarantee. Same discipline as any view
    // into a moved-from object: do not use a writer-derived span/ref after
    // seal(). (use_count alone does not enforce this — it governs the
    // shared_ptr, not view lifetimes.)
    pixel_buffer_ptr pixels_;
};

} // namespace vc
