// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_stage_registry.h"

#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::edit {

void vc_stage_registry::require_not_registered_as_paramless(
    const stage_kind& kind) const {
    if (factories_.find(kind) != factories_.end()) {
        throw vc::vc_exception(
            vc_error_code::invalid_argument,
            "vc_stage_registry: kind '" + kind +
                "' is already registered as a PARAMLESS stage "
                "(register_kind); it cannot also be registered as a "
                "session-aware stage");
    }
}

void vc_stage_registry::require_not_registered_as_session_aware(
    const stage_kind& kind) const {
    if (session_factories_.find(kind) != session_factories_.end()) {
        throw vc::vc_exception(
            vc_error_code::invalid_argument,
            "vc_stage_registry: kind '" + kind +
                "' is already registered as a SESSION-AWARE stage "
                "(register_stage); it cannot also be registered as a "
                "paramless stage");
    }
}

void vc_stage_registry::register_kind(const stage_kind& kind, factory make) {
    require_not_registered_as_session_aware(kind);
    factories_[kind] = std::move(make);
}

vc::pipe::stage_ptr vc_stage_registry::create(const stage_kind& kind,
                                              vc::pipe::stage_name name) const {
    const auto it = factories_.find(kind);
    if (it == factories_.end()) {
        throw vc::vc_exception(vc_error_code::invalid_argument,
                               "vc_stage_registry: unknown kind '" + kind +
                                   "'");
    }
    auto stage = it->second(std::move(name));
    // `make` is an arbitrary caller-supplied std::function (register_kind()
    // takes it directly, unlike register_stage<StageT>()'s hardcoded
    // make_unique lambda), so nothing upstream constrains its return value.
    // This is the boundary where that externally-supplied value enters the
    // system, so it is validated once, here, rather than trusted all the way
    // to vc_pipeline::add() — which only asserts on a null stage, because a
    // make_unique result reaching it can never be null. A registry-produced
    // null has no such guarantee, so it gets the throwing treatment instead.
    if (stage == nullptr) {
        throw vc::vc_exception(
            vc_error_code::invalid_argument,
            "vc_stage_registry: factory registered for kind '" + kind +
                "' returned a null stage");
    }
    return stage;
}

vc::pipe::stage_ptr
vc_stage_registry::create(const stage_kind& kind,
                          vc::pipe::stage_name name,
                          const vc_edit_session& session) const {
    const auto it = session_factories_.find(kind);
    if (it == session_factories_.end()) {
        throw vc::vc_exception(vc_error_code::invalid_argument,
                               "vc_stage_registry: unknown kind '" + kind +
                                   "'");
    }
    return it->second(std::move(name), session);
}

bool vc_stage_registry::has(const stage_kind& kind) const noexcept {
    return factories_.find(kind) != factories_.end() ||
           session_factories_.find(kind) != session_factories_.end();
}

} // namespace vc::edit
