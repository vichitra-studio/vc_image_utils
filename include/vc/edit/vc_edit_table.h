// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <concepts>
#include <optional>
#include <string>

#include "vc/edit/vc_table.h"

namespace vc::edit {

// MISS-EXPOSING reader concept: `const R&` exposes `get(key) -> optional<V>`
// — a miss surfaces so the caller can recompute. i_edit_table's read side is
// written this way "in spirit": the concrete stores below use a runtime
// interface (i_edit_table), not this concept, as their calling convention;
// the concept documents and checks that their METHOD SHAPE matches it (see
// the static_asserts below). Lives here, not in a shared concepts file,
// because i_edit_table's stores are its only conformers.
//
// Its sibling, vc_defaulted_reader (vc_edit_settings_store.h), covers the
// OPPOSITE flavor — `get(key, fallback) -> V`, a miss silently hidden behind
// a default. The two differ in ARITY (one arg vs two) as well as return
// type, so one concept cannot honestly cover both without force-fitting one
// shape onto the other — do not collapse them, and do not force-fit
// vc_edit_settings_reader into this one.
template <typename R, typename K, typename V>
concept vc_optional_reader = requires(const R& r, const K& key) {
    { r.get(key) } -> std::same_as<std::optional<V>>;
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
    // std::nullopt so the caller can decide to recompute — the opposite of
    // vc_edit_settings_reader's total-get (vc_edit_settings_store.h). See
    // vc_optional_reader (the miss-exposing concept flavor) above.
    [[nodiscard]] virtual std::optional<data_bytes>
    get(const std::string& key) const = 0;
    virtual void set(const std::string& key, data_bytes value) = 0;
};

// Reproducible derived data (alignment/homography, algorithmic masks,
// feature points, rasterized parametric masks). Content-hash keyed; MAY
// evict; NEVER saved — a loss means recompute, not data loss. Backed by an
// INJECTED i_table& (test/mock substitution; the cache does not
// construct its own backing store).
//
// [LATER] (not built now): eviction/GC/invalidation policy, the actual
// chained-hash keying, taps/injections integration. Only the adapter SHAPE
// is scaffolded here — get()/set() are TODO(you) reps (content-hash keying
// + delegation to the backing store is the user's rep).
class vc_cached_edits_table : public i_edit_table {
  public:
    explicit vc_cached_edits_table(i_table& backing);

    [[nodiscard]] std::optional<data_bytes>
    get(const std::string& key) const override;
    void set(const std::string& key, data_bytes value) override;

  private:
    i_table& backing_;
};

// Pin the contract at compile time: vc_cached_edits_table conforms "in spirit"
// to the shared vc_optional_reader (MISS-EXPOSING flavor)/vc_writer concepts — the
// same METHOD SHAPE the interface above declares, keyed by std::string over
// data_bytes.
static_assert(vc_optional_reader<vc_cached_edits_table, std::string, data_bytes>);
static_assert(vc_writer<vc_cached_edits_table, std::string, data_bytes>);

// Non-reproducible derived data (AI/ML object masks, non-deterministic
// embeddings). Id-keyed; PINNED (never evicted); travels with the edit —
// SAVED/bundled — because a loss here IS data loss, so it behaves like
// intent, not a cache. Backed by an INJECTED i_table&.
//
// [LATER] (not built now): disk/bundle IO. get()/set() are TODO(you) reps.
class vc_persistent_edits_table : public i_edit_table {
  public:
    explicit vc_persistent_edits_table(i_table& backing);

    [[nodiscard]] std::optional<data_bytes>
    get(const std::string& key) const override;
    void set(const std::string& key, data_bytes value) override;

  private:
    i_table& backing_;
};

// Pin the contract at compile time: vc_persistent_edits_table conforms "in
// spirit" to the shared vc_optional_reader (MISS-EXPOSING flavor)/vc_writer concepts
// — the same METHOD SHAPE the interface above declares, keyed by
// std::string over data_bytes.
static_assert(vc_optional_reader<vc_persistent_edits_table, std::string, data_bytes>);
static_assert(vc_writer<vc_persistent_edits_table, std::string, data_bytes>);

} // namespace vc::edit
