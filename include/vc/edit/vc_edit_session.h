// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <memory>

#include "vc/edit/vc_edit_document.h" // held BY VALUE below => needs the full
                                      // definition, not a forward declaration
                                      // (an incomplete-type value member is
                                      // ill-formed).
// i_image_meta now lives in core (vc/core/vc_image_meta.h, demoted
// 2026-07-28 — see that header's comment and docs/edit_model.md Sec 6). This
// member only names the INTERFACE, never vc_memory_image_meta, so the
// interface-only header (standard-library headers + vc/core/vc_any.h) is
// enough — a
// light header, so including it directly is cheap and avoids an extra
// forward declaration. The image_metadata_handle alias below is NOT part of
// that core header: it names the session's OWNING overlay handle, which is
// edit-local state (docs/edit_model.md Sec 6's "two layers, merged on
// export"), not a core concept, so it is declared here instead.
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_meta.h"

namespace vc::edit {

// The owning metadata handle: session-local mutable overlay state, as
// distinct from the image's own composed, immutable
// shared_ptr<const i_image_meta> (vc::const_image_meta_ptr, vc_image_info.h).
// Kept here (not beside i_image_meta in core) because nothing in core needs
// a unique_ptr<i_image_meta> — only vc_edit_session's overlay does.
using image_metadata_handle = std::unique_ptr<i_image_meta>;

// Two forward declarations remain even though the MEMBERS below no longer
// mention them by name: the constructor still takes these two concrete
// types by reference (see the ctor declaration for why), and a reference
// parameter needs its type at least declared. i_edit_table (also fwd here)
// is what the members/accessors are typed as instead — a forward
// declaration suffices for both: nothing in this header dereferences or
// calls through any of the three, so no complete type is needed until
// vc_edit_session.cpp, which includes vc_edit_table.h (for i_edit_table's
// definition) plus vc_cached_edits_table.h/vc_persistent_edits_table.h (for
// the two concrete types' definitions) — needed there for the upcast from
// concrete& to i_edit_table& done in the member-init list.
class i_edit_table;              // fwd — the MEMBERS' type (storage is USED)
class vc_persistent_edits_table; // fwd — a CTOR PARAM's type (identity is
                                 // ESTABLISHED)
class vc_cached_edits_table;     // fwd — a CTOR PARAM's type (identity is
                                 // ESTABLISHED)

// Everything about ONE image. Storage is deliberately SPLIT — the different
// kinds of data (edit settings, image metadata, derived pixel data) have
// different formats, durability, and interop needs — but access is UNIFIED
// here: this one domain entity COMPOSES the sources of truth rather than
// merging them. It is what undo/redo snapshots, what portability bundles,
// what the UI binds to, and what build_pipeline reads NARROW SLICES from. A
// facade (has-a), not a shared interface (is-a).
//
// The session INJECTS all of its durable state at construction — it does
// not own the two derived stores by value, and it does not hold a single
// `i_edit_table*` handle. The two stores are separate objects (differing
// in BOTH key scheme and serialization lifecycle — vc_edit_table.h), so
// they are injected as two SEPARATE non-owning references. Nothing in the
// codebase move-assigns a session (only move-construction is used), so
// these are plain C++ references rather than nullable pointers: every
// caller must supply real backends, and no consumer needs to null-check
// them. The metadata overlay stays an OWNING `unique_ptr<i_image_meta>` —
// it is session-local mutable state, not an externally-owned backend — but
// is held to the SAME non-null contract as persistent_/cache_: the
// constructor throws if `meta` is null, so meta() can hand back a plain
// reference. Composition + accessors are plumbing, not reps.
//
// DIP split: the MEMBERS and their accessors are typed `i_edit_table&`, not
// the concrete `vc_persistent_edits_table`/`vc_cached_edits_table` — both
// concrete stores' entire public surface is exactly ctor + get() + set(),
// i.e. nothing beyond i_edit_table, so a consumer of persistent()/cache()
// should depend on the interface it actually uses. The CONSTRUCTOR
// PARAMETERS stay the concrete types on purpose, though: two same-typed
// `i_edit_table&` parameters could be silently swapped positionally by a
// caller (both compile identically), whereas distinct concrete parameter
// types make that mistake a compile error. The rule applied is
// "abstraction where storage is USED [accessors/members], type safety
// where identity is ESTABLISHED [the ctor]" — the two members are
// upcast-initialized from the concrete ctor params in the .cpp.
class vc_edit_session {
  public:
    // The metadata handle is a std::unique_ptr<i_image_meta>; the destructor
    // and move constructor are still declared here and defined `= default`
    // in the .cpp (rather than inline) to keep the special-member emission
    // next to the rest of the aggregate's out-of-line definitions. The
    // owning unique_ptr makes the aggregate MOVE-ONLY: a copy (for a
    // snapshot) would need a metadata clone, which is a later rep — so copy
    // is deferred, move is kept. There is no move-assignment operator: a
    // class with reference members cannot be move-assigned (a reference
    // cannot be rebound after construction), and nothing in the codebase
    // needs one.
    //
    // `meta`, `persistent`, and `cache` are ALL mandatory — injection happens
    // only here, at construction, and the constructor throws vc::vc_exception
    // if `meta` is null. Every caller must supply a real (even if trivial
    // in-memory) metadata backend, same as the two store references.
    vc_edit_session(vc_image source,
                    vc_edit_document edits,
                    image_metadata_handle meta,
                    vc_persistent_edits_table& persistent,
                    vc_cached_edits_table& cache);

    ~vc_edit_session();
    vc_edit_session(vc_edit_session&&) noexcept;

    const vc_image& source() const noexcept { // immutable source
        return source_;
    }

    vc_edit_document& edits() noexcept { // mutated by editing
        return edits_;
    }
    const vc_edit_document& edits() const noexcept { // read by build_pipeline
        return edits_;
    }

    // An OWNING unique_ptr, never a value member (i_image_meta is abstract).
    // The constructor enforces meta_ is non-null, so meta() can safely
    // dereference and stay noexcept — there is no "no backend attached"
    // state to handle here.
    i_image_meta& meta() noexcept { // mutated by editing
        return *meta_;
    }
    // Paired const overload, same shape as edits() above. Without it a
    // `const vc_edit_session&` could not read its own metadata at all — every
    // i_image_meta accessor is reached through this one handle, so a
    // non-const-only meta() makes the whole backend unreachable from a const
    // session, including i_image_meta's const with_value() (vc_image_meta.h),
    // which exists precisely to read a stored value without copying it.
    const i_image_meta& meta() const noexcept {
        return *meta_;
    }

    // NON-OWNING, NON-NULLABLE references: each store outlives the session
    // and has its own durability/lifecycle, so the session merely
    // references it. Typed `i_edit_table&` (not the concrete store type) —
    // see the DIP note on the class comment above.
    i_edit_table& persistent() noexcept {
        return persistent_;
    }
    i_edit_table& cache() noexcept {
        return cache_;
    }

  private:
    vc_image source_; // immutable; shallow-shared via its shared_ptr buffer
    vc_edit_document edits_;     // by value; the source of truth for editing
    image_metadata_handle meta_; // owning handle; non-null after construction
    i_edit_table& persistent_;   // non-owning; non-nullable; upcast from the
                                 // ctor's vc_persistent_edits_table& param
    i_edit_table& cache_;        // non-owning; non-nullable; upcast from the
                                 // ctor's vc_cached_edits_table& param
};

} // namespace vc::edit
