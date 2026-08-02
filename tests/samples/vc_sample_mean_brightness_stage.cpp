// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "samples/vc_sample_mean_brightness_stage.h"

#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_packet.h"

namespace vc::pipe {

vc_sample_mean_brightness_stage::vc_sample_mean_brightness_stage(
    stage_name name)
    : i_pipe(std::move(name)) {
}

const char* vc_sample_mean_brightness_stage::kind() const {
    return "sample_mean_brightness";
}

std::size_t vc_sample_mean_brightness_stage::params_hash() const {
    return 0; // paramless — nothing to vary
}

void vc_sample_mean_brightness_stage::declare(
    vc_pipe_contract& contract) const {
    contract.add_input_slot(slots::image);
    contract.add_output_slot(slots::mean); // non-image (double) output
}

void vc_sample_mean_brightness_stage::validate_inputs(
    const vc_pipe_context& context) const {
    // Nothing to check about SIZE: a vc_image can never have zero pixels
    // (vc_image_writer::validated() already rejects a zero
    // width/height/channels), so do_process()'s divide-by-size is safe.
    //
    // dtype IS worth checking: slot<T> pins the payload as a vc_image but
    // not which dtype it holds, and do_process() reads as<buf_f32>() — which
    // would otherwise throw from deep inside vc_pixel_buffer with a message
    // naming neither this stage nor the offending slot.
    const auto& image = context.get_input(slots::image);
    if (image.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc_sample_mean_brightness_stage: input must be f32 (this sample "
            "kernel does not handle u8/u16)");
    }
}

void vc_sample_mean_brightness_stage::do_process(
    vc_pipe_context& context) const {
    const auto& image = context.get_input(slots::image);
    const auto src = image.pixels()->as<vc::buf_f32>();

    double sum = 0.0;
    for (const float value : src) {
        sum += static_cast<double>(value);
    }
    const double mean_value = sum / static_cast<double>(src.size());

    context.set_output(slots::mean, vc_pipe_packet{mean_value});
}

} // namespace vc::pipe
