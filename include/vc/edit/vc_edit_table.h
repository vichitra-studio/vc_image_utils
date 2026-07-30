// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <utility>

#include "vc/edit/vc_table.h"

namespace vc::edit {

// MISS-EXPOSING reader concept: `const R&` exposes `get(key) -> optional<V>`
// — a miss surfaces so the caller can recompute. i_edit_table's read side is
// written this way "in spirit": the concrete stores
// (vc_cached_edits_table.h/vc_persistent_edits_table.h) use a runtime
// interface (i_edit_table), not this concept, as their calling convention;
// the concept documents and checks that their METHOD SHAPE matches it (see
// the static_asserts in vc_edit_table.cpp). Lives here, not in a shared
// concepts file, because i_edit_table's stores are its only conformers.
//
// This is the MISS-EXPOSING flavor — a miss returns `optional`, so the
// caller can decide to recompute. A TOTAL-GET flavor (a miss silently
// returns a default) existed as a sibling concept, `vc_defaulted_reader_req`,
// but its sole conformer was removed; the two differed in ARITY (one arg vs
// two) as well as return type, so if a total-get reader returns, it should
// get its own concept rather than being force-fit into this one.
template <typename R, typename K, typename V>
concept vc_optional_reader_req = requires(const R& r, const K& key) {
    { r.get(key) } -> std::same_as<std::optional<V>>;
};

// WRITER concept: `R&` exposes `set(key, value) -> void` — checked at
// compile time (matching the `vc_pixel_element_req` style already used
// elsewhere) instead of one inherited interface every backend derives from.
// Relocated here (2026-07-25) from the shared `vc_table.h`: it used to serve
// two consumer families (i_edit_table's two stores, declared in
// vc_cached_edits_table.h/vc_persistent_edits_table.h, plus a
// settings-writer class since removed); with only one family left, it
// belongs beside its sole conformers rather than in the shared low-level
// byte-store header.
// Each conformance is pinned with a static_assert next to its concrete type,
// not here, since the checked type must already be complete (see
// `vc_edit_table.cpp`).
template <typename W, typename K, typename V>
concept vc_writer_req = requires(W& w, const K& key, V value) {
    { w.set(key, std::move(value)) } -> std::same_as<void>;
};

// An edit-table content-hash key: hash(own param slice + input hash). This
// is vc_cached_edits_table's keying convention.
using content_hash = std::string;

// An edit-table stable id key: an opaque, caller-assigned identifier — NOT
// a content hash, because persistent data is by definition NOT reproducible,
// so there is no "recompute cheaply, key by inputs" story a hash would
// serve. This is vc_persistent_edits_table's keying convention.
using persistent_id = std::string;

// CONSUMER-FACING edit-table interface for derived per-image data (masks,
// alignment/homography, embeddings, and similar computed artifacts). Two
// SEPARATE concrete stores implement it — vc_cached_edits_table
// (reproducible, content-hash keyed, may evict, never saved) and
// vc_persistent_edits_table (non-reproducible, id-keyed, pinned, saved/bundled) —
// differing in BOTH key scheme AND serialization lifecycle, which is why
// they are two objects rather than one store with a policy flag. Both sit
// behind this one interface, so a consumer reading/writing through it +
// typed slots is SOURCE-AGNOSTIC — it cannot tell, and does not need to,
// which concrete store answered.
//
// One interface signature serves both stores' differing key CONVENTIONS by
// keying on plain std::string; content_hash/persistent_id above document
// which convention each concrete store's callers should spell — the
// interface itself does not encode which one is in play.
class i_edit_table {
  public:
    virtual ~i_edit_table() = default;

    // MISS-EXPOSING get: a hit returns the bytes, a miss returns
    // std::nullopt so the caller can decide to recompute — a TOTAL-get
    // reader would instead hide a miss behind a default (no conformer of
    // that flavor currently exists in this codebase). See
    // vc_optional_reader_req (the miss-exposing concept flavor) above.
    [[nodiscard]] virtual std::optional<data_bytes>
    get(const std::string& key) const = 0;
    virtual void set(const std::string& key, data_bytes value) = 0;

  protected:
    // See i_table (vc_table.h): the copy ctor's declaration would otherwise
    // suppress the implicit default one that vc_cached_edits_table's
    // (vc_cached_edits_table.h) and vc_persistent_edits_table's
    // (vc_persistent_edits_table.h) base-subobject initialization relies on.
    i_edit_table() = default;

    // Slicing prevention, same reasoning as i_table (vc_table.h): protected
    // copy ops keep a derived store's own copy ops working while blocking
    // assignment/construction through a base i_edit_table&.
    i_edit_table(const i_edit_table&) = default;
    i_edit_table& operator=(const i_edit_table&) = default;
};

} // namespace vc::edit
