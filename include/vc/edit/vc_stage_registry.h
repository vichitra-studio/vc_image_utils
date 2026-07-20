// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "vc/edit/vc_edit_session.h"
#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::edit {

// A builder is concept-enforced (not merely duck-typed) at REGISTRATION
// time (register_stage below), not at USE time (create): it must be
// invocable with `const vc_edit_session&` (the translation build_pipeline
// performs — e.g. `&vc_blur_stage::from_session`, a pointer to a static
// member function on the stage itself; the vc_stage_builder concept below
// accepts this identically to a free function, since both just satisfy
// `std::invocable<const Builder&, const vc_edit_session&>`), and its
// result must be exactly what StageT's constructor needs alongside the
// per-instance name: `StageT(stage_name, Builder-result)`.
// `register_stage<StageT>(kind, builder)` captures the concrete type + its
// params-builder + ctor behind one uniform `create(kind, name, session)`.
// No casts anywhere in the call path.
template <typename Builder, typename StageT>
concept vc_stage_builder =
    std::invocable<const Builder&, const vc_edit_session&> &&
    std::constructible_from<
        StageT, vc::pipe::stage_name,
        std::invoke_result_t<const Builder&, const vc_edit_session&>>;

// The construction `kind()` registry. It fulfils the i_pipe::kind() seam,
// removes hardcoded `new`-chains, and enables CLI introspection. It maps a
// `kind` string to a factory that builds ONE stage — it NEVER infers the
// graph: assembly (which stages, how many, how wired) stays imperative in
// build_pipeline. A registry lets wiring be data but never removes the need
// to STATE it; a data-driven graph template is [LATER].
//
// TWO registration/construction paths coexist:
//   - register_kind/create(kind,name) — the ORIGINAL, name-only path, kept
//     for paramless stages (e.g. a passthrough/plumbing stage) that need no
//     session-derived params.
//   - register_stage<StageT>(kind,builder)/create(kind,name,session) — the
//     session-aware path: the builder derives StageT's params FROM the
//     session (the translation build_pipeline performs), so a caller never
//     `new`-chains a concrete stage type or restates its ctor signature.
// Every method here is map-operation PLUMBING — there is no rep; the stage
// kind() REGISTRATIONS (which stages exist, and their builders) are
// populated in build_pipeline, the user's rep.
class vc_stage_registry {
  public:
    // A registered kind string (e.g. "blur", "passthrough") — the same
    // vocabulary i_pipe::kind() returns. Named so the two maps below and
    // every register_kind/create/has signature spell the same type, not a
    // bare std::string repeated at each call site.
    using stage_kind = std::string;

    using factory =
        std::function<vc::pipe::stage_ptr(vc::pipe::stage_name)>;

    // The session-aware factory shape: name + the session it derives params
    // from. Erased once, at register_stage<StageT>() below, so create()
    // never needs to know the concrete stage type or its params type.
    using session_factory = std::function<vc::pipe::stage_ptr(
        vc::pipe::stage_name, const vc_edit_session&)>;

    // Register a factory under `kind`. A later registration for the same kind
    // overwrites the earlier one (last-wins).
    void register_kind(const stage_kind& kind, factory make);

    // Look up `kind` and invoke its factory with `name`, or throw
    // vc::vc_exception (invalid_argument) on an unknown kind. This is
    // map-lookup-or-throw plumbing (structurally identical to has()), NOT a rep —
    // the scaffold writes it in full.
    vc::pipe::stage_ptr create(const stage_kind& kind,
                               vc::pipe::stage_name name) const;

    // Register `StageT` under `kind`, erasing its concrete type and
    // `builder` (a session -> params translation) behind one uniform
    // session_factory. Concept-enforced at THIS call site (vc_stage_builder
    // above) — an ill-shaped builder/StageT pairing fails to compile here,
    // never at create()'s call site. A later registration for the same kind
    // overwrites the earlier one (last-wins), matching register_kind.
    template <typename StageT, typename Builder>
        requires vc_stage_builder<Builder, StageT>
    void register_stage(const stage_kind& kind, Builder builder) {
        session_factories_[kind] =
            [builder = std::move(builder)](
                vc::pipe::stage_name name,
                const vc_edit_session& session) -> vc::pipe::stage_ptr {
                return std::make_unique<StageT>(std::move(name),
                                                builder(session));
            };
    }

    // Look up `kind` in the session-aware map and invoke its factory with
    // `name` and `session`, or throw vc::vc_exception (invalid_argument) on
    // an unknown kind. Map-lookup-or-throw plumbing, structurally identical
    // to the name-only create() above — NOT a rep.
    vc::pipe::stage_ptr create(const stage_kind& kind,
                               vc::pipe::stage_name name,
                               const vc_edit_session& session) const;

    // Whether a factory is registered for `kind` (either registration path).
    bool has(const stage_kind& kind) const noexcept;

  private:
    std::unordered_map<stage_kind, factory> factories_;
    std::unordered_map<stage_kind, session_factory> session_factories_;
};

} // namespace vc::edit
