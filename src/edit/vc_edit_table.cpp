// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_edit_table.h"

#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

// Pin the contract at compile time: vc_cached_edits_table conforms "in
// spirit" to the shared vc_optional_reader_req (MISS-EXPOSING flavor)/
// vc_writer_req concepts — the same METHOD SHAPE the i_edit_table interface
// declares, keyed by std::string over data_bytes.
static_assert(vc_optional_reader_req<vc_cached_edits_table, std::string, data_bytes>);
static_assert(vc_writer_req<vc_cached_edits_table, std::string, data_bytes>);

// Pin the contract at compile time: vc_persistent_edits_table conforms "in
// spirit" to the same two concepts.
static_assert(vc_optional_reader_req<vc_persistent_edits_table, std::string, data_bytes>);
static_assert(vc_writer_req<vc_persistent_edits_table, std::string, data_bytes>);

vc_cached_edits_table::vc_cached_edits_table(i_table& backing)
    : backing_(backing) {
}

std::optional<data_bytes> vc_cached_edits_table::get(const std::string& key) const {
    // TODO(you): return backing_.get(key) — a hit yields the bytes, a miss
    // yields nullopt so the caller recomputes (content_hash keying: `key` is
    // the chained hash of this node's params + its input hashes). Shell
    // throws so the miss-then-hit spec is red-by-design until the rep lands.
    (void)key;
    (void)backing_; // the rep returns backing_.get(key); marks the use site
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_cached_edits_table::get not yet implemented");
}

void vc_cached_edits_table::set(const std::string& key, data_bytes value) {
    // TODO(you): backing_.put(key, std::move(value)).
    (void)key;
    (void)value;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_cached_edits_table::set not yet implemented");
}

vc_persistent_edits_table::vc_persistent_edits_table(i_table& backing)
    : backing_(backing) {
}

std::optional<data_bytes> vc_persistent_edits_table::get(const std::string& key) const {
    // TODO(you): return backing_.get(key) — a hit yields the bytes, a miss
    // yields nullopt (`key` here is a persistent_id, a stable caller-assigned
    // id, NOT a content hash — this store is non-reproducible). Shell throws
    // so the miss-then-hit spec is red-by-design until the rep lands.
    (void)key;
    (void)backing_;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_persistent_edits_table::get not yet implemented");
}

void vc_persistent_edits_table::set(const std::string& key, data_bytes value) {
    // TODO(you): backing_.put(key, std::move(value)).
    (void)key;
    (void)value;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_persistent_edits_table::set not yet implemented");
}

} // namespace vc::edit
