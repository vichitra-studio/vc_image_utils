// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/stages/vc_blur_stage.h"

#include <utility>

#include "vc/edit/vc_edit_session.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_contract_builder.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"

namespace vc::pipe {

vc_blur_stage::vc_blur_stage(stage_name name, vc_blur_params params)
    : i_pipe(std::move(name)), params_(params) {
}

const char* vc_blur_stage::kind() const {
    return "blur";
}

void vc_blur_stage::declare(contract_builder& contract) const {
    // Same contract shape as passthrough: one image in, one image out. The blur
    // reads a neighbourhood but that is a process() concern, not a contract one.
    contract.add_input_slot(slots::in);
    contract.add_output_slot(slots::out);
}

void vc_blur_stage::process(vc_pipe_context& context) const {
    // TODO(you): the blur kernel — YOUR rep to write and test. A sketch of the
    // shape (delete this and implement):
    //
    //   const auto& src = context.get_input(slots::in);          // read input
    //   vc_image_writer dst{src.width(), src.height(),
    //                       src.channels(), vc::buf_f32{0.0f}};
    //   // ... for each pixel, average a params_.radius neighbourhood (box or
    //   //     gaussian); if params_.normalize, divide by the kernel weight ...
    //   context.set_output(slots::out, vc_pipe_packet{std::move(dst).seal()});
    //
    // Until then this throws, so the blur spec in test_vc_param_schema.cpp is
    // RED-by-design (and red for exactly THIS one reason — the test drives
    // process() directly through a context, not through run()).
    (void)context;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_blur_stage::process: blur kernel not yet "
                           "implemented (TODO(you))");
}

vc_blur_params vc_blur_stage::from_session(const vc::edit::vc_edit_session& session) {
    // TODO(you): read the relevant slice of session.edits() (or wherever the
    // blur's user-facing knob(s) end up living in vc_edit_document) and
    // return the resolved vc_blur_params. This is the toy-algorithm analogue
    // of the exposure/capture slices already on vc_edit_document.
    (void)session;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_blur_stage::from_session: derive blur params from the "
                           "session (TODO(you))");
}

} // namespace vc::pipe
