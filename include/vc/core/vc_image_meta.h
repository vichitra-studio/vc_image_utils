// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <functional>
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

    // The READ path. Hands back a COPY, and deliberately an
    // optional<const value> rather than an optional<value>: the const is what
    // turns a write through the returned box into a compile error instead of a
    // silent no-op. Without it,
    //   meta.get("iso")->get<std::string>() = "9999";
    // compiles clean and mutates the temporary this function just returned,
    // which dies at the end of the full expression — the stored value never
    // changes and nothing diagnoses it. (Ref-qualifying vc_any::get<T>() does
    // NOT close that hole: std::optional::operator-> yields a raw pointer, so
    // the temporary is laundered into an lvalue and an `&`-qualified overload
    // still binds.) Reads come through here; writes go through set() below
    // for a whole value, or with_value() for an in-place edit.
    [[nodiscard]] virtual std::optional<const value>
    get(const field& f) const = 0;
    virtual void set(const field& f, value v) = 0;

    // The IN-PLACE write path: calls fn on the STORED box and returns true, or
    // returns false if `f` is absent. Exists to avoid the copy that a
    // get()-modify-set() round trip forces — get() copies the whole payload
    // out of the store and set() moves a whole new one back, which for a
    // payload whose copy is real work (a color matrix, a large buffer) is a
    // measurable and entirely avoidable cost. vc_any's mutable get<T>()
    // (vc_any.h) is what makes the callback body possible, and this is the
    // only way to reach it on a value that lives in a store: every other
    // accessor here returns by value.
    //
    // A SCOPED CALLBACK rather than a `value*`-returning find(), for the
    // reason vc_image_writer::with_pixels<T>() (vc_image_writer.h) is the only
    // bulk pixel accessor left there — the span-RETURNING one was removed
    // precisely because a returned handle invites retention past the window it
    // is valid for — and for one that is specific to this
    // being an INTERFACE: a backend is not obliged to store vc_metadata_value
    // objects at all. The planned native backends (Apple Image I/O, Android
    // ExifInterface, the DNG SDK — see the class comment above) sit on foreign
    // representations and would have no vc_metadata_value in memory to return
    // a pointer INTO. Owning both ends of the access window lets such a
    // backend materialize a value, run fn against it, and commit the result
    // before returning; a pointer-returning find() has no commit point, so it
    // would quietly restrict this interface to backends that happen to store
    // `value` natively.
    //
    // std::function rather than a template parameter because a virtual member
    // cannot be a template. The type erasure costs an indirect call (and
    // possibly one small allocation) per CALL — not per element — which is
    // negligible against the payload copy this exists to remove.
    //
    // PRECONDITION: `fn` must not call back into this object (no set(), no
    // further with_value()). The reference it receives is valid ONLY for the
    // duration of the call. Stated as a contract rather than left to whatever
    // one backend happens to survive: a std::unordered_map backend tolerates
    // reentrant insertion because rehashing does not invalidate references to
    // elements, but a backend that MATERIALIZES a value for the window (the
    // native ones described above) commits on return, so a reentrant write
    // would be silently overwritten by that commit.
    //
    // EXCEPTION SAFETY: the BASIC guarantee, not the strong one. An exception
    // from `fn` propagates out unchanged, and whatever `fn` had already written
    // to the stored value STAYS written — the store is left valid but possibly
    // half-edited. Deliberate, not an oversight: rolling back would mean
    // copying the payload aside before every call, which is precisely the cost
    // this method exists to avoid, so the strong guarantee cannot be offered
    // without defeating the feature. A caller that needs all-or-nothing should
    // compute the new value fully, then publish it with set(). A materializing
    // backend must follow suit and commit the partial edit rather than discard
    // it, so the observable behaviour does not depend on which backend is in
    // use.
    //
    // Deliberately NOT [[nodiscard]], unlike get() above: "edit this field if
    // it is present, and I do not care whether it was" is a legitimate call,
    // so discarding the flag is not a specific bug. Note the line is NOT
    // "bools never get the attribute" — vc_pipeline::has_consumer and
    // io::file_exists are both [[nodiscard]] bool. Those are pure predicates,
    // where discarding the answer means the call did nothing; here the primary
    // effect is running fn, and the bool is secondary. See
    // docs/coding_guidelines.md §5.4.
    virtual bool with_value(const field& f,
                            const std::function<void(value&)>& fn) = 0;

    // The READ-ONLY twin, and the only zero-copy way to READ a stored value:
    // get() above hands back a copy, so reading a large payload through it
    // costs exactly as much as writing one through get()-modify-set() did.
    // Paired mutable/const accessors are the same shape vc_pixel_buffer::as<T>()
    // uses (vc_pixel_buffer.h) — and the reason this one matters more than
    // symmetry is CONSTNESS: an image exposes its captured metadata as
    // shared_ptr<const i_image_meta> (vc_image_info.h), so the non-const
    // overload above is unreachable from a sealed vc_image and this is the ONLY
    // copy-free accessor such a caller has.
    //
    // Same name rather than a distinct read_value(): resolution is driven by
    // the implicit object argument, so a non-const meta picks the mutable
    // overload and a const meta picks this one, even when the callback itself
    // would convert to either. No call site has to know which it got.
    virtual bool
    with_value(const field& f,
               const std::function<void(const value&)>& fn) const = 0;

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
