// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/edit/vc_edit_table.h"
#include "vc/edit/vc_table.h"

namespace vc::edit {

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

} // namespace vc::edit
