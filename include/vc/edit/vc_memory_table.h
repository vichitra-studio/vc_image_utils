// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <unordered_map>

#include "vc/edit/vc_table.h"

namespace vc::edit {

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
