// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "vc/core/vc_any.h"

namespace vc {

// A type-erased metadata value: EXIF/IPTC fields are not all scalars (a
// color matrix, a GPS coordinate struct, a rating integer, a copyright
// string all need to fit through the same `set`/`get`), so a plain
// std::string cannot represent the full value vocabulary. Backed by
// vc::vc_any (include/vc/vc_any.h) — the SAME wrap/get<T>/has_value/type
// mechanism as vc::pipe::vc_pipe_packet (include/vc/pipe/vc_pipe_packet.h) —
// but kept an INDEPENDENT type via a distinct tag: metadata values are
// durable, (eventually) serializable state that outlives a single render,
// unlike vc_pipe_packet's ephemeral per-render-slot payload, so the two must
// not be silently interchangeable even though the underlying mechanism is
// identical. Distinctness is exactly what the tag buys: the two aliases name
// two different specializations of one template, so neither converts to the
// other.
using vc_metadata_value = vc::vc_any<vc_any_tag::meta_value>;

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
//
// Lives at vc/ (core), not vc/edit/: the interface's OWN dependencies are
// already 100% core (standard-library headers + vc_any.h — nothing here
// needs anything from vc::edit), and vc_image_info (core) composes a
// shared_ptr<const i_image_meta> as its captured-metadata field (see
// vc_image_info.h) — a core aggregate's field type belongs at or below core,
// not above it. The concrete, EXIF/IPTC-parsing backend
// (vc_memory_image_meta and, later, the real per-platform backends) stays in
// vc::edit (vc_memory_image_meta.h): it is domain plumbing that belongs with
// the rest of the edit model (docs/edit_model.md Sec 6), while this
// interface is the narrow, dependency-free seam a core value type can name
// directly. Demoted 2026-07-28 — see docs/edit_model.md Sec 6's dated note
// for the full reasoning and what would reopen it.
class i_image_meta {
  public:
    virtual ~i_image_meta() = default;

    using field = std::string;       // placeholder key vocabulary
    using value = vc_metadata_value; // type-erased value vocabulary

    [[nodiscard]] virtual std::optional<value> get(const field& f) const = 0;
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

  protected:
    // See i_table (vc/edit/vc_table.h): the copy ctor's declaration would
    // otherwise suppress the implicit default one that vc_memory_image_meta
    // relies on.
    i_image_meta() = default;

    // Slicing prevention, same reasoning as i_table (vc/edit/vc_table.h):
    // protected copy ops keep a derived backend's own copy ops working while
    // blocking assignment/construction through a base i_image_meta&.
    i_image_meta(const i_image_meta&) = default;
    i_image_meta& operator=(const i_image_meta&) = default;
};

} // namespace vc
