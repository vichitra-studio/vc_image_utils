// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/stages/vc_grayscale_stage.h"

#include <utility>

// TODO(you): you will need these to implement declare()/process():
//   #include "vc/pipe/vc_pipe_contract.h"
//   #include "vc/pipe/vc_pipe_context.h"
//   #include "vc/pipe/vc_pipe_packet.h"
//   #include "vc/vc_image.h"
//   #include "vc/vc_pixel_buffer.h"   // to read/write pixel data via as<T>()

namespace vc::pipe {

vc_grayscale_stage::vc_grayscale_stage(stage_name name)
    : i_pipe(std::move(name)) {
}

const char* vc_grayscale_stage::kind() const {
    return "grayscale";
}

void vc_grayscale_stage::declare(vc_pipe_contract& contract) const {
    // TODO(you): follow vc_passthrough_stage. Declare the image input and image
    // output using this stage's typed slots (the tests expect names "rgb" and
    // "grey"):
    //   contract.add_input_slot(slots::rgb);
    //   contract.add_output_slot(slots::grey);
    (void)contract;
}

void vc_grayscale_stage::process(vc_pipe_context& context) const {
    // TODO(you): read the "rgb" input image (ctx.get_input(slots::rgb)), build
    // a new 1-channel vc_image, fill it with a luminance of the RGB channels
    // (e.g. 0.299 R + 0.587 G + 0.114 B over the f32 buffer), and
    // ctx.set_output(slots::grey, vc_pipe_packet{result}).
    // Note: the base vc_image constructor is itself still a learning stub, so a
    // full numeric test waits until that is implemented — declare() alone is
    // enough to exercise the pipeline's type contract.
    (void)context;
}

} // namespace vc::pipe
