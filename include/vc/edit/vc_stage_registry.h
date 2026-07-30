// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "vc/edit/vc_edit_session.h"
#include "vc/edit/vc_stage_params.h"
#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::edit {

// The construction `kind()` registry. It fulfils the i_pipe::kind() seam,
// removes hardcoded `new`-chains, and enables CLI introspection. It maps a
// `kind` string to a factory that builds ONE stage — it NEVER infers the
// graph: assembly (which stages, how many, how wired) stays imperative in
// build_pipeline. A registry lets wiring be data but never removes the need
// to STATE it; a data-driven graph template is [LATER].
//
// TWO registration/construction paths coexist:
//   - register_kind/create(kind,name) — the name-only path, for PARAMLESS
//     stages (e.g. a passthrough/plumbing stage) that need no session-derived
//     params. Takes a factory callable because there is no params type to
//     look up.
//   - register_stage<StageT>(kind)/create(kind,name,session) — the
//     session-aware path for stages that DO carry params. The translation is
//     looked up from vc_stage_params<StageT> (vc_stage_params.h), not passed
//     in, so a caller never `new`-chains a concrete stage type, restates its
//     ctor signature, or re-invents its session mapping.
//
// A session -> params BUILDER ARGUMENT used to be passed to register_stage
// instead. It was removed in favour of the type-keyed trait: a builder
// argument could not enforce that a stage HAS a translation (a caller could
// always pass an ad-hoc lambda), and let one stage type acquire a different
// mapping at every call site. The trait makes the mapping a property of the
// stage type — one canonical translation, enforced at compile time. The
// deliberate trade-off is that the registry can no longer give one stage type
// two different session mappings; a graph that genuinely needs that
// constructs those stages directly in build_pipeline rather than through
// this registry.
//
// Every method here is map-operation PLUMBING — there is no rep; the stage
// kind() REGISTRATIONS (which stages exist) are populated in build_pipeline,
// the user's rep.
class vc_stage_registry {
  public:
    // A registered kind string (e.g. "blur", "passthrough") — the same
    // vocabulary i_pipe::kind() returns. Named so the two maps below and
    // every register_kind/create/has signature spell the same type, not a
    // bare std::string repeated at each call site.
    using stage_kind = std::string;

    using factory = std::function<vc::pipe::stage_ptr(vc::pipe::stage_name)>;

    // The session-aware factory shape: name + the session it derives params
    // from. Erased once, at register_stage<StageT>() below, so create()
    // never needs to know the concrete stage type or its params type.
    using session_factory = std::function<vc::pipe::stage_ptr(
        vc::pipe::stage_name, const vc_edit_session&)>;

    // Register a factory under `kind`. A later registration for the same kind
    // overwrites the earlier one (last-wins). `make` must return a non-null
    // stage; create() below enforces this — `make` is an arbitrary
    // caller-supplied callable, so nothing at registration time constrains
    // what it hands back.
    void register_kind(const stage_kind& kind, factory make);

    // Look up `kind` and invoke its factory with `name`, or throw
    // vc::vc_exception (invalid_argument) on an unknown kind. The factory's
    // result is also checked for null before it is handed back, and a null
    // result throws vc::vc_exception (invalid_argument) too: `make` is an
    // arbitrary caller-supplied std::function (register_kind() stores it
    // as-is, unlike register_stage<StageT>()'s hardcoded make_unique lambda),
    // so this is the boundary where that externally-supplied value enters
    // the system — validated once, here, rather than trusted all the way to
    // vc_pipeline::add() (which only asserts on a null stage, because a
    // make_unique result reaching it can never be null). This is
    // map-lookup-or-throw-or-reject plumbing, NOT a rep — the scaffold writes
    // it in full.
    vc::pipe::stage_ptr create(const stage_kind& kind,
                               vc::pipe::stage_name name) const;

    // Register `StageT` under `kind`, erasing its concrete type behind one
    // uniform session_factory. The session -> params translation is NOT
    // passed in: it is looked up from vc_stage_params<StageT> (see
    // vc_stage_params.h), so a stage type has exactly ONE canonical
    // translation rather than one per call site, and a stage that never
    // stated its translation cannot be registered at all.
    //
    // Concept-enforced at THIS call site (vc_stage_params_req) — a missing or
    // ill-shaped specialization fails to compile HERE, naming StageT, never
    // at create()'s call site and never at runtime. A later registration for
    // the same kind overwrites the earlier one (last-wins), matching
    // register_kind.
    template <typename StageT>
        requires vc_stage_params_req<StageT>
    void register_stage(const stage_kind& kind) {
        require_not_registered_as_paramless(kind);
        session_factories_[kind] =
            [](vc::pipe::stage_name name,
               const vc_edit_session& session) -> vc::pipe::stage_ptr {
            return std::make_unique<StageT>(
                std::move(name),
                vc_stage_params<StageT>::from_session(session));
        };
    }

    // Look up `kind` in the session-aware map and invoke its factory with
    // `name` and `session`, or throw vc::vc_exception (invalid_argument) on
    // an unknown kind. Map-lookup-or-throw plumbing, NOT a rep. Unlike the
    // name-only create() above, this overload does NOT re-check the result
    // for null: session_factories_ is populated exclusively by
    // register_stage<StageT>() above, whose lambda body hardcodes
    // std::make_unique<StageT>(...) — that call either returns a real
    // instance or throws (bad_alloc, or StageT's constructor), never null.
    // Caller-supplied code DOES reach this path — vc_stage_params<StageT> is
    // an open-set trait anyone can specialize (see the trait's own doc
    // comment, and tests/test_vc_edit.cpp's out-of-framework specialization
    // for vc_sample_blur_stage) — but only in the PARAMS position, feeding
    // make_unique's second argument. It cannot make make_unique itself
    // return null the way register_kind()'s caller-supplied factory can:
    // there the caller's return value IS the stage; here it is only an
    // input to a construction that cannot produce a null result. Should a
    // future overload ever accept a caller-supplied session_factory directly
    // (the stage-producing position, not just the params-producing one), the
    // same null-guard would need to move here.
    vc::pipe::stage_ptr create(const stage_kind& kind,
                               vc::pipe::stage_name name,
                               const vc_edit_session& session) const;

    // Whether a factory is registered for `kind` (either registration path).
    bool has(const stage_kind& kind) const noexcept;

  private:
    // Cross-path collision guards. The two registration paths own SEPARATE
    // maps, so register_kind's last-wins rule does not reach across them:
    // without these, one kind string could be registered on both and then
    // resolve to a DIFFERENT stage type depending on which create() overload
    // a caller happened to use, with has() unable to tell them apart. That is
    // a silently-wrong stage rather than an error, so each path rejects a
    // kind the other already claims. (Re-registering on the SAME path is
    // still last-wins — only the cross-path case is a contradiction.)
    void require_not_registered_as_paramless(const stage_kind& kind) const;
    void require_not_registered_as_session_aware(const stage_kind& kind) const;

    std::unordered_map<stage_kind, factory> factories_;
    std::unordered_map<stage_kind, session_factory> session_factories_;
};

} // namespace vc::edit
