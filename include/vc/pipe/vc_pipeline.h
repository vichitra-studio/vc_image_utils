// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_render_context.h"

namespace vc::pipe {

// A set of named pipes plus the wiring between their slots. The runner is
// LINEAR (stages execute in insertion order); a general DAG topological sort is
// [LATER] — the interface does not preclude it, but this runner does not
// implement it (docs/pipe_design.md Sec 7, Sec 12.4).
//
// Lifecycle: assemble (add + connect) -> validate() -> run().
class vc_pipeline {
  public:
    // Take ownership of a stage. The stage carries its OWN name (i_pipe::name),
    // so there is no separate name argument. Returns that name so wiring can
    // refer to the stage without restating the string literal:
    //   const auto grey =
    //   pipe.add(std::make_unique<vc_grayscale_stage>("grey"));
    //   pipe.connect(grey, vc_grayscale_stage::slots::grey,
    //                mean, vc_mean_brightness_stage::slots::image);
    stage_name add(stage_ptr pipe);

    // Wire an upstream OUTPUT slot to a downstream INPUT slot, each named by
    // its stage plus a typed slot descriptor:
    //   pipe.connect(grey, vc_grayscale_stage::slots::grey,
    //                mean, vc_mean_brightness_stage::slots::image);
    // Taking slot<T> (not a raw slot name) keeps the wiring typo-safe and keeps
    // the name-keyed stage_port ctor off the assembly surface — every wire is
    // spelled through a stage's `slots::` members (Sec 12.5).
    //
    // The two slot types are INDEPENDENT — this is NOT a compile-time type
    // check. `vc_image_utils` is a runtime-composition library, so a mismatch
    // across a connection is caught at assembly time by validate(), on both the
    // code-wired and config-wired paths (Sec 6); connect just lowers each
    // descriptor to its stage_port coordinate.
    template <typename Tout, typename Tin>
    void connect(stage_name from_stage,
                 slot<Tout> from_slot,
                 stage_name to_stage,
                 slot<Tin> to_slot) {
        connections_.push_back(
            connection{.from = stage_port{std::move(from_stage), from_slot},
                       .to = stage_port{std::move(to_stage), to_slot}});
    }

    // Run every pipe's declare() to gather its slot contracts, then check every
    // connection's TYPE contract: the upstream output slot's declared type must
    // equal the downstream input slot's expected type. Throw vc::vc_exception
    // with a clear message on the first mismatch, e.g.
    //   "connection mean.mean -> grey.rgb: produces double but expects
    //    vc_image".
    // No pixels flow here — this compares declarations only (Sec 6). The data
    // contract (vc_image_spec) is [LATER]; only the type layer is checked now.
    void validate() const;

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
    //
    // `run_context` is the run's CONTROL-ONLY host (see vc_render_context.h)
    // — cancellation now, checked BETWEEN stages by run()'s body, progress
    // [LATER]. This SUPERSEDES the bare `const vc_cancellation_token&`
    // signature; it is a trailing DEFAULT so every existing one-argument
    // call site binds a never-cancelled context and is unaffected — this
    // change is purely additive. The context is threaded through
    // i_pipe::process()/vc_pipe_context (each per-stage vc_pipe_context is
    // built with a copy of `run_context`), so a stage MAY add an
    // in-process cancellation checkpoint by reading
    // context.run_context().cancelled() — no stage does yet
    // (vc_blur_stage::process() stays a rep shell).
    render_io_map run(render_io_map inputs,
                      const vc_render_context& run_context = {}) const;

  private:
    // The wiring lives here, not on any contract: a connection joins two
    // stages, so it is graph-global topology that only exists once stages are
    // assembled. A per-stage contract cannot hold it.
    struct connection {
        stage_port from; // upstream stage's output port
        stage_port to;   // downstream stage's input port
    };

    // The upstream (stage, slot) wired into this input port, or nullopt if
    // nothing connects to it (an open input, fed by run()'s caller instead).
    [[nodiscard]] std::optional<stage_port>
    upstream_of(const stage_port& to_port) const;

    // Whether any connection consumes this output port (so it is NOT open).
    [[nodiscard]] bool has_consumer(const stage_port& from_port) const;

    // `input_slots` is one stage's declared input slot names
    // (contract.input_slot_names()) — returns just the OPEN ones (no
    // connection feeds them), as (stage, slot) ports. Named distinctly from
    // run()'s local open_input_ports/open_output_ports vectors (which these
    // feed) so nothing shadows a member function of the same name.
    stage_port_list
    find_open_inputs(const stage_name& stage,
                     const std::vector<slot_name>& input_slots) const;

    // Mirrors find_open_inputs for the output side (contract.output_slot_
    // names()), using has_consumer() instead of upstream_of().
    stage_port_list
    find_open_outputs(const stage_name& stage,
                      const std::vector<slot_name>& output_slots) const;

    // The named stage's contract, or throw stage_not_found. A private query,
    // like vc_pipe_contract's has_slot()/find_slot_type() — used only by
    // validate(), never exposed on the public surface.
    const vc_pipe_contract& require_contract(const stage_name& name) const;

    std::vector<stage_ptr> stages_;
    std::vector<connection> connections_;
    stage_contract_map contracts_;
};

} // namespace vc::pipe
