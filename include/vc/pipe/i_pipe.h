// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <utility>

#include "vc/pipe/vc_pipe_types.h"

namespace vc::pipe {

// Forward declarations keep the interface header light: a pipe only names
// these types by reference, so their full definitions are needed only in the
// .cpp that implements a concrete stage. declare() is handed a contract_builder
// (the narrow author-facing view), NOT the full vc_pipe_contract — that stays
// framework-internal (docs/pipe_design.md Sec 12.5).
class contract_builder;
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

    virtual void declare(contract_builder& contract) const = 0;
    virtual void process(vc_pipe_context& context) const = 0;

  protected:
    // Only a concrete stage (forwarding its own ctor arg) can name an instance.
    explicit i_pipe(stage_name name) : name_(std::move(name)) {
    }

  private:
    stage_name name_;
};

} // namespace vc::pipe
