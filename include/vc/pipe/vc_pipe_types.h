// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vc/pipe/vc_pipe_packet.h"

namespace vc::pipe {

class vc_pipe_contract;
struct stage_port;

// A slot and a stage are both identified at the framework boundary by a stable
// text name — the form a CLI or config file can produce (docs/pipe_design.md
// Sec 4.2, Sec 12.1). Plain std::string: no interning yet (that is an Ongoing
// item, added when a profile demands it). A numeric slot index is reserved for
// later, when a stage maps its own scoped enum onto these indices; it is unused
// by the linear runner.
using slot_name = std::string;
using stage_name = std::string;
using slot_index = std::uint32_t;

// The pipeline-level, graph-wide map keyed by stage_port: vc_pipeline::run()'s
// open-inputs argument and open-outputs return value. Distinct from a single
// stage's own input/output maps (vc_pipe_context), which are keyed by bare
// slot_name because they only ever concern one stage at a time — a
// stage_port's extra stage field only matters once packets are being handed
// across the whole graph, which is exactly what run() does.
using render_io_map = std::unordered_map<stage_port, vc_pipe_packet>;

using stage_contract_map = std::unordered_map<stage_name, vc_pipe_contract>;

// A plain list of graph coordinates — vc_pipeline's open-input/open-output
// port lists (find_open_inputs()/find_open_outputs(), vc_pipeline.cpp), used
// enough times there to be worth a name of its own.
using stage_port_list = std::vector<stage_port>;

// A slot belongs to one direction. Input and output are SEPARATE name
// namespaces (Sec 4.2): the same name may appear once as an input and once as
// an output without colliding. (Which vector of a contract holds a slot already
// encodes its direction; this enum names the concept for introspection.)
enum class slot_direction : std::uint8_t {
    input,
    output,
};

// The one AUTHORING representation of a slot: a name bonded to its payload
// type (docs/pipe_design.md Sec 12.1). A stage publishes these as static
// constexpr members (see any stage's `struct slots`); T is a phantom parameter
// — `slot` stores only the name, so `slot<vc_image>` needs vc_image merely
// declared, not complete.
//
// This is compile-time-only sugar that lives strictly at the authoring edge:
// the instant a slot<T> crosses into the framework (add_input_slot, connect,
// ctx.get_input, a stage_port ctor) it is LOWERED — the name kept as a string,
// the type erased to std::type_index. It never propagates or is stored.
template <typename T> struct slot {
    std::string_view name;
};

// A graph coordinate: which slot on which stage. THE single wiring currency —
// connect() takes a pair of these, and the map-variant run() keys its
// input/output maps by them. Built from a stage name plus either a typed slot
// descriptor (which lowers to the slot's name right here) or an already-lowered
// slot name; never hand-typed as two raw string literals in graph code. Named
// for its two fields: a port ON a stage (contrast a "node", which is a stage).
struct stage_port {
    stage_name stage;
    slot_name slot;

    stage_port() = default;

    // From a typed descriptor:
    // `stage_port{"a", vc_passthrough_stage::slots::in}`. Lowers slot<T> to
    // its name at the boundary, keeping the T only long enough to be typo-safe
    // at the call site.
    template <typename T>
    stage_port(stage_name stage_id, ::vc::pipe::slot<T> descriptor)
        : stage(std::move(stage_id)), slot(descriptor.name) {
    }

    // From an already-lowered name — the runner assembling a port out of a
    // contract's enumerated slot names.
    stage_port(stage_name stage_id, slot_name slot_id)
        : stage(std::move(stage_id)), slot(std::move(slot_id)) {
    }

    bool operator==(const stage_port& other) const noexcept {
        return stage == other.stage && slot == other.slot;
    }

    // Combined hash of the two name fields, so a stage_port is usable as a map
    // key (std::hash<stage_port> below just forwards here — a std::hash
    // specialization is required for unordered_map and cannot be a member, but
    // the logic lives on the type it belongs to).
    std::size_t hash() const noexcept {
        const std::size_t h1 = std::hash<stage_name>{}(stage);
        const std::size_t h2 = std::hash<slot_name>{}(slot);
        // Boost-style combine — good enough for a small wiring map.
        return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
    }
};

} // namespace vc::pipe

// Lets a stage_port be an unordered_map key (run()'s input/output maps).
template <> struct std::hash<vc::pipe::stage_port> {
    std::size_t operator()(const vc::pipe::stage_port& p) const noexcept {
        return p.hash();
    }
};