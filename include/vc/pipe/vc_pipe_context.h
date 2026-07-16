// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <unordered_map>
#include <utility>

#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::pipe {

// The per-run data carrier handed to a pipe's process(). It holds the input
// packets the pipe reads and collects the output packets it publishes; inputs
// and outputs are separate maps, so a stage may reuse a name across the two
// directions.
//
// The surface is split by audience, and NO name-keyed method is public
// (docs/pipe_design.md Sec 12.5):
//   author-facing (a stage's process() body) — get_input(slot<T>) /
//                    set_output(slot<T>): a stage reads its inputs and writes
//                    its outputs through its OWN typed slot descriptors, so T
//                    is deduced and never restated, and no raw name is ever
//                    spelled.
//   runner-facing (vc_pipeline::run()) — the map CONSTRUCTOR injects a stage's
//                    inputs in one coarse handoff, and take_outputs() harvests
//                    all of them at once. The runner works name-keyed on plain
//                    maps it owns, never through a per-slot setter/getter here.
// The two string-keyed lowering targets are private: reachable only through a
// slot<T>, never on the surface a stage author holds.
//
// Written in full so stages have a stable API to build against; the reps are
// the stage process() bodies and the pipeline runner that drives contexts.
class vc_pipe_context {
  public:
    vc_pipe_context() = default;

    // The runner builds a stage's input packets (open inputs + upstream
    // outputs) into one map keyed by input-slot name and constructs the context
    // from it. This whole-map handoff is the ONLY way inputs enter a context —
    // there is no per-slot public setter — so the name-keyed surface never
    // reaches a stage author.
    explicit vc_pipe_context(
        std::unordered_map<slot_name, vc_pipe_packet> inputs)
        : inputs_(std::move(inputs)) {
    }

    // ---- author-facing: a stage's process() body ----

    // Typed read: `ctx.get_input(slots::rgb)` yields a `const vc_image&`. The
    // type comes from the descriptor, so it is never spelled at the call site.
    // Throws vc::vc_exception if the slot is unbound or holds a different type.
    template <typename T> const T& get_input(slot<T> s) const {
        return get_input(slot_name{s.name}).template get<T>();
    }

    // Typed publish: `ctx.set_output(slots::grey, vc_pipe_packet{result})`. The
    // descriptor names the slot; the packet carries the value.
    template <typename T> void set_output(slot<T> s, vc_pipe_packet packet) {
        set_output(slot_name{s.name}, std::move(packet));
    }

    // ---- runner-facing: vc_pipeline::run() ----

    // Harvest every published output at once, keyed by output-slot name.
    // Rvalue-qualified: the runner calls it on `std::move(ctx)` after
    // process(), moving the packets out — a stage's outputs are read exactly
    // once.
    std::unordered_map<slot_name, vc_pipe_packet> take_outputs() && {
        return std::move(outputs_);
    }

  private:
    // Lowering targets of the typed methods above — the name-keyed access lives
    // here, private, reached only through a slot<T>. Never on the public
    // surface a stage author holds. get_input throws vc::vc_exception if the
    // slot is unbound.
    const vc_pipe_packet& get_input(const slot_name& name) const;
    void set_output(const slot_name& name, vc_pipe_packet packet);

    std::unordered_map<slot_name, vc_pipe_packet> inputs_;
    std::unordered_map<slot_name, vc_pipe_packet> outputs_;
};

} // namespace vc::pipe
