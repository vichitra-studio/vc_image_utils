// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_memory_table.h"

#include <utility>

namespace vc::edit {

void vc_memory_table::put(const std::string& key, data_bytes value) {
    map_[key] = std::move(value);
}

std::optional<data_bytes> vc_memory_table::get(const std::string& key) const {
    const auto it = map_.find(key);
    if (it == map_.end()) {
        return std::nullopt; // a miss is expected control flow, not an error
    }
    return it->second;
}

} // namespace vc::edit
