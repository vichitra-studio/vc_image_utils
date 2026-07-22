// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_pipeline.h"
#include "vc/pipe/vc_render_context.h"

namespace vc::pipe {

stage_name vc_pipeline::add(stage_ptr pipe) {
    stage_name name = pipe->name();
    if (contracts_.find(name) != contracts_.end()) {
        throw vc::vc_exception(
            vc::vc_error_code::stage_already_added,
            "vc_pipeline::add(): stage name already added: " + name);
    }
    vc_pipe_contract contract;
    pipe->declare(contract);
    stages_.push_back(std::move(pipe));
    contracts_.emplace(name, contract);
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

        const auto& from_contract = require_contract(from_stage);
        const auto& to_contract = require_contract(to_stage);
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

render_io_map vc_pipeline::run(render_io_map inputs,
                               const vc_render_context& run_context) const {
    stage_port_list open_input_ports;
    stage_port_list open_output_ports;
    for (const auto& contract : contracts_) {
        const auto& stage_name = contract.first;
        const auto& stage_contract = contract.second;

        auto ins =
            find_open_inputs(stage_name, stage_contract.input_slot_names());
        open_input_ports.insert(open_input_ports.end(), ins.begin(), ins.end());

        auto outs =
            find_open_outputs(stage_name, stage_contract.output_slot_names());
        open_output_ports.insert(open_output_ports.end(), outs.begin(),
                                 outs.end());
    }

    throw_if_cancelled(run_context);
    for (auto& input : inputs) {
        const auto& port = input.first;
        auto input_itr = std::ranges::find(open_input_ports, port);

        if (input_itr == open_input_ports.end()) {
            vc::throw_pipe_run_error(
                "unexpected input port slot: " + port.stage + "." + port.slot);
        }

        open_input_ports.erase(input_itr);
    }
    if (!open_input_ports.empty()) {
        vc::throw_pipe_run_error("missing input port slots packets");
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
        const auto& stage_name = pipe->name();
        const auto& stage_contract = contracts_.at(stage_name);

        vc::pipe::vc_pipe_context::slot_packet_map stage_inputs;

        for (const auto& input_slot : stage_contract.input_slot_names()) {
            stage_port port{stage_name, input_slot};
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
                    vc::throw_pipe_run_error(
                        "missing output from upstream stage: " +
                        upstream->stage + "." + upstream->slot);
                }
            }
        }

        throw_if_cancelled(run_context);

        vc_pipe_context context{std::move(stage_inputs), run_context};
        pipe->process(context);

        auto stage_outputs = std::move(context).take_outputs();
        for (auto& output : stage_outputs) {
            stage_port port{stage_name, output.first};
            outputs.emplace(port, std::move(output.second));
        }
    }

    throw_if_cancelled(run_context);
    for (const auto& port : open_output_ports) {
        if (outputs.find(port) == outputs.end()) {
            vc::throw_pipe_run_error("missing output port slot: " + port.stage +
                                     "." + port.slot);
        }
    }

    throw_if_cancelled(run_context);
    std::erase_if(outputs, [&](const auto& pair) {
        return std::ranges::find(open_output_ports, pair.first) ==
               open_output_ports.end();
    });

    throw_if_cancelled(run_context);
    return outputs;
}

} // namespace vc::pipe
