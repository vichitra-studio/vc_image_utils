// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/math/vc_dft.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_scalar.h"

#include <complex>
#include <cstddef>
#include <numbers>
#include <span>

namespace vc::math {

complex_signal to_signal(real_view real_samples) {
    complex_signal out;
    out.reserve(real_samples.size());
    for (const vc::float32 v : real_samples) {
        out.emplace_back(v, 0.0F);
    }
    return out;
}

complex_signal dft1d(complex_view x) {
    // Empty in, empty out -- the header's contract, and the reason the
    // division below can never be by zero. Not an error: the transform of an
    // empty sequence IS the empty sequence, so there is nothing to report.
    if (x.empty()) {
        return {};
    }
    const double angle_scale =
        -2.0 * std::numbers::pi / static_cast<double>(x.size());
    complex_signal out(x.size());
    for (std::size_t k = 0; k < x.size(); ++k) {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t n = 0; n < x.size(); ++n) {
            double angle = angle_scale * static_cast<double>(k * n);
            std::complex<double> exp_term{std::cos(angle), std::sin(angle)};
            sum += static_cast<std::complex<double>>(x[n]) * exp_term;
        }
        out[k] = vc_complex{static_cast<vc::float32>(sum.real()),
                            static_cast<vc::float32>(sum.imag())};
    }
    return out;
}

complex_signal dft1d(real_view x) {
    const complex_signal lifted = to_signal(x);
    return dft1d(lifted);
}

complex_signal idft1d(complex_view spectrum) {
    if (spectrum.empty()) {
        return {};
    }
    const double angle_scale =
        2.0 * std::numbers::pi / static_cast<double>(spectrum.size());
    const double scale_factor = 1.0 / static_cast<double>(spectrum.size());
    complex_signal out(spectrum.size());
    for (std::size_t n = 0; n < spectrum.size(); ++n) {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t k = 0; k < spectrum.size(); ++k) {
            double angle = angle_scale * static_cast<double>(k * n);
            std::complex<double> exp_term{std::cos(angle), std::sin(angle)};
            sum += static_cast<std::complex<double>>(spectrum[k]) * exp_term;
        }
        sum *= scale_factor;
        out[n] = vc_complex{static_cast<vc::float32>(sum.real()),
                            static_cast<vc::float32>(sum.imag())};
    }
    return out;
}

// ---- 2-D -----------------------------------------------------------------
//
// Two passes of the 1-D transform: every row, then every column of that
// result. The derivation, the pass order, the scaling rule and the cost of
// separability are all in vc_dft.h above the declarations.
//
// NO ARGUMENT VALIDATION HERE, and that is not an omission. grid2d cannot be
// constructed from dimensions that disagree with the buffer, so by the time
// `extent` exists the only two things these functions could have checked are
// already true. The checks live in grid2d::checked() -- one place, tested
// once, shared with the Poisson solvers when they arrive.
//
// The column pass gathers into a contiguous temporary and scatters back.
// Columns are strided, which looks like it should hurt: the stride is `width`
// elements, so at 8 bytes per complex sample every column element touches a
// fresh 64-byte cache line and 8 of every 64 bytes get used. Measured, that
// costs 0.08-0.18% of the pass, because a column is gathered ONCE and then
// has N operations done on it -- the strided read is amortised over the
// arithmetic. A blocked transpose is about 3.7x faster at the memory step
// (0.05 ms against 0.19 ms at 256x256) and remains the lever if this ever
// matters; it does not matter at O(N^2).

namespace {

// The 1-D transform a separable pass applies. dft1d and idft1d both match it,
// and that single difference is the ONLY thing that distinguished the two 2-D
// bodies before this helper existed -- forty lines duplicated to vary one
// function call.
//
// A PLAIN FUNCTION POINTER, not a template parameter. The helper calls `pass`
// width + height times in total, and each of those calls does O(N^2) work
// inside, so the indirect call is unmeasurable; a template would buy inlining
// that nothing needs and cost a template to read. If this ever became the hot
// path -- it will not; FFTW takes over at P4 -- the measurement in
// vc_dft.h's cost note is where to start.
//
// And NOT std::forward. Perfect forwarding preserves the value category of
// something you pass along: it earns its keep when the callable owns state, is
// expensive to copy, or must be moved from. These two are stateless free
// functions. std::forward<T>(pass) would compile here and do precisely
// nothing, which is worse than not writing it -- a reader would look for the
// ownership question it implies and find none.
using pass1d = complex_signal (*)(complex_view);

// Row pass, then column pass. The whole 2-D transform.
//
// The derivation, the pass order and the scaling rule are in vc_dft.h above
// the declarations. Nothing is validated here: grid2d cannot hold dimensions
// that disagree with the buffer, so by the time `extent` exists there is
// nothing left to check.
complex_signal separable_passes(complex_view in, grid2d extent, pass1d pass) {
    const std::size_t width = extent.width();
    const std::size_t height = extent.height();

    complex_signal out;
    out.reserve(in.size()); // one allocation, not one per row

    // PASS 1 -- rows. A row is a contiguous slice, so the view over it costs
    // nothing; this is the cheap pass and it goes first for that reason.
    for (std::size_t y = 0; y < height; ++y) {
        const std::size_t row_start = y * width;
        const complex_view row_view(in.data() + row_start, width);
        const complex_signal transformed_row = pass(row_view);
        out.insert(out.end(), transformed_row.begin(), transformed_row.end());
    }

    // PASS 2 -- columns, over pass 1's RESULT rather than the input. Columns
    // are strided, so they must be gathered into a contiguous temporary and
    // scattered back. Measured, that stride costs 0.08-0.18% of the pass: a
    // column is gathered once and then has N operations done on it, so the
    // cost is amortised over the arithmetic. See vc_dft.h.
    for (std::size_t kx = 0; kx < width; ++kx) {
        complex_signal column(height);
        for (std::size_t ky = 0; ky < height; ++ky) {
            column[ky] = out[(ky * width) + kx];
        }
        const complex_signal transformed_column = pass(column);
        for (std::size_t ky = 0; ky < height; ++ky) {
            out[(ky * width) + kx] = transformed_column[ky];
        }
    }

    return out;
}

} // namespace

// Both entry points are now the same two passes with a different 1-D
// transform. Passing `dft1d` bare is unambiguous despite the real_view
// overload: the parameter's type is pass1d, and overload resolution against a
// specific function-pointer target picks the matching one.
complex_signal dft2d(complex_view plane, grid2d extent) {
    return separable_passes(plane, extent, dft1d);
}

complex_signal idft2d(complex_view spectrum, grid2d extent) {
    return separable_passes(spectrum, extent, idft1d);
}

complex_signal fftshift2d(complex_view plane, grid2d extent) {
    // TODO(you): out[(x + W/2) % W, (y + H/2) % H] = in[x, y], integer
    // division. The header pins the convention and 08_spectrum_viz asserts the
    // exact index mapping on a 4x2 and a 5x3 grid.
    //
    // extent cannot disagree with plane.size() -- that is what grid2d is for --
    // so there is nothing to validate here.
    (void)plane;
    (void)extent;
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "fftshift2d: not implemented yet");
}

} // namespace vc::math
