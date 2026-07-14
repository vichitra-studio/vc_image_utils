// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <unordered_map>
#include <utility>

#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::pipe {

// The per-run data carrier handed to a pipe's process(). It holds the input
// packets the pipe reads (keyed by input-slot name) and collects the output
// packets it publishes (keyed by output-slot name). Input and output names are
// separate maps, so a stage may reuse a name across the two directions.
//
// The API is the 2x2 of {get,set} x {input,output}, split by audience
// (docs/pipe_design.md Sec 12.2):
//   author-facing  — get_input(slot<T>) / set_output(slot<T>): a stage reads
//                    its inputs and writes its outputs through its own typed
//                    slot descriptors, so T is deduced and never restated.
//   runner-facing  — set_input / get_output, keyed by plain slot_name: the
//                    pipeline injects a stage's inputs and harvests its outputs
//                    when it only has the lowered name.
// The typed overloads are thin sugar over the string-keyed maps.
//
// Written in full so stages have a stable API to build against; the reps are
// the stage process() bodies and the pipeline runner that drives contexts.
class vc_pipe_context {
  public:
    // ---- input side ----

    // Author-facing typed read: `ctx.get_input(slots::rgb)` yields a
    // `const vc_image&`. The type comes from the descriptor, so it is never
    // spelled at the call site. Throws vc::vc_exception if the slot is unbound
    // or holds a different type.
    template <typename T> const T& get_input(slot<T> s) const {
        return get_input(slot_name{s.name}).template get<T>();
    }

    // String-keyed typed read — used where a name is in hand rather than a
    // descriptor. Throws if the slot is unbound or holds a different type.
    template <typename T> const T& get_input(const slot_name& name) const {
        return get_input(name).template get<T>();
    }

    // Raw packet read. Throws vc::vc_exception if the slot is unbound.
    const vc_pipe_packet& get_input(const slot_name& name) const;

    // Runner-facing: inject a packet onto an input slot before process() runs.
    void set_input(const slot_name& name, vc_pipe_packet packet);

    // ---- output side ----

    // Author-facing typed publish: `ctx.set_output(slots::grey,
    // vc_pipe_packet{result})`. The descriptor names the slot; the packet
    // carries the value.
    template <typename T> void set_output(slot<T> s, vc_pipe_packet packet) {
        set_output(slot_name{s.name}, std::move(packet));
    }

    // String-keyed publish on a named slot.
    void set_output(const slot_name& name, vc_pipe_packet packet);

    // Runner-facing: harvest a published output. Throws if none was produced.
    const vc_pipe_packet& get_output(const slot_name& name) const;
    bool has_output(const slot_name& name) const noexcept;

  private:
    std::unordered_map<slot_name, vc_pipe_packet> inputs_;
    std::unordered_map<slot_name, vc_pipe_packet> outputs_;
};

} // namespace vc::pipe
