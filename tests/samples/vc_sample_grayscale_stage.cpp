// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "samples/vc_sample_grayscale_stage.h"

#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_packet.h"

namespace vc::pipe {

vc_sample_grayscale_stage::vc_sample_grayscale_stage(stage_name name)
    : i_pipe(std::move(name)) {
}

const char* vc_sample_grayscale_stage::kind() const {
    return "sample_grayscale";
}

std::size_t vc_sample_grayscale_stage::params_hash() const {
    return 0; // paramless — nothing to vary
}

void vc_sample_grayscale_stage::declare(vc_pipe_contract& contract) const {
    contract.add_input_slot(slots::rgb);
    contract.add_output_slot(slots::grey);
}

void vc_sample_grayscale_stage::validate_inputs(
    const vc_pipe_context& context) const {
    const auto& image = context.get_input(slots::rgb);

    // Luminance reads channel 0/1/2 unconditionally, so "at least 3 channels"
    // is a domain invariant declare()'s type contract cannot express — it
    // would let a single-channel image through to read out of bounds.
    if (image.channels() < 3) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc_sample_grayscale_stage: input needs at least 3 channels "
            "(R, G, B)");
    }

    // dtype is the OTHER invariant the type contract cannot carry: slot<T>
    // pins the payload as a vc_image, but not which dtype that image holds.
    // do_process() reads as<buf_f32>(), which would otherwise throw from deep
    // inside vc_pixel_buffer with a message that names neither this stage nor
    // the offending slot. Stated here so the failure is attributable.
    if (image.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc_sample_grayscale_stage: input must be f32 (this sample "
            "kernel does not handle u8/u16)");
    }
}

void vc_sample_grayscale_stage::do_process(vc_pipe_context& context) const {
    const auto& image = context.get_input(slots::rgb);
    const auto src = image.pixels()->as<vc::buf_f32>();

    vc_image_writer out{image.width(), image.height(), 1, vc::buf_f32{0.0f}};
    for (vc::image_dim y = 0; y < image.height(); ++y) {
        for (vc::image_dim x = 0; x < image.width(); ++x) {
            const float r = src[image.meta().index(x, y, 0)];
            const float g = src[image.meta().index(x, y, 1)];
            const float b = src[image.meta().index(x, y, 2)];
            out.at<vc::buf_f32>(x, y, 0) = 0.299f * r + 0.587f * g + 0.114f * b;
        }
    }

    context.set_output(slots::grey, vc_pipe_packet{std::move(out).seal()});
}

} // namespace vc::pipe
