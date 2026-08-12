// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_memory_image_meta.h"

#include <utility>

namespace vc::edit {

std::optional<const i_image_meta::value>
vc_memory_image_meta::get(const field& f) const {
    const auto it = map_.find(f);
    if (it == map_.end()) {
        return std::nullopt; // a miss is expected control flow, not an error
    }
    return it->second;
}

void vc_memory_image_meta::set(const field& f, value v) {
    map_[f] = std::move(v);
}

bool vc_memory_image_meta::with_value(const field& f,
                                      const std::function<void(value&)>& fn) {
    const auto it = map_.find(f);
    if (it == map_.end()) {
        return false; // same contract as get(): a miss is control flow
    }
    fn(it->second); // the STORED box, not a copy — see the interface comment
    return true;
}

bool vc_memory_image_meta::with_value(
    const field& f, const std::function<void(const value&)>& fn) const {
    const auto it = map_.find(f);
    if (it == map_.end()) {
        return false;
    }
    fn(it->second);
    return true;
}

} // namespace vc::edit
