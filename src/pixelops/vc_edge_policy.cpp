// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_edge_policy.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace vc::pixelops {
namespace {

// Resolve ONE index on ONE axis. Internal on purpose -- see the header: as a
// public two-scalar function it invited a width/height swap at the call site
// that its own 1-D tests could never catch. Here there is exactly one caller,
// immediately below, which reads both dimensions off the image.
//
// `i` may be negative or >= n. Returns the index actually to be read, or
// nullopt under `zero` when `i` lies outside [0, n). nullopt means "this tap
// contributes nothing", which is a different statement from "read index 0" --
// hence an optional and not a sentinel. A sentinel like -1 invites being cast
// to an unsigned index, and a negative index cast to image_dim is about four
// billion.
std::optional<vc::image_dim>
resolve_index(std::int64_t i, vc::image_dim n, vc_edge_policy policy) {
    if (i >= 0 && i < n)
        return static_cast<vc::image_dim>(i);

    switch (policy) {
    case vc_edge_policy::zero:
        return std::nullopt;
    case vc_edge_policy::clamp:
        return static_cast<vc::image_dim>(
            std::clamp(i, std::int64_t{0}, static_cast<std::int64_t>(n - 1)));
    case vc_edge_policy::reflect: {
        // Reflection about the EDGE PIXEL is periodic with period 2*(n-1):
        // going out and folding back returns to the start after that many
        // steps. A single fold only works while |i| stays inside one period,
        // and outside it produces a NEGATIVE index that casts to about four
        // billion. So fold into one period first, then mirror.
        if (n == 1) {
            return 0; // period would be 0; every index is the one pixel
        }
        const auto span = static_cast<std::int64_t>(n);
        const std::int64_t period = 2 * (span - 1);
        std::int64_t m = i % period; // may be negative -- same as wrap
        if (m < 0) {
            m += period;
        }
        if (m >= span) {
            m = period - m; // upper half of the period mirrors back
        }
        return static_cast<vc::image_dim>(m);
    }
    case vc_edge_policy::wrap:
        return static_cast<vc::image_dim>(((i % n) + n) % n);
    default:
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "resolve_index: unknown edge policy");
    }
}

} // namespace

float fetch(std::span<const vc::buf_f32> px,
            const vc::vc_image& src,
            std::int64_t x,
            std::int64_t y,
            vc::channel_count ch,
            vc_edge_policy policy) {
    const auto sx = resolve_index(x, src.width(), policy);
    const auto sy = resolve_index(y, src.height(), policy);
    if (sx == std::nullopt || sy == std::nullopt)
        return 0.0F;
    return px[src.meta().index(*sx, *sy, ch)];
}

} // namespace vc::pixelops
