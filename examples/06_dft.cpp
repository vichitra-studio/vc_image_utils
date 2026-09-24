// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 06 -- the DFT, and the convention that nothing downstream will tell
// you about.
//
// Example 05 ended on a convolution whose correctness hinged on one sign:
// f(i-k) versus f(i+k) decided convolution-or-correlation, failed silently,
// and a symmetric kernel could not expose it. This example is the same lesson
// one level up.
//
//     forward:   X[k] = sum over n of  x[n] . e^(-2.pi.i.k.n/N)
//     inverse:   x[n] = (1/N) . sum over k of  X[k] . e^(+2.pi.i.k.n/N)
//
// Flip that minus and NOTHING BREAKS LOUDLY. The spectrum conjugates.
// Magnitudes are unchanged, Parseval is unchanged, and the round trip still
// closes because the inverse flipped too. Every check that looks only at
// |X[k]| passes. What has actually happened is that every phase downstream is
// now inverted, and it surfaces at P4's FFTW swap, months later.
//
// So the assertions below are chosen to be able to FAIL:
//
//     [1,2,3,4] -> X[1] = -2+2i     asymmetric; the other sign gives -2-2i
//     shifted impulse               flat magnitude, RAMPING phase -- the
//                                   direction of the ramp is the convention
//     [1, i, -1, -i] -> X = [0,4,0,0]  a complex input, whose energy lands at
//                                   k=1 under this sign and k=3 under the other
//
// And two that exist because a four-point case cannot see them:
//
//     Hermitian symmetry            X[N-k] == conj(X[k]) for REAL input only
//     N=1024 round trip             float-only accumulation drifts here; a
//                                   4-point case is exact either way

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <vector>

#include "vc/math/vc_dft.h"

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

