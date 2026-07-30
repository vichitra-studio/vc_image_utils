// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/edit/vc_edit_table.h"
#include "vc/edit/vc_table.h"

namespace vc::edit {

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

} // namespace vc::edit
