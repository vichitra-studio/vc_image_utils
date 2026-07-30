// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_context.h"
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
    //   const auto a = pipe.add(std::make_unique<vc_passthrough_stage>("a"));
    //   const auto b = pipe.add(std::make_unique<vc_passthrough_stage>("b"));
    //   pipe.connect(a, vc_passthrough_stage::slots::out,
    //                b, vc_passthrough_stage::slots::in);
    //
    // PRECONDITION: `pipe` must be non-null. This is a caller programming
    // error, not a runtime condition — checked only via a debug-build
    // assert (see vc_pipeline.cpp), compiled out under NDEBUG.
    stage_name add(stage_ptr pipe);

    // Wire an upstream OUTPUT slot to a downstream INPUT slot, each named by
    // its stage plus a typed slot descriptor:
    //   pipe.connect(a, vc_passthrough_stage::slots::out,
    //                b, vc_passthrough_stage::slots::in);
    // (For a HETEROGENEOUS example — an image->image stage feeding an
    // image->double analyzer — see the vc_sample_* stages in tests/samples/;
    // vc_passthrough_stage is the only stage the library itself ships.)
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

    // Checks every connection against the graph assembled so far, throwing
    // vc::vc_exception on the first problem found. Non-exhaustive list of
    // what's checked (see vc_pipeline.cpp for the exact order):
    //   - UNKNOWN STAGE: a connection naming a stage that was never add()ed
    //     (require_contract() throws stage_not_found);
    //   - duplicate input wiring: two connections targeting the same (stage,
    //     slot) input port;
    //   - TOPOLOGY: since run() executes stages in insertion order (no
    //     topological sort — Sec 12.9), a connection's producing stage must
    //     have been add()ed at a strictly earlier index than its consuming
    //     stage, and a stage may not feed its own input (a self-loop). Any
    //     cycle must contain at least one such backwards edge in a linear
    //     ordering, so this single index comparison also rejects cycles,
    //     with no graph/DAG machinery;
    //   - TYPE contract: the upstream output slot's declared type must equal
    //     the downstream input slot's expected type, e.g.
    //       "connection mean.mean -> grey.rgb: produces double but expects
    //        vc_image".
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
    //     connection is not observable here (use vc::debug::dump() to
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
    // context.run_context().cancelled() — no stage does yet (not even the
    // longest-running one, vc_sample_blur_stage in tests/samples/, whose
    // kernel IS implemented but checks cancellation only between stages).
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

    // One stage's input packet map, resolved from run()'s two sources: for
    // each of `stage_contract`'s declared input slots, take the packet from
    // `inputs` if it is one of this run's caller-supplied open inputs,
    // otherwise from `outputs` via the upstream connection. A fanned-out
    // upstream output is read by more than one downstream input — only the
    // LAST remaining read (per `remaining_reads`) moves the packet out of
    // `outputs`; every earlier read copies (cheap for vc_image: a
    // shared_ptr refcount bump, not a pixel copy). Throws
    // vc::pipe::throw_pipe_run_error if a connected upstream never
    // published its output. Extracted from run()'s per-stage loop body so
    // that loop reads at one level: resolve inputs -> process -> harvest
    // outputs.
    [[nodiscard]] vc_pipe_context::slot_packet_map resolve_stage_inputs(
        const stage_name& stage,
        const vc_pipe_contract& stage_contract,
        render_io_map& inputs,
        render_io_map& outputs,
        std::unordered_map<stage_port, std::size_t>& remaining_reads) const;

    // Filters `outputs` down to exactly `open_output_ports`, in place:
    // throws vc::pipe::throw_pipe_run_error if a declared open output was
    // never published, then erases every entry not in `open_output_ports`
    // (an intermediate output some connection already consumed, and so not
    // observable to run()'s caller). Mirrors find_open_outputs's naming, but
    // operates on the actual packet map rather than just port names. Takes
    // `run_context` to preserve run()'s cancellation checkpoint between the
    // missing-output check and the erase_if — both steps used to be
    // separated by a throw_if_cancelled() call in run()'s own body.
    void harvest_open_outputs(render_io_map& outputs,
                              const stage_port_list& open_output_ports,
                              const vc_render_context& run_context) const;

    // The named stage's contract, or throw stage_not_found. A private query,
    // like vc_pipe_contract's has_slot()/find_slot_type() — used only by
    // validate(), never exposed on the public surface.
    const vc_pipe_contract& require_contract(const stage_name& name) const;

    std::vector<stage_ptr> stages_;
    std::vector<connection> connections_;
    stage_contract_map contracts_;
};

} // namespace vc::pipe
