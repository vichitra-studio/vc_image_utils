// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_conv_frequency.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/math/vc_dft.h"
#include "vc/math/vc_grid2d.h"
#include "vc/pixelops/vc_convolve.h"
#include "vc/utils/vc_strings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace vc::pixelops {

namespace {

vc::math::grid_dim padded_axis(vc::image_dim extent,
                               vc::image_dim taps,
                               const char* axis) {
    const std::uint64_t sum =
        std::uint64_t{extent} + std::uint64_t{taps} - 1U;

    if (sum > std::numeric_limits<vc::math::grid_dim>::max()) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            std::string("frequency_extent: padded ") + axis + " extent " +
                std::to_string(extent) + " + " + std::to_string(taps) +
                " - 1 does not fit in a grid dimension");
    }
    return static_cast<vc::math::grid_dim>(sum);
}

// PROMOTION CANDIDATES, deliberately left local. max_imag() already exists as a
// test helper in 06_dft.cpp and is wanted here in production, which is the
// strongest case of the four; embed_channel()'s zero-pad and multiply_into()'s
// elementwise product are both generic over complex planes and would belong in
// vc::math. Promoting them needs its own tests.

// The kernel laid into a plane of `extent`, CENTRED AND WRAPPED: tap (kx, ky)
// goes to (kx mod Lx, ky mod Ly), so the centre tap sits at (0,0) and negative
// offsets land at the far end of each axis.
//
// A 3x3 kernel on a 4x4 plane therefore occupies the FOUR CORNERS, not a 3x3
// block at the top-left. That is the shape to eyeball if this is ever
// suspected: a block at the top-left is the causal placement, which yields a
// correct-looking image shifted by (radius_x, radius_y) and is invisible to any
// magnitude check, because a shift multiplies each bin by something of radius 1.
//
// Returns a complex plane and NOT a vc_kernel: vc_kernel's constructor requires
// odd, non-zero dimensions, because radius_x() only exists if there is a centre
// tap. A padded plane is W+kw-1 wide, which is EVEN whenever the image is. A
// plane is not a kernel; it is a buffer.
vc::math::vc_complex_signal kernel_plane(const vc_kernel& kernel,
                                         const vc::math::grid2d& extent) {
    // extent.count() and NOT width * height: both dimensions are uint32_t, so
    // their product would be formed in 32-bit unsigned and wrap.
    vc::math::vc_complex_signal plane(extent.count(),
                                      vc::math::vc_complex{0.0F, 0.0F});

    // Signed throughout, then cast once the value is provably in range. kx is
    // kernel_offset (int64_t) and the extent is uint32_t; mixing them
    // unconverted is a sign-conversion warning at best.
    const auto lx = static_cast<kernel_offset>(extent.width());
    const auto ly = static_cast<kernel_offset>(extent.height());

    for (kernel_offset ky = -kernel.radius_y(); ky <= kernel.radius_y(); ++ky) {
        for (kernel_offset kx = -kernel.radius_x(); kx <= kernel.radius_x();
             ++kx) {
            // The double modulo is not redundant: C++ % returns a NEGATIVE
            // remainder for negative operands, so (-1 % 4) is -1, not 3. Same
            // reason vc_edge_policy.h spells it out for wrap.
            const kernel_offset wx = ((kx % lx) + lx) % lx;
            const kernel_offset wy = ((ky % ly) + ly) % ly;

            // ACCUMULATE, do not assign. When the kernel is WIDER than the
            // extent -- legal under wrap, where the extent is the source size
            // -- two or more taps land in the same slot and their weights must
            // SUM. A 5-tap kernel on a 4-wide ring maps offset -2 and offset
            // +2 both to slot 2; assigning lets the later write win and
            // silently drops the other tap. Measured on {1,2,4,8,16} over a
            // 4x1 impulse: slot 2 read 16 where convolve(wrap) gives 17.
            const auto ix = static_cast<std::size_t>(wx);
            const auto iy = static_cast<std::size_t>(wy);
            plane[(iy * extent.width()) + ix] +=
                vc::math::vc_complex{kernel.at(kx, ky), 0.0F};
        }
    }
    return plane;
}

// One channel of `src`, embedded TOP-LEFT in a plane of `extent`, zeros
// elsewhere.
//
// Building at the SOURCE size and transforming at the PADDED extent is an
// out-of-bounds read inside separable_passes, not a wrong answer -- dft2d now
// refuses that outright, but allocating at the extent is what makes it correct.
//
// Reads through at<buf_f32>(x, y, ch) rather than indexing the raw span.
// vc_image_info::index() is (y*width + x)*channels + ch -- INTERLEAVED, not
// planar. The two forms AGREE for a single channel, so a planar index passes
// every one-channel test and silently scrambles a three-channel image.
vc::math::vc_complex_signal embed_channel(const vc::vc_image& src,
                                          vc::channel_count ch,
                                          const vc::math::grid2d& extent) {
    if (src.width() > extent.width() || src.height() > extent.height()) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "embed_channel: source does not fit inside the extent");
    }

    vc::math::vc_complex_signal plane(extent.count(),
                                      vc::math::vc_complex{0.0F, 0.0F});
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            plane[(static_cast<std::size_t>(y) * extent.width()) + x] =
                vc::math::vc_complex{src.at<vc::buf_f32>(x, y, ch), 0.0F};
        }
    }
    return plane;
}

// Elementwise product, in place into `spectrum`.
void multiply_into(vc::math::vc_complex_signal& spectrum,
                  vc::math::vc_complex_view by) {
    if (spectrum.size() != by.size()) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "multiply_into: spectra differ in length");
    }
    for (std::size_t i = 0; i < spectrum.size(); ++i) {
        spectrum[i] *= by[i];
    }
}

