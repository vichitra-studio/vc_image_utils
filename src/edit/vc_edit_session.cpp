// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_edit_session.h"

#include <utility>

// Complete vc_persistent_edits_table/vc_cached_edits_table (their own
// headers) and i_edit_table (vc_edit_table.h) definitions — needed here for
// the concrete&->i_edit_table& upcast in the member-init list below (the
// header only forward-declares all three).
#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/edit/vc_cached_edits_table.h"
#include "vc/edit/vc_edit_table.h"
#include "vc/edit/vc_persistent_edits_table.h"

namespace vc::edit {

// `edits` is taken by value (the sink pattern, per Sec 4.3) but deliberately
// NOT std::move()d into edits_ below, unlike `source` and `meta`: those two
// are heap-owning (vc_image, unique_ptr), so moving them in avoids a real
// copy, while vc_edit_document is a small, trivially-copyable aggregate (an
// int plus a couple of all-numeric/bool-member structs, no heap ownership) —
// std::move() on it compiles down to the exact same copy an ordinary copy
// would, so writing it here would only imply an optimisation that is not
// actually happening.
vc_edit_session::vc_edit_session(vc_image source,
                                 vc_edit_document edits,
                                 std::unique_ptr<i_image_meta> meta,
                                 vc_persistent_edits_table& persistent,
                                 vc_cached_edits_table& cache)
    : source_(std::move(source)), edits_(edits), meta_(std::move(meta)),
      persistent_(persistent), cache_(cache) {
    if (meta_ == nullptr) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_edit_session: meta must not be null — every "
                               "session requires a real metadata backend");
    }
}

// Out-of-line so this TU is the one place the aggregate's special members
// are emitted. The move constructor is re-declared because a user-declared
// destructor suppresses its implicit generation; it works fine with
// reference members (a reference is simply copied into the new object at
// construction). There is no move-assignment operator: reference members
// make one ill-formed, and nothing in the codebase uses one.
vc_edit_session::~vc_edit_session() = default;
vc_edit_session::vc_edit_session(vc_edit_session&&) noexcept = default;

} // namespace vc::edit
