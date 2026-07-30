// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "samples/vc_sample_blur_stage.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"
#include "vc/vc_image_writer.h"
#include "vc/vc_pixel_buffer.h"
#include "vc/vc_types.h"

namespace vc::pipe {

vc_sample_blur_stage::vc_sample_blur_stage(stage_name name,
                                           vc_sample_blur_params params)
    : i_pipe(std::move(name)), params_(params) {
    // radius is known in full at construction — there is no run-time input it
    // could depend on — so it is checked HERE, not deferred to
    // validate_inputs()/process() time: an invalid blur should never be
    // constructible, let alone added to a pipeline and pass validate().
    //
    // Written as NOT(positive), not `radius <= 0.0`: every relational
    // comparison against NaN is false, so `radius <= 0.0` would silently
    // accept a NaN radius (`NaN > 0.0` is also false, so its negation
    // correctly throws instead of falling through into the kernel).
    if (!(params_.radius > 0.0)) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "vc_sample_blur_stage: radius must be positive");
    }
}

const char* vc_sample_blur_stage::kind() const {
    return "sample_blur";
}

std::size_t vc_sample_blur_stage::params_hash() const {
    const std::size_t h1 = std::hash<double>{}(params_.radius);
    const std::size_t h2 = std::hash<bool>{}(params_.normalize);
    // Boost-style combine — same formula as stage_port::hash() (vc_pipe_types.h).
    return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
}

void vc_sample_blur_stage::declare(vc_pipe_contract& contract) const {
    // One image in, one image out. The blur reads a neighbourhood but that
    // is a process() concern, not a contract one.
    contract.add_input_slot(slots::in);
    contract.add_output_slot(slots::out);
}

void vc_sample_blur_stage::validate_inputs(
    const vc_pipe_context& context) const {
    // dtype is an invariant the type contract cannot carry: slot<T> pins the
    // payload as a vc_image but not which dtype it holds, and do_process()
    // reads as<buf_f32>() — which would otherwise throw from deep inside
    // vc_pixel_buffer with a message naming neither this stage nor the slot.
    const auto& image = context.get_input(slots::in);
    if (image.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc_sample_blur_stage: input must be f32 (this sample kernel "
            "does not handle u8/u16)");
    }
}

void vc_sample_blur_stage::do_process(vc_pipe_context& context) const {
    const auto& src_image = context.get_input(slots::in);
    const auto src = src_image.pixels()->as<vc::buf_f32>();

    const vc::image_dim width = src_image.width();
    const vc::image_dim height = src_image.height();
    const vc::channel_count channels = src_image.channels();
    const auto& meta = src_image.meta();

    // Square neighbourhood, radius rounded to the nearest whole pixel, then
    // CLAMPED to the image extent. The clamp is load-bearing, not cosmetic:
    // `y + radius` below is image_dim (uint32) arithmetic, so a large radius
    // wraps past UINT32_MAX and std::min then picks the small wrapped value,
    // leaving y1 BELOW y — the pixel is averaged over a truncated band that
    // need not even contain itself. Measured on a 1x16 ramp with
    // radius = 4294967290: unclamped gives 0.02 (rows 0-4 only) where the
    // correct whole-image mean is 0.075. It is a silently WRONG value, not a
    // crash — y0 pins to 0 whenever radius is large, so y1 >= y0 always holds
    // and the count can never reach zero. (An earlier note here claimed this
    // path divided by zero and produced NaN; that was wrong — verified by
    // running both variants.)
    //
    // Clamping to the extent is also the semantically right answer: a
    // neighbourhood wider than the image already covers the whole image.
    //
    // NOTE: a radius in (0, 0.5) rounds to 0 here and the stage becomes an
    // identity copy. That is accepted, not guarded — validate_inputs() only
    // requires radius > 0, so a sub-half-pixel blur is a legal no-op.
    const auto max_extent = static_cast<long long>(std::max(width, height));
    const auto radius = static_cast<vc::image_dim>(
        std::min(std::llround(params_.radius), max_extent));

    vc_image_writer out{width, height, channels, vc::buf_f32{0.0f}};
    for (vc::image_dim y = 0; y < height; ++y) {
        const vc::image_dim y0 = (y > radius) ? y - radius : 0;
        const vc::image_dim y1 = std::min(height - 1, y + radius);
        for (vc::image_dim x = 0; x < width; ++x) {
            const vc::image_dim x0 = (x > radius) ? x - radius : 0;
            const vc::image_dim x1 = std::min(width - 1, x + radius);
            for (vc::channel_count ch = 0; ch < channels; ++ch) {
                double sum = 0.0;
                std::size_t count = 0;
                for (vc::image_dim ny = y0; ny <= y1; ++ny) {
                    for (vc::image_dim nx = x0; nx <= x1; ++nx) {
                        sum += static_cast<double>(src[meta.index(nx, ny, ch)]);
                        ++count;
                    }
                }
                const double value =
                    params_.normalize ? sum / static_cast<double>(count) : sum;
                out.at<vc::buf_f32>(x, y, ch) = static_cast<float>(value);
            }
        }
    }

    context.set_output(slots::out, vc_pipe_packet{std::move(out).seal()});
}

} // namespace vc::pipe
