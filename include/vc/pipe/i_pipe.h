// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include "vc/pipe/vc_pipe_types.h"

namespace vc::pipe {

// Forward declarations keep the interface header light: a pipe only names
// these types by reference, so their full definitions are needed only in the
// .cpp that implements a concrete stage.
class vc_pipe_contract;
class vc_pipe_context;

// A pipe is one image-processing stage — the unit a pipeline chains together.
// It is a thin ABSTRACT BASE, not a pure interface: it carries one field, the
// stage's per-instance name (docs/pipe_design.md Sec 12.2).
//
// Two kinds of identity:
//   name() — PER-INSTANCE, stored, set through the protected ctor. The stage's
//            address *in a particular graph*; per-instance because one graph
//            can hold several stages of the same type (e.g. N `align` stages in
//            an HDR merge). add() and connect() key off it.
//   kind() — PER-TYPE, a stable literal ("grayscale") for type identity: the
//            CLI registry / config factory / logging. No Phase-1 runner uses
//            it yet; it is kept as the seam those consumers will need.
//
// Two responsibilities, split so the graph can be checked before it is run
// (Sec 4.4):
//   declare() — announce this pipe's input/output slots and their type
//               contracts. Called once at graph-assembly time. Moves no data.
//   process() — do the work: read input packets from the context, publish
//               output packets. Called once per run, after validation passed.
//
// Pipes are stateless per run: all configuration (a blur radius, a merge
// algorithm choice) is captured at construction and held as private members,
// so process() needs no parameter block (Sec 4.4). declare()/process() are
// const for that reason.
class i_pipe {
  public:
    virtual ~i_pipe() = default;

    // The stage's name within its graph. Set at construction, never changes.
    const stage_name& name() const noexcept {
        return name_;
    }

    // The stage TYPE's stable identifier (a literal). Per-type, not per-run.
    virtual const char* kind() const = 0;

    // This instance's params identity, as a hand-written hash combine (see
    // e.g. tests/samples/vc_sample_blur_stage.cpp) — PER-INSTANCE, unlike
    // kind(), since
    // two stages of the same kind can carry different params. PURE, matching
    // validate_inputs()'s "no stage inherits a silent default" rationale: a
    // paramless stage still states its answer explicitly (a fixed 0), rather
    // than inheriting one. No consumer yet — kept as the seam a future
    // content-hash cache will need, mirroring kind()'s own "no consumer yet"
    // framing above.
    virtual std::size_t params_hash() const = 0;

    virtual void declare(vc_pipe_contract& contract) const = 0;

    // The pipeline's one per-stage, per-run entry point. NON-virtual, so this
    // ordering cannot be bypassed by a stage: validate_inputs() always runs
    // before do_process(), because a stage only ever overrides the two
    // protected steps below, never this wrapper.
    void process(vc_pipe_context& context) const {
        validate_inputs(context);
        do_process(context);
    }

  protected:
    // Only a concrete stage (forwarding its own ctor arg) can name an instance.
    explicit i_pipe(stage_name name) : name_(std::move(name)) {
    }

    // Domain-specific input invariants beyond type — e.g. a blur radius must
    // be positive, or two image inputs must share dimensions. PURE: every
    // stage must say explicitly whether it has one, even if the answer is an
    // empty body — no stage inherits a silent default. Read inputs here via
    // context.get_input(slot<T>) exactly as in do_process() — its typed
    // extraction proves a slot's type as a side effect of checking its value,
    // for whichever slots this override reads.
    virtual void validate_inputs(const vc_pipe_context& context) const = 0;

    // The stage's actual work: read inputs, publish outputs. What a concrete
    // stage overrides; renamed from `process`, now this class's non-virtual
    // wrapper above.
    virtual void do_process(vc_pipe_context& context) const = 0;

  private:
    stage_name name_;
};

// An owned pipeline stage. The registry factory and vc_pipeline both traffic
// in a stage's owning handle; naming it here (right after the class it
// wraps) gives that handle a domain vocabulary instead of the raw
// std::unique_ptr<i_pipe> spelling.
using stage_ptr = std::unique_ptr<i_pipe>;

} // namespace vc::pipe
