// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vc::edit {

// The opaque payload of the byte store. Edit settings and derived blobs both
// serialize down to bytes; the typed adapters above give each role its
// vocabulary.
using data_bytes = std::vector<std::byte>;

// "Program to an interface" applies at the STORAGE ENGINE, not the data role:
// this shared low-level byte store is what BOTH edit settings (via
// vc_edit_settings_writer/vc_edit_settings_reader) and derived data (via the
// two i_edit_table impls, vc_cached_edits_table/vc_persistent_edits_table —
// vc_edit_table.h) build on. A MISS is expected control flow, not an error,
// so get() returns std::optional rather than throwing — the miss-semantics
// SPLIT is what the adapters layer on top: vc_edit_settings_reader HIDES
// misses behind defaults, i_edit_table EXPOSES them to trigger recompute.
class i_table {
  public:
    virtual ~i_table() = default;

    virtual void put(const std::string& key, data_bytes value) = 0;
    [[nodiscard]] virtual std::optional<data_bytes>
    get(const std::string& key) const = 0;
};

// The tests backend. A std::unordered_map wrapper is PLUMBING, not a rep —
// fully written. file-backed / sqlite-backed stores are [LATER] and are not
// created now.
class vc_memory_table : public i_table {
  public:
    void put(const std::string& key, data_bytes value) override;
    [[nodiscard]] std::optional<data_bytes>
    get(const std::string& key) const override;

  private:
    std::unordered_map<std::string, data_bytes> map_;
};

// WRITER concept: `R&` exposes `set(key, value) -> void` — checked at compile
// time (matching the vc_pixel_element / vc_param_struct style already used
// elsewhere) instead of one inherited interface every backend derives from.
// Lives here, next to i_table/vc_memory_table, because it is the ONE shape
// shared by both of this file's downstream consumers — vc_edit_settings_writer
// (vc_edit_settings_store.h) and the two i_edit_table stores
// (vc_edit_table.h) — which both already depend on this header for
// i_table/data_bytes; putting it here avoids a third file whose only content
// is a concept nobody owns. Each conformance is pinned with a static_assert
// next to its concrete type, not here, since the checked type must already be
// complete (see vc_edit_table.h / vc_edit_settings_store.h).
template <typename W, typename K, typename V>
concept vc_writer = requires(W& w, const K& key, V value) {
    { w.set(key, std::move(value)) } -> std::same_as<void>;
};

} // namespace vc::edit
