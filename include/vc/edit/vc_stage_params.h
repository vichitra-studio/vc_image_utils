// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <concepts>
#include <utility>

#include "vc/edit/vc_edit_session.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::edit {

// THE session -> params translation seam, keyed by stage TYPE.
//
// A stage owns WHAT params it needs (its params struct lives beside it in
// vc::pipe). The edit layer owns WHERE those values come from (which slice
// of vc_edit_document). The translation needs BOTH vocabularies, so it can
// only live at the level that already sees both — this one. A stage must
// never grow a from_session() of its own: that would make vc::pipe depend
// on vc::edit, and since vc::edit already depends on vc::pipe (i_pipe,
// vc_pipeline), the two would form a dependency CYCLE. Escalating the
// translation up to here is what keeps the arrow pointing one way.
//
// DECLARED, never DEFINED. vc_stage_params<X> for a stage X with no
// specialization below is an INCOMPLETE type, so registering X on the
// session-aware path fails to compile, naming X. That is the enforcement:
// a stage cannot reach vc_stage_registry::register_stage() without first
// stating how its params come out of a session.
//
// Declared-not-defined also makes this an OPEN SET: a specialization can be
// added from any file — including a test TU, see tests/test_vc_edit.cpp —
// without editing this header or the registry. Same shape as specializing
// std::hash<T> for your own type rather than adding a .hash() member to it.
//
// Each specialization is a plain struct with ONE static:
//   template <> struct vc_stage_params<vc::pipe::vc_some_stage> {
//       static vc::pipe::vc_some_params from_session(const vc_edit_session&);
//   };
// (vc_some_stage is illustrative — no library stage carries params yet.)
// Declare them all HERE (definitions in src/edit/vc_stage_params.cpp) so the
// one header lists every stage's translation, and so every TU that registers
// stages has seen the specialization before it instantiates the primary
// ([temp.expl.spec]/7 makes the reverse order ill-formed, no diagnostic
// required).
//
// Note the failure mode is LOUD, not silent, precisely BECAUSE the primary is
// undefined: a TU that misses the declaration cannot quietly instantiate some
// other definition — it gets an incomplete type, the concept goes false, and
// register_stage() fails to compile naming the stage. Verified by compiling a
// TU that omits the declaration. (An earlier revision of this comment claimed
// the opposite — that the miss would be silent and only surface as a
// cross-TU ODR violation. That was wrong: a DEFINED primary would have that
// hazard; an undefined one converts it into a compile error.)
template <typename StageT> struct vc_stage_params;

// Checks that StageT's slot above is filled in AND that what it produces is
// exactly what StageT's constructor wants alongside the per-instance name.
//
// Naming the UNDEFINED primary here is SFINAE-friendly, not a hard error:
// for an unspecialized stage the concept evaluates cleanly to false and the
// caller gets "constraints not satisfied", naming the stage — verified on
// Apple clang 17 both by static_assert(!vc_stage_params_req<X>) and by the
// register_stage() diagnostic. Clause order is a mild DIAGNOSTIC preference
// only, not a correctness requirement: with the requires-expression first
// clang reports «'vc_stage_params<StageT>::from_session(session)' would be
// invalid: implicit instantiation of undefined template», whereas leading
// with constructible_from reports the blunter «substituted constraint
// expression is ill-formed». Both name the offending stage type; both
// compile. (Measured, not assumed — an earlier revision of this comment
// claimed the swapped order broke outright, which is false.)
//
// CAVEAT: that SFINAE-friendliness is verified on Apple clang 17 ONLY — no
// GCC or MSVC is available in this environment to check against. It is
// believed standard-conforming ([expr.prim.req]: a requirement whose
// substitution yields an invalid expression makes the requires-expression
// false), and it is the same mechanism common detection idioms rely on, but
// treat a first build on another toolchain as the real confirmation. The
// tests/test_vc_edit.cpp static_asserts will catch a divergence immediately.
//
// The params type is never spelled here — decltype asks the trait what it
// returns and threads that straight into the constructor check, so adding a
// stage never means restating its params type (the same no-restating
// property the removed builder-argument concept had).
template <typename StageT>
concept vc_stage_params_req =
    requires(const vc_edit_session& session) {
        vc_stage_params<StageT>::from_session(session);
    } && std::constructible_from<StageT,
                                 vc::pipe::stage_name,
                                 decltype(vc_stage_params<StageT>::from_session(
                                     std::declval<const vc_edit_session&>()))>;

// ---- specializations ----
//
// None yet: no LIBRARY stage carries params today (vc_passthrough_stage is
// paramless; the blur/grayscale/mean worked examples live in tests/samples/
// and specialize this locally). The first production stage with params adds
// its declaration here and its body in src/edit/vc_stage_params.cpp.

} // namespace vc::edit
