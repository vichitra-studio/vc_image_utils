// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace vc::edit {

// The opaque payload of the byte store. Derived blobs serialize down to
// bytes; the typed i_edit_table adapters (vc_edit_table.h) give that role
// its vocabulary.
using data_bytes = std::vector<std::byte>;

// "Program to an interface" applies at the STORAGE ENGINE, not the data role:
// this shared low-level byte store is what derived data (via the two
// i_edit_table impls, vc_cached_edits_table/vc_persistent_edits_table —
// vc_cached_edits_table.h/vc_persistent_edits_table.h) builds on. A MISS is
// expected control flow, not an error, so get() returns std::optional rather
// than throwing — i_edit_table EXPOSES misses to trigger recompute.
class i_table {
  public:
    virtual ~i_table() = default;

    virtual void put(const std::string& key, data_bytes value) = 0;
    [[nodiscard]] virtual std::optional<data_bytes>
    get(const std::string& key) const = 0;

  protected:
    // A user-declared copy constructor suppresses the implicit default one,
    // so it must be restated explicitly for vc_memory_table
    // (vc_memory_table.h, and any other derived store) to keep
    // default-constructing.
    i_table() = default;

    // Slicing prevention: no base here declares copy ops, so the implicit
    // copy assignment operator is public and reachable through an i_table&.
    // Protected keeps a derived store's own copy ops working while blocking
    // base-reference assignment/construction.
    i_table(const i_table&) = default;
    i_table& operator=(const i_table&) = default;
};

} // namespace vc::edit
