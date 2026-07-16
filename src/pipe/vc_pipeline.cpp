// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/vc_pipeline.h"

#include <utility>

// TODO(you): implementing validate() and run() will need:
//   #include "vc/pipe/vc_pipe_contract.h"          // the queryable contract
//   #include "vc/pipe/vc_pipe_contract_builder.h"  // declare() writes through
//   it #include "vc/pipe/vc_pipe_context.h"           // a context per stage in
//   run() #include "vc/vc_error_code.h"                  // throw on a contract
//   mismatch #include "vc/vc_exception.h"
//
// connect() is a template, so it lives in the header — nothing to define here.

namespace vc::pipe {

stage_name vc_pipeline::add(std::unique_ptr<i_pipe> pipe) {
    stage_name name = pipe->name(); // copy the name before the pipe is moved
    stages_.push_back(std::move(pipe));
    return name;
}

const i_pipe* vc_pipeline::find_stage(const stage_name& name) const {
    for (const auto& pipe : stages_) {
        if (pipe->name() == name) {
            return pipe.get();
        }
    }
    return nullptr;
}

void vc_pipeline::validate() const {
    // TODO(you): the assembly-time TYPE check (docs/pipe_design.md Sec
    // 6, 12.5).
    //   1. For each stage, make a vc_pipe_contract, wrap it in a
    //      contract_builder, and call stage->declare(builder). Keep the
    //      contracts (e.g. a map stage_name -> vc_pipe_contract) so you can
    //      look slots up per connection. The stage's name is stage->name().
    //   2. For each connection {from, to} (each a stage_port {stage, slot}):
    //        - find_stage(from.stage) / find_stage(to.stage); if either is
    //          missing, throw vc::vc_exception(invalid_argument,
    //          "...unknown stage...").
    //        - look up the upstream OUTPUT type
    //          (contract.output_slot_type(from.slot)) and the downstream INPUT
    //          type (contract.input_slot_type(to.slot)). Each returns a
    //          std::type_index and THROWS vc::vc_exception if the slot was
    //          never declared, so you never branch on "not found".
    //        - if the two type_index values differ, throw a clear message, e.g.
    //            "connection " + from.stage + "." + from.slot + " -> " +
    //            to.stage + "." + to.slot + ": produces <out> but expects
    //            <in>".
    //          (std::type_index has .name() for the message.)
    //   The data contract (vc_image_spec) is [LATER]; only the type layer here.
}

std::unordered_map<stage_port, vc_pipe_packet>
vc_pipeline::run(std::unordered_map<stage_port, vc_pipe_packet> inputs) const {
    // TODO(you): the linear runner, MAP VARIANT (docs/pipe_design.md Sec 12.5).
    //   1. Gather contracts: for each stage, wrap a fresh vc_pipe_contract in a
    //      contract_builder, declare() into it, and keep the contract (you need
    //      its slot lists to find the open ports).
    //   2. Open inputs  = every (stage, input-slot) that is NOT some
    //      connection's `to` port. Open outputs = every (stage, output-slot)
    //      that is NOT some connection's `from` port. Build these from each
    //      contract's input_slot_names()/output_slot_names() minus
    //      connections_.
    //   3. Check `inputs` covers EXACTLY the open inputs (no missing, no extra
    //      key) — otherwise throw vc::vc_exception(invalid_argument, ...).
    //   4. Walk stages_ in insertion order. For each stage:
    //        - gather its input packets into a
    //          std::unordered_map<slot_name, vc_pipe_packet>: for each open
    //          input port on this stage MOVE the caller's packet out of
    //          `inputs`; for each connection whose `to.stage` is this stage,
    //          copy the upstream stage's published output for from.slot.
    //        - construct vc_pipe_context{std::move(stage_inputs)} and call
    //          stage->process(ctx).
    //        - harvest with `auto outs = std::move(ctx).take_outputs();` and
    //          stash `outs` (keyed by stage_name) so downstream stages and the
    //          final harvest can read this stage's outputs.
    //   5. Harvest: for each open output port, read the producing stage's
    //   stashed
    //      output for that slot into the result map keyed by that stage_port.
    //
    // Placeholder so the scaffold links. Returning an empty map makes the run()
    // tests fail loudly until this is written (same convention as
    // tests/test_vc_image.cpp).
    (void)inputs;
    return {};
}

} // namespace vc::pipe
