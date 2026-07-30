// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_errors.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_pipeline.h"
#include "vc/pipe/vc_render_context.h"

namespace vc::pipe {

stage_name vc_pipeline::add(stage_ptr pipe) {
    // Every legitimate construction path hands add() a non-null stage:
    // std::make_unique throws bad_alloc rather than returning null, and
    // vc_stage_registry::create() never lets a null stage escape either, on
    // either overload. The name-only overload (register_kind()'s arbitrary
    // caller-supplied std::function) cannot hand one back: it checks its
    // factory's return value itself and throws vc::vc_exception if that
    // value is null, so a hostile/buggy factory is already turned into a
    // throw — never into a null reaching here. The session-aware overload
    // (register_stage<StageT>()'s hardcoded make_unique lambda) needs no
    // such check in the first place: that map is populated exclusively by
    // make_unique, which returns a real instance or throws, never null. A
    // null here — add(nullptr), a default-constructed stage_ptr, a
    // double-moved-from stage_ptr — is therefore always a caller
    // programming error, never a legitimate runtime state, so a debug
    // assertion is the right tool: unlike vc_edit_session's constructor
    // (which throws on a null meta_, because that null CAN legitimately
    // arrive from runtime data via a caller with an absent backend), there
    // is no runtime path that should reach this with pipe == nullptr.
    assert(pipe && "vc_pipeline::add() called with a null stage");
    stage_name name = pipe->name();
    if (contracts_.find(name) != contracts_.end()) {
        throw vc::vc_exception(
            vc::vc_error_code::stage_already_added,
            "vc_pipeline::add(): stage name already added: " + name);
    }
    vc_pipe_contract contract;
    pipe->declare(contract);
    // Strong exception guarantee: do all failable work (the reserve and the
    // emplace, either of which may throw bad_alloc) BEFORE the one operation
    // that commits `pipe` into stages_. Once capacity is reserved,
    // push_back on a noexcept-movable unique_ptr cannot throw, so there is
    // no window where a stage lands in stages_ without a matching entry in
    // contracts_ (an invariant run()'s contracts_.at() call relies on).
    stages_.reserve(stages_.size() + 1);
    contracts_.emplace(name, std::move(contract));
    stages_.push_back(std::move(pipe));
    return name;
}

const vc_pipe_contract&
vc_pipeline::require_contract(const stage_name& name) const {
    auto it = contracts_.find(name);
    if (it == contracts_.end()) {
        throw vc::vc_exception(vc::vc_error_code::stage_not_found,
                               "vc_pipeline::validate() unknown stage: " +
                                   name);
    }
    return it->second;
}

void vc_pipeline::validate() const {
    // Stage name -> its index in stages_ (i.e. its position in add()
    // insertion order, which is also run()'s execution order — the runner is
    // LINEAR, no topological sort; docs/pipe_design.md Sec 12.9). Built once
    // here rather than added as a persistent member: it is only ever needed
    // during validate(), and stages_ already holds the one canonical
    // ordering (Sec 12.9's "don't store the same fact twice").
    std::unordered_map<stage_name, std::size_t> position;
    position.reserve(stages_.size());
    for (std::size_t i = 0; i < stages_.size(); ++i) {
        position.emplace(stages_[i]->name(), i);
    }

    // A declared input slot holds exactly one packet, so at most one
    // connection may target it — two connections wired into the same
    // (stage, slot) input port is a malformed graph, not a merge, and must
    // be rejected here rather than left for run() to resolve by insertion
    // order.
    std::unordered_set<stage_port> seen_inputs;
    for (const auto& conn : connections_) {
        const auto& from_stage = conn.from.stage;
        const auto& to_stage = conn.to.stage;
        const auto& from_slot = conn.from.slot;
        const auto& to_slot = conn.to.slot;

        if (!seen_inputs.insert(conn.to).second) {
            throw vc::vc_exception(
                vc::vc_error_code::pipe_input_already_connected,
                "vc_pipeline::validate() input slot already wired: " +
                    to_stage + "." + to_slot);
        }

        // require_contract() throws stage_not_found for a connection naming
        // a stage that was never add()ed — run these lookups BEFORE the
        // topology check below, so an unknown-stage graph gets that clear
        // diagnostic instead of a confusing "position lookup missing" one
        // (position only knows about add()ed stages, same set as
        // contracts_).
        const auto& from_contract = require_contract(from_stage);
        const auto& to_contract = require_contract(to_stage);

        // The runner executes stages in INSERTION order, not graph order
        // (Sec 12.9 deliberately defers a topological sort) — so a
        // connection only runs correctly "forwards": the producing stage
        // must have been add()ed at a strictly earlier index than the
        // consuming stage. Any cycle in a linear ordering must contain at
        // least one backwards edge, so this single check also rejects
        // cycles for free, with no graph/DAG machinery. Checked here, before
        // the type check below: a backwards wire can never run regardless of
        // whether its types happen to match, so the topology diagnostic is
        // the more useful of the two to report first.
        const std::size_t from_pos = position.at(from_stage);
        const std::size_t to_pos = position.at(to_stage);
        if (from_stage == to_stage) {
            // A self-loop: from_pos == to_pos always holds, so the general
            // "added before" wording below would be false here (nothing was
            // added before anything — one stage feeds itself). Give it its
            // own message rather than let a technically-true-by-accident
            // comparison produce a misleading one.
            throw vc::vc_exception(
                vc::vc_error_code::pipe_invalid_topology,
                "vc_pipeline::validate() self-loop: " + from_stage + "." +
                    from_slot + " -> " + to_stage + "." + to_slot +
                    ": a stage cannot consume its own output — the runner "
                    "executes stages once, in insertion order, not in a "
                    "fixed-point loop");
        }
        if (!(from_pos < to_pos)) {
            throw vc::vc_exception(
                vc::vc_error_code::pipe_invalid_topology,
                "vc_pipeline::validate() backwards connection: " + from_stage +
                    "." + from_slot + " -> " + to_stage + "." + to_slot + ": " +
                    to_stage + " was add()ed before " + from_stage +
                    ", but the runner executes stages in insertion order, so " +
                    to_stage + " would run before " + from_stage +
                    " ever produces this output");
        }

        std::type_index from_type = from_contract.output_slot_type(from_slot);
        std::type_index to_type = to_contract.input_slot_type(to_slot);
        if (from_type != to_type) {
            throw vc::vc_exception(
                vc::vc_error_code::pipe_connection_mismatch,
                "vc_pipeline::validate() connection type mismatch: " +
                    from_stage + "." + from_slot + " -> " + to_stage + "." +
                    to_slot + ": produces " + from_type.name() +
                    " but expects " + to_type.name());
        }
    }
}

std::optional<stage_port>
vc_pipeline::upstream_of(const stage_port& to_port) const {
    for (const auto& conn : connections_) {
        if (conn.to == to_port) {
            return conn.from;
        }
    }
    return std::nullopt;
}

bool vc_pipeline::has_consumer(const stage_port& from_port) const {
    for (const auto& conn : connections_) {
        if (conn.from == from_port) {
            return true;
        }
    }
    return false;
}

stage_port_list
vc_pipeline::find_open_inputs(const stage_name& stage,
                              const std::vector<slot_name>& input_slots) const {
    stage_port_list open;
    for (const auto& slot : input_slots) {
        stage_port port{stage, slot};
        if (!upstream_of(port)) {
            open.push_back(port);
        }
    }
    return open;
}

stage_port_list vc_pipeline::find_open_outputs(
    const stage_name& stage, const std::vector<slot_name>& output_slots) const {
    stage_port_list open;
    for (const auto& slot : output_slots) {
        stage_port port{stage, slot};
        if (!has_consumer(port)) {
            open.push_back(port);
        }
    }
    return open;
}

vc_pipe_context::slot_packet_map vc_pipeline::resolve_stage_inputs(
    const stage_name& stage,
    const vc_pipe_contract& stage_contract,
    render_io_map& inputs,
    render_io_map& outputs,
    std::unordered_map<stage_port, std::size_t>& remaining_reads) const {
    vc::pipe::vc_pipe_context::slot_packet_map stage_inputs;

    for (const auto& input_slot : stage_contract.input_slot_names()) {
        stage_port port{stage, input_slot};
        auto input_itr = inputs.find(port);
        if (input_itr != inputs.end()) {
            stage_inputs.emplace(input_slot, std::move(input_itr->second));
        } else if (auto upstream = upstream_of(port)) {
            auto upstream_output_itr = outputs.find(*upstream);
            if (upstream_output_itr != outputs.end()) {
                if (--remaining_reads.at(*upstream) == 0) {
                    stage_inputs.emplace(
                        input_slot, std::move(upstream_output_itr->second));
                } else {
                    stage_inputs.emplace(input_slot,
                                         upstream_output_itr->second);
                }
            } else {
                throw_pipe_run_error("missing output from upstream stage: " +
                                     upstream->stage + "." + upstream->slot);
            }
        }
    }

    return stage_inputs;
}

void vc_pipeline::harvest_open_outputs(
    render_io_map& outputs,
    const stage_port_list& open_output_ports,
    const vc_render_context& run_context) const {
    for (const auto& port : open_output_ports) {
        if (outputs.find(port) == outputs.end()) {
            throw_pipe_run_error("missing output port slot: " + port.stage +
                                 "." + port.slot);
        }
    }

    throw_if_cancelled(run_context);
    std::erase_if(outputs, [&](const auto& pair) {
        return std::ranges::find(open_output_ports, pair.first) ==
               open_output_ports.end();
    });
}

render_io_map vc_pipeline::run(render_io_map inputs,
                               const vc_render_context& run_context) const {
    stage_port_list open_input_ports;
    stage_port_list open_output_ports;
    for (const auto& contract : contracts_) {
        // Named `current_stage`, not `stage_name`: the latter shadows the
        // `vc::pipe::stage_name` type alias for the rest of this scope.
        const auto& current_stage = contract.first;
        const auto& stage_contract = contract.second;

        auto ins =
            find_open_inputs(current_stage, stage_contract.input_slot_names());
        open_input_ports.insert(open_input_ports.end(), ins.begin(), ins.end());

        auto outs = find_open_outputs(current_stage,
                                      stage_contract.output_slot_names());
        open_output_ports.insert(open_output_ports.end(), outs.begin(),
                                 outs.end());
    }

    throw_if_cancelled(run_context);
    for (auto& input : inputs) {
        const auto& port = input.first;
        auto input_itr = std::ranges::find(open_input_ports, port);

        if (input_itr == open_input_ports.end()) {
            throw_pipe_run_error("unexpected input port slot: " + port.stage +
                                 "." + port.slot);
        }

        open_input_ports.erase(input_itr);
    }
    if (!open_input_ports.empty()) {
        throw_pipe_run_error("missing input port slots packets");
    }

    // A stage's output CAN feed more than one downstream input (fan-out) —
    // nothing restricts a connection's `from` to being used once. `outputs`
    // lives for the whole run() call, so the first consumer to std::move a
    // shared value out of it would leave the entry moved-from for every
    // later consumer. remaining_reads counts, per upstream port, how many
    // connections still need to read it; only the LAST read moves, every
    // earlier one copies (cheap for vc_image: it holds its buffer via
    // shared_ptr, so a copy is a refcount bump, not a pixel copy). Built
    // fresh here from connections_ — local to this call, nothing persistent
    // to keep in sync or go stale.
    std::unordered_map<stage_port, std::size_t> remaining_reads;
    for (const auto& conn : connections_) {
        ++remaining_reads[conn.from];
    }

    throw_if_cancelled(run_context);
    render_io_map outputs;
    for (const auto& pipe : stages_) {
        // Named `stage_id`, not `stage_name`: the latter shadows the
        // `vc::pipe::stage_name` type alias for the rest of this scope.
        const auto& stage_id = pipe->name();
        const auto& stage_contract = contracts_.at(stage_id);

        auto stage_inputs = resolve_stage_inputs(
            stage_id, stage_contract, inputs, outputs, remaining_reads);

        throw_if_cancelled(run_context);

        vc_pipe_context context{std::move(stage_inputs), run_context};
        pipe->process(context);

        auto stage_outputs = std::move(context).take_outputs();
        for (auto& output : stage_outputs) {
            stage_port port{stage_id, output.first};
            outputs.emplace(port, std::move(output.second));
        }
    }

    throw_if_cancelled(run_context);
    harvest_open_outputs(outputs, open_output_ports, run_context);

    throw_if_cancelled(run_context);
    return outputs;
}

} // namespace vc::pipe
