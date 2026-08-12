// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "vc/core/vc_types.h"

namespace vc {

// Forward-declared, not #included (both i_image_meta and vc_image_info are
// core — see vc_image_meta.h's own comment for why the interface lives here
// rather than in vc::edit): this is a plain compile-firewall forward
// declaration, the same technique vc_image.h uses for vc_image_writer. A
// forward declaration + shared_ptr<const T> is enough because shared_ptr's
// destructor/copy/comparison are all type-erased and do not need T complete,
// so vc_image_info stays a value type with no out-of-line special members
// even though i_image_meta is never #included here — a consumer that only
// holds/copies/compares the handle (most of vc_image_info's own clients)
// never pays for pulling in <optional>/<string>/vc_any.h; a consumer
// that actually calls get()/set() on it includes vc/vc_image_meta.h itself.
class i_image_meta;

// A shared, read-only handle onto a vc::i_image_meta — the composed-
// metadata field's exact type, spelled once here rather than at every
// accessor/parameter that needs it. Matches the const_pixel_buffer_ptr
// precedent in vc_types.h: a shared_ptr handle over a domain type gets a
// named alias instead of being spelled out repeatedly.
using const_image_meta_ptr = std::shared_ptr<const vc::i_image_meta>;

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
class vc_image_info {
  public:
    vc_image_info() = default;

    vc_image_info(image_dim width, image_dim height, channel_count channels)
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
        return static_cast<std::size_t>(width_) * height_ * channels_;
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
    //
    // THROWS on an out-of-range x/y/ch. A coordinate is caller input the same
    // way validated()'s width/height/channels are (vc_image_writer.cpp) — and
    // every other kind of invalid input in this library already fails loudly
    // (that geometry validation, vc_pixel_buffer::as<T>()'s dtype mismatch,
    // set_metadata()'s null check). Without this check a bad coordinate would
    // silently compute a bogus offset and vc_image_writer::at() would read or
    // write through it via std::span::operator[], which has no bounds check of
    // its own (span gained no checked accessor in C++20). Not noexcept for
    // that reason.
    //
    // This guards the coordinate path ONLY — it is not a bounds check on the
    // buffer. A caller that takes the whole span (vc_image_writer's
    // with_pixels<T>(), vc_image::pixels()->as<T>()) and indexes it directly
    // bypasses this entirely; span::operator[] is still unchecked there.
    //
    // NOTE: a default-constructed vc_image_info is 0x0x0, so EVERY coordinate
    // is out of range for it — index(0, 0, 0) throws rather than returning 0
    // as it did before the check existed. That is the intended reading (an
    // empty descriptor has no elements to address), not an oversight.
    std::size_t index(image_dim x, image_dim y, channel_count ch) const {
        if (x >= width_) {
            throw_out_of_range("x", x, "width", width_);
        }
        if (y >= height_) {
            throw_out_of_range("y", y, "height", height_);
        }
        if (ch >= channels_) {
            throw_out_of_range("ch", ch, "channels", channels_);
        }
        return (static_cast<std::size_t>(y) * width_ + x) * channels_ + ch;
    }

    bool operator==(const vc_image_info&) const noexcept = default;

    // ---- composed metadata ----
    //
    // The image's captured metadata (EXIF/IPTC/etc, vc::i_image_meta),
    // COMPOSED here rather than merged into the geometry above — physical
    // dimensions (width_/height_/channels_) always describe the BUFFER, and
    // stay independent of metadata like EXIF orientation that can make the
    // DISPLAY dimensions differ. Held as shared_ptr<const T>: a mask is
    // an image whose composed metadata is null (the default), and a real
    // image's metadata is immutable + shareable, matching how vc_image
    // itself shares its pixel buffer. vc_image_info stays a copyable VALUE
    // type — shared_ptr copies are cheap refcount bumps, not a deep clone.
    const const_image_meta_ptr& metadata() const noexcept {
        return metadata_;
    }

    // Setter: only vc_image_writer calls this (before seal()) — a
    // vc_image_info already inside a sealed vc_image is reached only via a
    // const accessor, so this is the writer's one write path onto the
    // descriptor, not a general public mutator: the image writer composes
    // a ready vc_image_info; it never parses EXIF itself — the caller
    // hands in an already-built i_image_meta.
    void set_metadata(const_image_meta_ptr metadata) noexcept {
        metadata_ = std::move(metadata);
    }

  private:
    // The one throw site behind index()'s three range checks — same shape as
    // vc_pixel_buffer::throw_on_mismatch<T>() (vc_pixel_buffer.h): the checks
    // stay as three separate `if`s (each needs its own comparison), but the
    // message-building/throw is written once instead of three times.
    //
    // Declared here, DEFINED in vc_image_info.cpp: this is the rare/error
    // path, not the common-case comparisons above, so it does not need to be
    // inline. Keeping it out-of-line means vc_error_code.h/vc_exception.h and
    // the message's string concatenation are compiled once, in that one .cpp,
    // rather than parsed and re-inlined into every TU that includes this
    // header (most of the library) — the same reason libstdc++'s
    // vector::at() outlines its own throw path instead of inlining it.
    //
    // std::size_t rather than image_dim/channel_count: one helper serves both
    // an image_dim coordinate (x/y) and a channel_count one (ch), so naming
    // either alias here would be wrong for the other caller, and spelling the
    // underlying std::uint32_t would hardcode what vc_types.h exists to keep
    // changeable. size_t holds any widening of either alias.
    [[noreturn]] static void throw_out_of_range(std::string_view coord,
                                                std::size_t value,
                                                std::string_view bound_name,
                                                std::size_t bound);

    image_dim width_ = 0;
    image_dim height_ = 0;
    channel_count channels_ = 0;
    const_image_meta_ptr
        metadata_; // null by default: a mask is an image whose composed metadata is null
};

} // namespace vc
