// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/stages/vc_mean_brightness_stage.h"

#include <utility>

// TODO(you): you will need these to implement declare()/process():
//   #include "vc/pipe/vc_pipe_contract.h"  // declare() adds slots here
//   #include "vc/pipe/vc_pipe_context.h"
//   #include "vc/pipe/vc_pipe_packet.h"
//   #include "vc/vc_image.h"
//   #include "vc/vc_pixel_buffer.h"

namespace vc::pipe {

vc_mean_brightness_stage::vc_mean_brightness_stage(stage_name name)
    : i_pipe(std::move(name)) {
}

const char* vc_mean_brightness_stage::kind() const {
    return "mean_brightness";
}

std::size_t vc_mean_brightness_stage::params_hash() const {
    return 0; // paramless — nothing to vary
}

void vc_mean_brightness_stage::declare(vc_pipe_contract& contract) const {
    // TODO(you): the key line that makes this stage worth having — the OUTPUT
    // is a scalar, not an image. slots::mean is a slot<double>, so
    // add_output_slot records a `double` payload type. The tests expect input
    // "image" and output "mean":
    //   contract.add_input_slot(slots::image);
    //   contract.add_output_slot(slots::mean);   // <-- non-image payload
    // This is the heterogeneous type contract the plain apply(image)->image
    // interface cannot express (docs/pipe_design.md Sec 2.1).
    (void)contract;
}

void vc_mean_brightness_stage::validate_inputs(
    const vc_pipe_context& context) const {
    // TODO(you): add a domain invariant here if this stage ever needs one
    // beyond what declare()'s type contract already guarantees; leave as a
    // no-op if it doesn't.
    (void)context;
}

void vc_mean_brightness_stage::do_process(vc_pipe_context& context) const {
    // TODO(you): read the "image" input (ctx.get_input(slots::image)), average
    // its f32 buffer, and publish a double: ctx.set_output(slots::mean,
    // vc_pipe_packet{mean}). This is where a non-image packet enters the flow.
    (void)context;
}

} // namespace vc::pipe
