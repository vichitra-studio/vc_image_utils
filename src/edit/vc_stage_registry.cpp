// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_stage_registry.h"

#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

void vc_stage_registry::register_kind(const stage_kind& kind, factory make) {
    factories_[kind] = std::move(make);
}

vc::pipe::stage_ptr
vc_stage_registry::create(const stage_kind& kind,
                          vc::pipe::stage_name name) const {
    const auto it = factories_.find(kind);
    if (it == factories_.end()) {
        throw vc::vc_exception(vc_error_code::invalid_argument,
                               "vc_stage_registry: unknown kind '" + kind + "'");
    }
    return it->second(std::move(name));
}

vc::pipe::stage_ptr
vc_stage_registry::create(const stage_kind& kind, vc::pipe::stage_name name,
                          const vc_edit_session& session) const {
    const auto it = session_factories_.find(kind);
    if (it == session_factories_.end()) {
        throw vc::vc_exception(vc_error_code::invalid_argument,
                               "vc_stage_registry: unknown kind '" + kind + "'");
    }
    return it->second(std::move(name), session);
}

bool vc_stage_registry::has(const stage_kind& kind) const noexcept {
    return factories_.find(kind) != factories_.end() ||
           session_factories_.find(kind) != session_factories_.end();
}

} // namespace vc::edit