// Largest |imag| over a plane. Same quantity 06_dft's check 8b measures.
float max_imag(vc::math::vc_complex_view plane) {
    float worst = 0.0F;
    for (const vc::math::vc_complex& z : plane) {
        worst = std::max(worst, std::abs(z.imag()));
    }
    return worst;
}

// RMS of the real parts -- the scale the imaginary residue is judged against.
float real_rms(vc::math::vc_complex_view plane) {
    if (plane.empty()) {
        return 0.0F;
    }
    double acc = 0.0;
    for (const vc::math::vc_complex& z : plane) {
        acc += static_cast<double>(z.real()) * static_cast<double>(z.real());
    }
    return static_cast<float>(
        std::sqrt(acc / static_cast<double>(plane.size())));
}

vc::math::grid2d extent_of(vc::math::grid_dim w, vc::math::grid_dim h) {
    return vc::math::grid2d::checked(
        static_cast<vc::element_count>(w) * static_cast<vc::element_count>(h),
        w, h, "frequency_extent");
}

} // namespace

vc::math::grid2d frequency_extent(const vc::vc_image& src,
                                  const vc_kernel& kernel,
                                  vc_edge_policy policy) {
    switch (policy) {
    case vc_edge_policy::wrap:
        return extent_of(src.width(), src.height());

    case vc_edge_policy::zero:
        return extent_of(padded_axis(src.width(), kernel.width(), "x"),
                         padded_axis(src.height(), kernel.height(), "y"));

    case vc_edge_policy::clamp:
    case vc_edge_policy::reflect:
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "frequency_extent: the DFT can express only wrap and zero; "
            "clamp and reflect have no frequency-domain equivalent");
    }

    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "frequency_extent: unknown edge policy");
}

// The residue is judged RELATIVE to the result's own scale, per section F.3.
// An absolute bound -- the first draft used 1e-5 -- passes on an image whose
// values are 0..1 and false-positives on the same image scaled to 0..255, for
// entirely correct code.
//
// 1e-4 is MEASURED, not estimated. Instrumenting this function over every
// 11_conv_theorem case plus 64/128/256 squared with a sigma-1.4 Gaussian:
//
//     worst observed ratio   1.86e-08    (32x24, zero-padded)
//     128x128 wrap / zero    3.76e-09 / 8.17e-09
//     256x256 wrap / zero    2.14e-09 / 6.51e-11
//
// so ~5400x of headroom at the worst case, and the ratio does NOT grow with
// size -- 64, 128 and 256 squared all land in the same 1e-9..1e-8 band, which
// is dft1d's double accumulator holding. On the other side, a real asymmetry
// leaves an O(0.1..1) residue; 06_dft check 8b measures exactly 0.5 against 0.0
// for a single broken twin. Four orders of margin either way.
constexpr float k_imag_rel_tol = 1e-4F;

// Floor on the denominator so an all-black result does not divide by zero. A
// genuinely black plane has an exactly-zero imaginary part, so it passes.
constexpr float k_rms_floor = 1e-12F;

vc::vc_image convolve_frequency(const vc::vc_image& src,
                                const vc_kernel& kernel,
                                vc_edge_policy policy) {
    if (src.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "convolve_frequency: src must be f32");
    }

    // Throws for clamp and reflect before any work is done.
    const vc::math::grid2d extent = frequency_extent(src, kernel, policy);

    // OUTPUT GEOMETRY FOLLOWS THE SOURCE, not the extent. The padded plane is
    // scratch space; a neighbourhood operation filters a picture, it does not
    // resize it -- same contract as convolve().
    vc_image_writer out{src.width(), src.height(), src.channels(),
                        vc::buf_f32{0.0F}};

    // Transformed ONCE, outside the channel loop. The kernel does not vary per
    // channel, and at 128^2 a dft2d is ~8M operations, so hoisting it is the
    // difference between three transforms and four for an RGB image.
    const vc::math::vc_complex_signal kernel_freq =
        vc::math::dft2d(kernel_plane(kernel, extent), extent);

    for (vc::channel_count ch = 0; ch < src.channels(); ++ch) {
        vc::math::vc_complex_signal spectrum =
            vc::math::dft2d(embed_channel(src, ch, extent), extent);

        multiply_into(spectrum, kernel_freq);

        const vc::math::vc_complex_signal result =
            vc::math::idft2d(spectrum, extent);

        // A real image convolved with a real kernel is real: both spectra are
        // twin-symmetric, so their product is, so the inverse has no imaginary
        // part beyond float noise. Checking it before discarding it is free,
        // and calling .real() blind would throw away the only evidence that
        // something upstream was not twin-symmetric -- which is how a
        // frequency-domain filter ships quietly wrong.
        const float scale = std::max(real_rms(result), k_rms_floor);
        const float residue = max_imag(result);
        if (residue > k_imag_rel_tol * scale) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                std::string("convolve_frequency: inverse transform is not "
                            "real -- max|imag| ") +
                    std::to_string(residue) + " against RMS " +
                    std::to_string(scale) +
                    ", which means the spectra were not twin-symmetric");
        }

        // CROP: the top-left source-sized window, with NO offset, because
        // kernel_plane() centres the kernel. A causal placement would need the
        // crop to start at (radius_x, radius_y) instead -- see its comment.
        for (vc::image_dim y = 0; y < src.height(); ++y) {
            for (vc::image_dim x = 0; x < src.width(); ++x) {
                out.at<vc::buf_f32>(x, y, ch) = static_cast<vc::buf_f32>(
                    result[(static_cast<std::size_t>(y) * extent.width()) + x]
                        .real());
            }
        }
    }

    return std::move(out).seal();
}

} // namespace vc::pixelops
