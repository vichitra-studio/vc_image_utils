// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <unordered_map>

#include "vc/core/vc_image_meta.h" // i_image_meta now lives in core — see that
                                   // header's comment for why the interface
// demoted while this concrete backend did not.

namespace vc::edit {

// An in-memory metadata backend — this is PLUMBING (a
// std::unordered_map<field,value> wrapper), the direct analogue of
// vc_memory_table, so it is written in full (it is NOT a rep).
class vc_memory_image_meta : public i_image_meta {
  public:
    [[nodiscard]] std::optional<const value> get(const field& f) const override;
    void set(const field& f, value v) override;
    bool with_value(const field& f,
                    const std::function<void(value&)>& fn) override;
    bool with_value(const field& f,
                    const std::function<void(const value&)>& fn) const override;

  private:
    std::unordered_map<field, value> map_;
};

} // namespace vc::edit
