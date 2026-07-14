// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/stages/vc_passthrough_stage.h"

#include <utility>

#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/vc_image.h"

namespace vc::pipe {

// WORKED REFERENCE — the shape every other stage follows.

vc_passthrough_stage::vc_passthrough_stage(stage_name name)
    : i_pipe(std::move(name)) {
}

const char* vc_passthrough_stage::kind() const {
    return "passthrough";
}

void vc_passthrough_stage::declare(vc_pipe_contract& contract) const {
    // One image in, one image out. The typed slot descriptors carry BOTH the
    // slot name and its payload type — add_*_slot needs nothing else.
    contract.add_input_slot(slots::in);
    contract.add_output_slot(slots::out);
}

void vc_passthrough_stage::process(vc_pipe_context& context) const {
    // Read the input as a vc_image (get_input() deduces the type from the
    // descriptor; throws if the packet is not one), then republish it unchanged
    // on the output slot. The copy is shallow — vc_image holds a shared_ptr to
    // its buffer — which is exactly right for an identity stage.
    const auto& image = context.get_input(slots::in);
    context.set_output(slots::out, vc_pipe_packet{image});
}

} // namespace vc::pipe
