// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

// A type-erased metadata value: EXIF/IPTC fields are not all scalars (a
// color matrix, a GPS coordinate struct, a rating integer, a copyright
// string all need to fit through the same `set`/`get`), so a plain
// std::string cannot represent the full value vocabulary. This reuses the
// SAME open-set std::any pattern as vc::pipe::vc_pipe_packet
// (include/vc/pipe/vc_pipe_packet.h) — a templated constructor that wraps
// any value, a get<T>() that throws on a type mismatch, a has_value()
// query — but is an INDEPENDENT class: metadata values are durable,
// (eventually) serializable state that outlives a single render, unlike
// vc_pipe_packet's ephemeral per-render-slot payload, so the two are not
// shared even though the underlying mechanism is identical.
class vc_metadata_value {
  public:
    vc_metadata_value() = default;

    // Wrap a value. The requires-clause stops this template from being
    // chosen in place of the copy/move constructors when T would deduce to
    // vc_metadata_value itself.
    template <typename T>
        requires(!std::is_same_v<std::decay_t<T>, vc_metadata_value>)
    explicit vc_metadata_value(T value) : value_(std::move(value)) {
    }

    // Unbox to T. Throws vc::vc_exception if the stored payload is not a T.
    template <typename T> const T& get() const {
        const T* held = std::any_cast<T>(&value_);
        if (held == nullptr) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                "vc_metadata_value::get<T>(): requested type does not match "
                "the stored payload type");
        }
        return *held;
    }

    bool has_value() const noexcept {
        return value_.has_value();
    }

  private:
    std::any value_;
};

// Image metadata: EXIF (ISO/shutter/GPS), color matrices, IPTC/copyright,
// ratings, flags, keywords. Unlike edits, STANDARDS and INTEROP genuinely
// matter here (Lightroom/darktable/Bridge read these), which is why it sits
// behind an interface: permissive, per-platform backends (Apple Image I/O,
// Android ExifInterface, a desktop parser, the DNG SDK for DNG output) plus a
// polymorphic caller. Licensing is load-bearing: exiv2 (GPL) is excluded
// EVERYWHERE, not just off-desktop — it must never enter this value type on
// any platform.
//
// SEAM ONLY. No production backend is built now ([LATER]): none of the
// per-platform backends are created here. `field` is a deliberately thin
// PLACEHOLDER key vocabulary so the editor-era decision on the full field set
// is not pre-baked; `value` is the type-erased box above, wide enough to
// carry whatever a field turns out to need.
class i_image_meta {
  public:
    virtual ~i_image_meta() = default;

    using field = std::string;         // placeholder key vocabulary
    using value = vc_metadata_value;   // type-erased value vocabulary

    virtual std::optional<value> get(const field& f) const = 0;
    virtual void set(const field& f, value v) = 0;

    // [LATER] virtual std::unique_ptr<i_image_meta> clone() const = 0;
    //   Polymorphic deep-copy of the intent, needed only when undo/redo grows
    //   an IN-MEMORY snapshot path: vc_edit_session owns metadata via
    //   unique_ptr and is therefore MOVE-ONLY, so a live-aggregate copy
    //   requires cloning this handle. Deferred because (a) that undo/redo
    //   path is [LATER] and (b) it lands with the first real per-platform
    //   backend, where a concrete clone body exists to write. Until then,
    //   snapshots the SERIALIZED intent (edit
    //   doc + metadata + non-repro derived), which needs no clone.
};

// The owning metadata handle. A unique_ptr over i_image_meta — session-local
// mutable overlay state (see vc_edit_session), as distinct from the image's
// own composed, immutable shared_ptr<const i_image_meta>.
using image_metadata_handle = std::unique_ptr<i_image_meta>;

// An in-memory metadata backend — this is PLUMBING (a
// std::unordered_map<field,value> wrapper), the direct analogue of
// vc_memory_table, so it is written in full (it is NOT a rep).
class vc_memory_image_meta : public i_image_meta {
  public:
    std::optional<value> get(const field& f) const override;
    void set(const field& f, value v) override;

  private:
    std::unordered_map<field, value> map_;
};

} // namespace vc::edit
