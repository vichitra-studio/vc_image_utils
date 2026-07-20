// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <memory>

#include "vc/edit/vc_edit_document.h" // held BY VALUE below => needs the full
                                      // definition, not a forward declaration
                                      // (an incomplete-type value member is
                                      // ill-formed).
#include "vc/edit/vc_image_meta.h" // i_image_meta + the image_metadata_handle alias;
                                   // a light header (<any>/<memory>/<optional>/
                                   // <string>/<unordered_map>), so including it
                                   // directly is cheap and avoids a forward
                                   // declaration + locally-restated alias.
#include "vc/vc_image.h"

namespace vc::edit {

class vc_persistent_edits_table; // fwd — held by a non-owning, non-null reference
class vc_cached_edits_table;  // fwd — held by a non-owning, non-null reference

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
    vc_edit_session(vc_image source, vc_edit_document edits,
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
    i_image_meta& meta() noexcept {
        return *meta_;
    }

    // NON-OWNING, NON-NULLABLE references: each store outlives the session
    // and has its own durability/lifecycle, so the session merely
    // references it.
    vc_persistent_edits_table& persistent() noexcept {
        return persistent_;
    }
    vc_cached_edits_table& cache() noexcept {
        return cache_;
    }

  private:
    vc_image source_;         // immutable; shallow-shared via its shared_ptr buffer
    vc_edit_document edits_;  // by value; the source of truth for editing
    image_metadata_handle meta_;    // owning handle; non-null after construction
    vc_persistent_edits_table& persistent_; // non-owning; non-nullable
    vc_cached_edits_table& cache_;       // non-owning; non-nullable
};

} // namespace vc::edit