bool near(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

// Complex comparison. Both parts, because a sign error lives in exactly one
// of them and a magnitude-only check is the thing this file exists to avoid.
bool near_c(vc::math::vc_complex a, vc::math::vc_complex b, float tol) {
    return near(a.real(), b.real(), tol) && near(a.imag(), b.imag(), tol);
}

using vc::math::complex_signal;
using vc::math::vc_complex;

constexpr float k_two_pi = 6.28318530717958647692F;

// Largest |a[i] - b[i]|. Returns infinity on a length mismatch rather than
// indexing off the end of the shorter one -- a transform that returns the
// wrong length is a failure, not undefined behaviour.
float max_abs_diff(const complex_signal& a, const complex_signal& b) {
    if (a.size() != b.size()) {
        return std::numeric_limits<float>::infinity();
    }
    float worst = 0.0F;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

float energy(const complex_signal& s) {
    double total = 0.0;
    for (const auto& v : s) {
        total += static_cast<double>(std::norm(v)); // norm() is |v|^2
    }
    return static_cast<float>(total);
}

} // namespace

int main() {
    using vc::math::dft1d;
    using vc::math::idft1d;
    using vc::math::to_signal;

    int passed = 0;
    int total = 0;
    auto expect = [&](bool ok, const char* what) {
        ++total;
        if (check(ok, what)) {
            ++passed;
        }
    };

    try {
        // ---- 1. degenerate sizes ------------------------------------------
        //
        // Empty is not an error and must not divide by zero in the inverse.
        // N=1 is the transform's fixed point: one sample, one bin, no
        // rotation anywhere.
        {
            const complex_signal empty{};
            expect(dft1d(empty).empty(), "dft1d(empty) is empty");
            expect(idft1d(empty).empty(), "idft1d(empty) is empty");

            const complex_signal one{vc_complex{7.0F, -3.0F}};
            const complex_signal one_spec = dft1d(one);
            expect(one_spec.size() == 1 &&
                       near_c(one_spec[0], vc_complex{7.0F, -3.0F}, 1e-6F),
                   "N=1: X[0] == x[0], untouched");
        }

        // ---- 2. DC is the plain sum ---------------------------------------
        //
        // k=0 is the one bin whose twiddle factor is e^0 = 1 everywhere, so
        // this isolates the accumulation from the rotation. If this fails,
        // the loop is wrong; if only later bins fail, the exponent is.
        const std::vector<float> ramp_real{1.0F, 2.0F, 3.0F, 4.0F};
        const complex_signal ramp = to_signal(ramp_real);
        const complex_signal spec = dft1d(ramp);

        expect(spec.size() == 4, "dft1d preserves length");
        expect(near_c(spec[0], vc_complex{10.0F, 0.0F}, 1e-5F),
               "X[0] == sum of x == 10 + 0i");

        // ---- 3. THE convention test ---------------------------------------
        //
        // [1,2,3,4] is asymmetric, which is the entire point. Hand-computed:
        //
        //     e^(-2.pi.i.1.n/4) for n = 0..3  =  1, -i, -1, +i
        //     X[1] = 1(1) + 2(-i) + 3(-1) + 4(+i) = (1-3) + i(-2+4) = -2 + 2i
        //
        // Under e^(+2.pi.i...) every imaginary part flips and X[1] = -2 - 2i.
        // Nothing else in this file except the shifted impulse and the
        // complex-input case can tell those two apart.
        expect(near_c(spec[1], vc_complex{-2.0F, 2.0F}, 1e-5F),
               "X[1] == -2 + 2i  (THE sign convention)");
        expect(near_c(spec[2], vc_complex{-2.0F, 0.0F}, 1e-5F),
               "X[2] == -2 + 0i");
        expect(near_c(spec[3], vc_complex{-2.0F, -2.0F}, 1e-5F),
               "X[3] == -2 - 2i");

        // ---- 4. Hermitian symmetry, for REAL input -------------------------
        //
        // x real  =>  X[N-k] == conj(X[k]). This catches an index-direction
        // slip that the magnitude checks cannot see, and it is the reason
        // Smith's real DFT needs only N/2+1 output points: the upper half
        // carries no new information. It does NOT hold for complex input --
        // see section 7, which is there to stop this being baked in.
        expect(near_c(spec[3], std::conj(spec[1]), 1e-5F),
               "real input: X[3] == conj(X[1])");
        expect(near_c(spec[2], std::conj(spec[2]), 1e-5F),
               "real input: X[2] is its own conjugate (real)");

        // ---- 5. impulse at the origin -------------------------------------
        //
        // delta contains every frequency in equal measure, all in phase.
        // A flat spectrum of exactly 1 is the strongest statement that no
        // stray normalisation crept into the FORWARD transform.
        {
            const complex_signal delta =
                to_signal(std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F});
            const complex_signal d = dft1d(delta);
            bool flat = d.size() == 4;
            for (const auto& v : d) {
                flat = flat && near_c(v, vc_complex{1.0F, 0.0F}, 1e-5F);
            }
            expect(flat, "impulse at 0 -> X[k] == 1 for every k");
        }

        // ---- 6. SHIFTED impulse: the phase ramp ----------------------------
        //
        // Move the impulse one sample and the magnitude does not change at
        // all -- still flat, still 1. Only the phase moves, and it ramps:
        //
        //     X[k] = e^(-2.pi.i.k/4)  =  1, -i, -1, +i
        //
        // That ramp runs the other way under the opposite convention, so this
        // is a second, independent witness to the sign. It is also why a
        // CENTRED impulse is a useless test: |DFT(delta_m)| is flat for every
        // shift m, so a magnitude-only check is blind to position entirely.
        {
            const complex_signal shifted =
                to_signal(std::vector<float>{0.0F, 1.0F, 0.0F, 0.0F});
            const complex_signal s = dft1d(shifted);
            expect(s.size() == 4 &&
                       near_c(s[0], vc_complex{1.0F, 0.0F}, 1e-5F) &&
                       near_c(s[1], vc_complex{0.0F, -1.0F}, 1e-5F) &&
                       near_c(s[2], vc_complex{-1.0F, 0.0F}, 1e-5F) &&
                       near_c(s[3], vc_complex{0.0F, 1.0F}, 1e-5F),
                   "impulse at 1 -> linear phase ramp 1, -i, -1, +i");

            bool magnitudes_flat = true;
            for (const auto& v : s) {
                magnitudes_flat =
                    magnitudes_flat && near(std::abs(v), 1.0F, 1e-5F);
            }
            expect(
                magnitudes_flat,
                "shifted impulse: magnitude still flat (shift is phase only)");
        }

        // ---- 7. a genuinely COMPLEX input ---------------------------------
        //
        // x[n] = e^(+2.pi.i.n/4) = 1, i, -1, -i -- a single sinusoid winding
        // ANTICLOCKWISE. Its entire energy belongs in one bin:
        //
        //     X = [0, 4, 0, 0]
        //
        // Two things this catches that no real-input test can. First, it
        // lands at k=1 under this convention and at k=3 under the other, so
        // it is sign-sensitive in a way that survives even if someone
        // "fixes" the real cases. Second, X[3] = 0 while conj(X[1]) = 4, so
        // it FAILS Hermitian symmetry -- which is correct, and which stops a
        // conjugate-symmetry shortcut from being baked in by accident while
        // passing every test above.
        {
            const complex_signal wind{
                vc_complex{1.0F, 0.0F}, vc_complex{0.0F, 1.0F},
                vc_complex{-1.0F, 0.0F}, vc_complex{0.0F, -1.0F}};
            const complex_signal w = dft1d(wind);
            expect(w.size() == 4 &&
                       near_c(w[0], vc_complex{0.0F, 0.0F}, 1e-5F) &&
                       near_c(w[1], vc_complex{4.0F, 0.0F}, 1e-5F) &&
                       near_c(w[2], vc_complex{0.0F, 0.0F}, 1e-5F) &&
                       near_c(w[3], vc_complex{0.0F, 0.0F}, 1e-5F),
                   "complex input e^(+i.2pi.n/4) -> all energy at k=1");
            expect(
                !near_c(w[3], std::conj(w[1]), 1e-5F),
                "complex input is NOT Hermitian (and must not be forced so)");
        }

        // ---- 7b. ODD N -----------------------------------------------
        //
        // Every other case here is N = 1, 4 or 1024. The header promises no
        // power-of-two requirement -- this is the check that makes that
        // promise testable rather than aspirational. An implementation with
        // a half-spectrum shortcut, or an N/2 that silently truncates, is
        // correct on all the even cases above and wrong here.
        //
        // N=3, x = [1,2,3]:  X = [6, -1.5+0.866i, -1.5-0.866i]
        // (sqrt(3)/2 = 0.8660254; confirmed against numpy.fft.)
        {
            const complex_signal odd =
                to_signal(std::vector<float>{1.0F, 2.0F, 3.0F});
            const complex_signal o = dft1d(odd);
            expect(o.size() == 3 &&
                       near_c(o[0], vc_complex{6.0F, 0.0F}, 1e-5F) &&
                       near_c(o[1], vc_complex{-1.5F, 0.8660254F}, 1e-5F) &&
                       near_c(o[2], vc_complex{-1.5F, -0.8660254F}, 1e-5F),
                   "odd N=3 transforms correctly (no power-of-two assumption)");

            const complex_signal odd_back = idft1d(o);
            expect(odd_back.size() == 3 && max_abs_diff(odd_back, odd) <= 1e-5F,
                   "odd N=3 round trip");
        }

        // ---- 7c. the real-input overload ------------------------------
        //
        // dft1d(real_view) must be exactly dft1d(to_signal(x)) -- it exists
        // for call-site ergonomics, not as a second algorithm. Asserting the
        // equivalence is what stops the two paths drifting if either is ever
        // specialised (a real-input DFT exploiting Hermitian symmetry is the
        // obvious future optimisation, and this check is what would catch it
        // disagreeing with the reference).
        {
            const std::vector<float> reals{1.0F, 2.0F, 3.0F, 4.0F};
            const complex_signal via_overload = dft1d(reals);
            const complex_signal via_lift = dft1d(to_signal(reals));
            expect(max_abs_diff(via_overload, via_lift) == 0.0F,
                   "dft1d(real_view) is bit-identical to dft1d(to_signal(x))");
            expect(via_overload.size() == 4 &&
                       near_c(via_overload[1], vc_complex{-2.0F, 2.0F}, 1e-5F),
                   "dft1d(real_view) gives the same X[1] = -2 + 2i");
        }

        // ---- 8. round trip -------------------------------------------------
        //
        // The 1/N lives on the inverse and nowhere else. If it is split
        // across both, or applied twice, this is where it shows.
        {
            const complex_signal back = idft1d(spec);
            expect(back.size() == 4 && max_abs_diff(back, ramp) <= 1e-5F,
                   "idft1d(dft1d(x)) == x");
        }

        // ---- 9. Parseval ---------------------------------------------------
        //
        //     sum |x[n]|^2  ==  (1/N) . sum |X[k]|^2
        //
        // For [1,2,3,4]:  1+4+9+16 = 30,  and (100+8+4+8)/4 = 30.
        //
        // Note this is a MAGNITUDE identity -- it holds under either sign
        // convention, which is exactly why it cannot be the only check.
        {
            const float time_energy = energy(ramp);
            const float freq_energy = energy(spec) / 4.0F;
            expect(near(time_energy, 30.0F, 1e-4F), "sum |x|^2 == 30");
            expect(near(freq_energy, 30.0F, 1e-4F), "(1/N) sum |X|^2 == 30");
        }

        // ---- 10. large N: the test a 4-point case cannot be --------------
        //
        // This is the ONLY check in this file that pins the DOUBLE
        // accumulator. Every output bin here is a sum of 1024 products. At
        // N=4 the hand cases are exact either way, so nothing above can see
        // it.
        //
        // The tolerance is measured, not guessed. Against this exact signal,
        // with float32 storage in both cases:
        //
        //     complex<double> accumulator   round-trip max|err|  4.7e-10
        //     complex<float>  accumulator   round-trip max|err|  1.8e-06
        //
        // so 1e-7 below passes with ~200x margin on double and fails by ~18x
        // on float. An earlier draft used 1e-6, which float missed by under
        // 2x -- too tight a margin to survive a different summation order.
        {
            constexpr std::size_t n_big = 1024;
            std::vector<float> big_real(n_big);
            for (std::size_t n = 0; n < n_big; ++n) {
                const float t =
                    static_cast<float>(n) / static_cast<float>(n_big);
                big_real[n] = std::cos(k_two_pi * 5.0F * t) +
                              0.5F * std::sin(k_two_pi * 137.0F * t);
            }
            const complex_signal big = to_signal(big_real);
            const complex_signal big_spec = dft1d(big);
            const complex_signal big_back = idft1d(big_spec);

            expect(big_spec.size() == n_big, "large N: length preserved");
            expect(max_abs_diff(big_back, big) <= 1e-7F,
                   "N=1024 round trip within 1e-7 (needs double accumulation)");

            // Parseval again, but RELATIVE. The absolute discrepancy scales
            // with the signal's magnitude, and a fixed absolute tolerance
            // that passes at N=4 fails here for entirely correct code --
            // the same lesson the separability check in 05_convolve learned
            // the hard way.
            //
            // THIS ONE DOES NOT TEST THE ACCUMULATOR, despite sitting next
            // to the check that does. Measured, a float-only chain gives
            // 1.4e-07 relative here -- it passes 1e-6 comfortably. Parseval
            // compares two energies, and energy is dominated by the largest
            // bins, which are the accurately computed ones. It is a
            // normalisation check, and only that.
            const float t_energy = energy(big);
            const float f_energy = energy(big_spec) / static_cast<float>(n_big);
            const float rel =
                std::fabs(t_energy - f_energy) / std::max(t_energy, 1e-12F);
            expect(rel <= 1e-6F, "N=1024 Parseval holds to 1e-6 RELATIVE");
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        return 1;
    }

    std::cout << "06_dft: " << passed << " / " << total << " checks passed\n";
    if (passed != total) {
        std::cout << "06_dft: FAIL\n";
        return 1;
    }
    std::cout << "06_dft: OK\n";
    return 0;
}
