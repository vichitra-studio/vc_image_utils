// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_edit_session.h"

#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

vc_edit_session::vc_edit_session(vc_image source, vc_edit_document edits,
                                 std::unique_ptr<i_image_meta> meta,
                                 vc_persistent_edits_table& persistent,
                                 vc_cached_edits_table& cache)
    : source_(std::move(source)), edits_(std::move(edits)),
      meta_(std::move(meta)), persistent_(persistent), cache_(cache) {
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
