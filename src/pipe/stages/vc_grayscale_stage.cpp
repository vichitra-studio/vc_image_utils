// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/stages/vc_grayscale_stage.h"

#include <utility>

// TODO(you): you will need these to implement declare()/process():
//   #include "vc/pipe/vc_pipe_contract.h"  // declare() adds slots here
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

std::size_t vc_grayscale_stage::params_hash() const {
    return 0; // paramless — nothing to vary
}

void vc_grayscale_stage::declare(vc_pipe_contract& contract) const {
    // TODO(you): follow vc_passthrough_stage. Declare the image input and image
    // output using this stage's typed slots (the tests expect names "rgb" and
    // "grey"):
    //   contract.add_input_slot(slots::rgb);
    //   contract.add_output_slot(slots::grey);
    (void)contract;
}

void vc_grayscale_stage::validate_inputs(const vc_pipe_context& context) const {
    // TODO(you): add a domain invariant here if this stage ever needs one
    // beyond what declare()'s type contract already guarantees (e.g. a
    // non-empty image); leave as a no-op if it doesn't.
    (void)context;
}

void vc_grayscale_stage::do_process(vc_pipe_context& context) const {
    // TODO(you): read the "rgb" input image (ctx.get_input(slots::rgb) -> const
    // vc_image&), then BUILD the output through a vc_image_writer (the only way
    // to write pixels; #include "vc/vc_image_writer.h"):
    //   vc_image_writer out{image.width(), image.height(), 1, vc::buf_f32{0.0f}};
    //   read the rgb input via input.pixels()->as<vc::buf_f32>() and
    //   input.meta().index(x, y, ch); write luminance
    //   (e.g. 0.299 R + 0.587 G + 0.114 B) via out.at<vc::buf_f32>(x, y, 0);
    //   then ctx.set_output(slots::grey,
    //                       vc_pipe_packet{std::move(out).seal()});
    // Note: vc_image_writer::validated() is still a learning stub (no dimension
    // checks yet), but allocation works — so a full numeric test is possible
    // once this process() body is written.
    (void)context;
}

} // namespace vc::pipe
