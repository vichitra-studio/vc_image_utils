// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/math/vc_dft.h"
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

} // namespace vc::math
