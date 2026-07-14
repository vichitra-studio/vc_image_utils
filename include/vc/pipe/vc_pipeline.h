// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::pipe {

// A set of named pipes plus the wiring between their slots. The runner is
// LINEAR (stages execute in insertion order); a general DAG topological sort is
// [LATER] — the interface does not preclude it, but this runner does not
// implement it (docs/pipe_design.md Sec 7, Sec 12.4).
//
// Lifecycle: assemble (add + connect) -> validate() -> run().
class vc_pipeline {
  public:
    // ---- assemble (implemented) ----

    // Take ownership of a stage. The stage carries its OWN name (i_pipe::name),
    // so there is no separate name argument. Returns that name so wiring can
    // refer to the stage without restating the string literal:
    //   const auto grey =
    //   pipe.add(std::make_unique<vc_grayscale_stage>("grey"));
    //   pipe.connect({grey, vc_grayscale_stage::slots::grey},
    //                {mean, vc_mean_brightness_stage::slots::image});
    stage_name add(std::unique_ptr<i_pipe> pipe);

    // Wire an upstream OUTPUT port to a downstream INPUT port. A stage_port is
    // {stage, slot}; build each from a stage name and a typed slot descriptor —
    // `{from, some_stage::slots::out}` — which keeps the slot typo-safe and
    // carries its type, while making `stage_port` the single wiring currency
    // the map-variant run() also speaks.
    //
    // No compile-time type check: this is a runtime-composition library, so a
    // type mismatch across a connection is caught at assembly time by
    // validate(), on both the code-wired and config-wired paths (Sec 6).
    void connect(stage_port from, stage_port to);

    // ---- validate (TODO(you)) ----
    //
    // Run every pipe's declare() to gather its slot contracts, then check every
    // connection's TYPE contract: the upstream output slot's declared type must
    // equal the downstream input slot's expected type. Throw vc::vc_exception
    // with a clear message on the first mismatch, e.g.
    //   "connection mean.mean -> grey.rgb: produces double but expects
    //    vc_image".
    // No pixels flow here — this compares declarations only (Sec 6). The data
    // contract (vc_image_spec) is [LATER]; only the type layer is checked now.
    void validate() const;

    // ---- run (TODO(you)) ----
    //
    // The MAP VARIANT (Sec 12.2). Inject the caller's packets onto the graph's
    // OPEN INPUTS (input slots no connection feeds), execute stages in
    // insertion order, then harvest the OPEN OUTPUTS (output slots no
    // connection consumes) into the returned map.
    //   - `inputs` must cover EXACTLY the open inputs: a missing or extra key
    //     is an error.
    //   - Only open outputs are returned; an intermediate output consumed by a
    //     connection is not observable here (use vc::utils::debug::dump() to
    //     inspect mid-pipeline).
    // "Open" is computed by subtracting the connected ports from each stage's
    // declared slots (contract.input_slot_names()/output_slot_names()).
    //
    // Taken by value as a SINK: the runner MOVES each packet out of the map and
    // into a stage's context, so an rvalue argument avoids a deep image copy
    // (an lvalue caller copies at the call site, knowingly).
    std::unordered_map<stage_port, vc_pipe_packet>
    run(std::unordered_map<stage_port, vc_pipe_packet> inputs) const;

  private:
    // The wiring lives here, not on any contract: a connection joins two
    // stages, so it is graph-global topology that only exists once stages are
    // assembled. A per-stage contract cannot hold it.
    struct connection {
        stage_port from; // upstream stage's output port
        stage_port to;   // downstream stage's input port
    };

    const i_pipe* find_stage(const stage_name& name) const;

    std::vector<std::unique_ptr<i_pipe>> stages_;
    std::vector<connection> connections_;
};

} // namespace vc::pipe
