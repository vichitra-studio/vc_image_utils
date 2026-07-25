// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vc::edit {

// The opaque payload of the byte store. Derived blobs serialize down to
// bytes; the typed adapters above give that role its vocabulary.
using data_bytes = std::vector<std::byte>;

// "Program to an interface" applies at the STORAGE ENGINE, not the data role:
// this shared low-level byte store is what derived data (via the two
// i_edit_table impls, vc_cached_edits_table/vc_persistent_edits_table —
// vc_edit_table.h) builds on. A MISS is expected control flow, not an error,
// so get() returns std::optional rather than throwing — i_edit_table
// EXPOSES misses to trigger recompute.
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

} // namespace vc::edit
