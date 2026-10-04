// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 07_dft2d -- acceptance checks for the 2-D DFT.
//
// WHAT THIS FILE IS FOR
//
// dft2d is two passes of dft1d, and dft1d is already trusted (06_dft, 28
// checks). So almost nothing here is about the transform being correct in the
// abstract; it is about the BOOKKEEPING of running a verified 1-D transform
// over a 2-D buffer. Those are different failure modes, and the file is
// organised around the four that actually happen:
//
//   1. A TRANSPOSED PASS. Rows transformed where columns were meant, or a
//      stride computed from the wrong dimension. On a SQUARE image the result
//      is the transpose of the right answer -- which for a symmetric test
//      input IS the right answer. Invisible. Checks 5 and 6 are non-square
//      and in both orientations, and they are the point of this file.
//
//   2. A DOUBLE-SCALED NORMALISATION. Szeliski's Eq 3.60 puts 1/(M.N) on the
//      FORWARD transform; this library puts nothing there and 1/N on each
//      inverse pass. Copy Eq 3.60 and the round trip comes back M.N times too
//      small. Checks 8 and 9.
//
//   3. SHIFT BLINDNESS. |DFT(impulse)| is flat wherever the impulse is, so a
//      magnitude-only criterion cannot see position at all. Check 11
//      DEMONSTRATES the blindness, and check 10 is the criterion that
//      replaces it. This is P2 doc F.1(b).
//
//   4. LOSING THE IMAGINARY PART. idft2d returns complex on purpose. Check 13.
//
// THE REFERENCE IS INDEPENDENT. naive_dft2d below evaluates the definition
// directly, O((M.N)^2), with no separation and no factorisation. It is not a
// second copy of the implementation; it is the equation. If the separated
// version and the direct version agree on non-square input, the separation
// itself is right.
//
// TOLERANCES are relative to signal RMS, not absolute (P2 doc F.3), except
// where a value is small and exact enough to compare directly.

#include <cmath>
#include <complex>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/math/vc_dft.h"

namespace {

using vc::math::complex_signal;
using vc::math::complex_view;
using vc::math::vc_complex;

constexpr float k_two_pi = 6.28318530717958647692F;
constexpr double k_two_pi_d = 6.28318530717958647692;

int passed = 0;
int total = 0;

void expect(bool condition, const char* what) {
    ++total;
    if (condition) {
        ++passed;
    } else {
        std::cerr << "FAILED: " << what << '\n';
    }
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

bool near_c(vc_complex a, vc_complex b, float tol) {
    return near(a.real(), b.real(), tol) && near(a.imag(), b.imag(), tol);
}

// Largest |a[i] - b[i]|. Infinity on a length mismatch -- a transform that
// returns the wrong length is a failure, not undefined behaviour.
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

float max_imag(const complex_signal& a) {
    float worst = 0.0F;
    for (const vc_complex& z : a) {
        worst = std::max(worst, std::abs(z.imag()));
    }
    return worst;
}

float rms(const complex_signal& a) {
    if (a.empty()) {
        return 0.0F;
    }
    double acc = 0.0;
    for (const vc_complex& z : a) {
        acc += static_cast<double>(std::norm(z));
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(a.size())));
}

// Sum of |z|^2 -- for Parseval.
double energy(const complex_signal& a) {
    double acc = 0.0;
    for (const vc_complex& z : a) {
        acc += static_cast<double>(std::norm(z));
    }
    return acc;
}

// ---- the independent reference ---------------------------------------------
//
// Szeliski Eq 3.60 evaluated literally, row-major, O((M.N)^2):
//
//     H(kx,ky) = sum over y, sum over x of  h(x,y) . e^(-2.pi.i.(kx.x/M + ky.y/N))
//
// NOTE THE MISSING 1/(M.N). Eq 3.60 carries one on the forward transform and
// this library does not -- see the scaling note in vc_dft.h. This reference
// deliberately matches the LIBRARY's convention, because its job is to check
// the separation, not to re-litigate the normalisation. Check 8 is what pins
// the normalisation.
//
// Accumulates in double and narrows once at the end, same as dft1d.
complex_signal naive_dft2d(complex_view plane, std::size_t width,
                           std::size_t height) {
    complex_signal out(plane.size());
    for (std::size_t ky = 0; ky < height; ++ky) {
        for (std::size_t kx = 0; kx < width; ++kx) {
            std::complex<double> sum{0.0, 0.0};
            for (std::size_t y = 0; y < height; ++y) {
                for (std::size_t x = 0; x < width; ++x) {
                    const double angle =
                        -k_two_pi_d *
                        ((static_cast<double>(kx * x) /
                          static_cast<double>(width)) +
                         (static_cast<double>(ky * y) /
                          static_cast<double>(height)));
                    sum += static_cast<std::complex<double>>(
                               plane[(y * width) + x]) *
                           std::complex<double>{std::cos(angle),
                                                std::sin(angle)};
                }
            }
            out[(ky * width) + kx] =
                vc_complex{static_cast<float>(sum.real()),
                           static_cast<float>(sum.imag())};
        }
    }
    return out;
}

complex_signal lift(const std::vector<float>& real_samples) {
    complex_signal out;
    out.reserve(real_samples.size());
    for (const float v : real_samples) {
        out.emplace_back(v, 0.0F);
    }
    return out;
}

} // namespace

int main() {
    using vc::math::dft1d;
    using vc::math::dft2d;
    using vc::math::idft2d;

    try {
        // ---- 1. degenerate sizes ---------------------------------------
        //
        // Empty in, empty out -- the header's contract, and the reason the
        // division inside idft2d can never be by zero.
        {
            const complex_signal none;
            expect(dft2d(none, 0, 0).empty(), "dft2d of empty is empty");
            expect(idft2d(none, 0, 0).empty(), "idft2d of empty is empty");
        }

        // ---- 2. dimension validation -----------------------------------
        //
        // 8 samples cannot be 3 wide. The project throws vc_exception with
        // vc_error_code, never a bare std:: exception -- the only
        // throw std:: in this repo was a mistake and was removed.
        //
        // The header also requires this check to AVOID multiplying width by
        // height, since that product can overflow size_t and then compare
        // equal to nonsense. These cases do not exercise the overflow; they
        // exercise that a mismatch is rejected at all.
        {
            const complex_signal eight(8, vc_complex{1.0F, 0.0F});
            bool threw_mismatch = false;
            try {
                (void)dft2d(eight, 3, 3);
            } catch (const vc::vc_exception& e) {
                threw_mismatch =
                    (e.code() == vc::vc_error_code::invalid_argument);
            }
            expect(threw_mismatch, "dft2d rejects width*height != size");

            bool threw_zero = false;
            try {
                (void)dft2d(eight, 0, 8);
            } catch (const vc::vc_exception& e) {
                threw_zero =
                    (e.code() == vc::vc_error_code::invalid_argument);
            }
            expect(threw_zero, "dft2d rejects zero width on non-empty input");
        }

        // ---- 3. a single row reduces to dft1d --------------------------
        //
        // A 1-pixel-high image has nothing to transform in the column pass
        // (a 1-point DFT is the sample itself), so the answer must be exactly
        // the 1-D transform. This ties the new code to the 28 checks that
        // already passed, and it is the cheapest possible smoke test.
        {
            const complex_signal row = lift({1.0F, 2.0F, 3.0F, 4.0F});
            const complex_signal one_d = dft1d(row);
            const complex_signal two_d = dft2d(row, 4, 1);
            expect(two_d.size() == 4, "dft2d 4x1 keeps length");
            expect(max_abs_diff(two_d, one_d) <= 1e-5F,
                   "dft2d of a single ROW equals dft1d");
            // Pinned by hand, the same vector 06_dft uses for the sign
            // convention: [1,2,3,4] -> [10, -2+2i, -2, -2-2i].
            expect(two_d.size() == 4 &&
                       near_c(two_d[1], vc_complex{-2.0F, 2.0F}, 1e-5F),
                   "dft2d 4x1 X[1] = -2 + 2i (sign convention survives 2-D)");
        }

        // ---- 4. a single column reduces to dft1d too -------------------
        //
        // Same data, 1 wide by 4 high. If width and height are swapped
        // anywhere, this and check 3 cannot both pass.
        {
            const complex_signal col = lift({1.0F, 2.0F, 3.0F, 4.0F});
            const complex_signal one_d = dft1d(col);
            const complex_signal two_d = dft2d(col, 1, 4);
            expect(max_abs_diff(two_d, one_d) <= 1e-5F,
                   "dft2d of a single COLUMN equals dft1d");
        }

        // ---- 5. NON-SQUARE, 4 wide x 2 high ----------------------------
        //
        // THE CHECK THIS FILE EXISTS FOR. Against the direct evaluation of
        // the definition, so it validates the separation itself.
        //
        // Hand-pinned as well, because an independent reference that is
        // itself wrong proves nothing. For
        //
        //     1 2 3 4
        //     5 6 7 8
        //
        // the spectrum row-major is
        //
        //     36, -4+4i, -4, -4-4i, -16, 0, 0, 0
        //
        // DC = 36 = 1+2+...+8. The second row is all zero except its DC,
        // because the two image rows differ by a constant 4 at every x: the
        // vertical direction holds only a DC step, and -16 is that step
        // times the 4 columns it spans.
        {
            const complex_signal plane =
                lift({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F});
            const complex_signal got = dft2d(plane, 4, 2);
            const complex_signal want = naive_dft2d(plane, 4, 2);
            expect(got.size() == 8, "dft2d 4x2 keeps length");
            expect(max_abs_diff(got, want) <= 1e-4F,
                   "dft2d 4x2 matches the direct definition");

            const complex_signal pinned = {
                vc_complex{36.0F, 0.0F},  vc_complex{-4.0F, 4.0F},
                vc_complex{-4.0F, 0.0F},  vc_complex{-4.0F, -4.0F},
                vc_complex{-16.0F, 0.0F}, vc_complex{0.0F, 0.0F},
                vc_complex{0.0F, 0.0F},   vc_complex{0.0F, 0.0F}};
            expect(max_abs_diff(got, pinned) <= 1e-4F,
                   "dft2d 4x2 matches the hand-computed spectrum");

            // AND THE REFERENCE ITSELF, against the same hand values. This
            // one passes before any implementation exists, which is the
            // point: if every other check is red you still know whether
            // naive_dft2d is sound, so a failure cannot be blamed on the
            // yardstick. An untested reference proves nothing about the
            // thing it measures.
            expect(max_abs_diff(want, pinned) <= 1e-4F,
                   "the direct reference matches the hand-computed spectrum");
        }

        // ---- 6. NON-SQUARE the other way, 2 wide x 4 high --------------
        //
        // Same eight samples, transposed shape. A width/height swap or a
        // stride taken from the wrong dimension passes check 5 and fails
        // here, or vice versa. Neither orientation alone is sufficient.
        {
            const complex_signal plane =
                lift({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F});
            const complex_signal got = dft2d(plane, 2, 4);
            const complex_signal want = naive_dft2d(plane, 2, 4);
            expect(got.size() == 8, "dft2d 2x4 keeps length");
            expect(max_abs_diff(got, want) <= 1e-4F,
                   "dft2d 2x4 matches the direct definition");
        }

        // ---- 7. DC bin is the sum of every sample ----------------------
        //
        // All the exponentials are 1 at kx=ky=0, so X[0] is a plain total.
        // Divide by width*height to get the mean brightness.
        {
            const complex_signal plane =
                lift({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F});
            const complex_signal spec = dft2d(plane, 4, 2);
            expect(!spec.empty() && near(spec[0].real(), 36.0F, 1e-4F) &&
                       near(spec[0].imag(), 0.0F, 1e-4F),
                   "dft2d X[0,0] == sum of all samples");
        }

        // ---- 8. ROUND TRIP, and the normalisation it pins --------------
        //
        // The 1/(M.N) must appear EXACTLY ONCE across the pair. idft2d gets
        // it for free from two idft1d passes -- 1/width on the rows and
        // 1/height on the columns.
        //
        // This is the check that catches Szeliski's Eq 3.60 being copied
        // literally into the forward pass. If it is, the result here comes
        // back 1/(4*2) = 1/8 of the input. Non-square on purpose, so a
        // 1/width-twice error is also visible.
        {
            const complex_signal plane =
                lift({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F});
            const complex_signal back = idft2d(dft2d(plane, 4, 2), 4, 2);
            expect(back.size() == 8, "round trip keeps length");
            expect(max_abs_diff(back, plane) <= 1e-4F,
                   "idft2d(dft2d(x)) == x on 4x2 (pins the 1/MN exactly once)");
        }

        // ---- 9. round trip on something image-sized --------------------
        //
        // 64x48: non-square, 3072 samples, every output bin a sum of 3072
        // products. The 4x2 case above is exact either way; this one is
        // where an accumulator or an ordering mistake would show.
        //
        // Tolerance RELATIVE to RMS (P2 doc F.3). An absolute bound that
        // passes at 4x2 fails here for entirely correct code, because the
        // discrepancy scales with the signal.
        {
            constexpr std::size_t w = 64;
            constexpr std::size_t h = 48;
            std::vector<float> image(w * h);
            for (std::size_t y = 0; y < h; ++y) {
                for (std::size_t x = 0; x < w; ++x) {
                    const float fx = static_cast<float>(x);
                    const float fy = static_cast<float>(y);
                    image[(y * w) + x] =
                        0.5F + (0.3F * std::cos(k_two_pi * 3.0F * fx /
                                                static_cast<float>(w))) +
                        (0.2F * std::sin(k_two_pi * 7.0F * fy /
                                         static_cast<float>(h)));
                }
            }
            const complex_signal plane = lift(image);
            const complex_signal spec = dft2d(plane, w, h);
            const complex_signal back = idft2d(spec, w, h);
            expect(spec.size() == w * h, "64x48: length preserved");

            const float scale = std::max(rms(plane), 1e-12F);
            expect(max_abs_diff(back, plane) / scale <= 1e-5F,
                   "64x48 round trip within 1e-5 RELATIVE to RMS");

            // Parseval in 2-D: sum|x|^2 == (1/MN) sum|X|^2. A magnitude
            // identity, so it holds under either sign convention and cannot
            // be the only check -- but it does catch a dropped bin.
            const double t_energy = energy(plane);
            const double f_energy =
                energy(spec) / static_cast<double>(w * h);
            const double rel = std::fabs(t_energy - f_energy) /
                               std::max(t_energy, 1e-12);
            expect(rel <= 1e-5, "64x48 Parseval holds to 1e-5 relative");
        }

        // ---- 10. off-centre impulse: the PHASE carries the position ----
        //
        // P2 doc F.1(b). An impulse at (x0, y0) in a w x h plane transforms
        // to exactly
        //
        //     X[kx,ky] = e^(-2.pi.i.(kx.x0/w + ky.y0/h))
        //
        // -- magnitude 1 everywhere, and the position encoded entirely in
        // the phase.
        //
        // COMPARED AS COMPLEX VALUES, NOT AS PHASE ANGLES, and that is
        // deliberate. atan2 returns only (-pi, pi], so a linear phase ramp
        // WRAPS -- at w=4, x0=1 the true phases are 0, -90, -180, -270 and
        // the last comes back as +90. A "is the phase linear" assertion
        // would go red on correct code (Smith Ch 8, Polar Nuisance 5).
        // Comparing complex values sidesteps wrapping entirely.
        //
        // Hand-checkable: impulse at (1,1) in 4 wide x 2 high gives
        //     1, -i, -1, +i,  -1, +i, 1, -i
        {
            constexpr std::size_t w = 4;
            constexpr std::size_t h = 2;
            constexpr std::size_t x0 = 1;
            constexpr std::size_t y0 = 1;
            std::vector<float> d(w * h, 0.0F);
            d[(y0 * w) + x0] = 1.0F;
            const complex_signal spec = dft2d(lift(d), w, h);

            bool ramp_ok = spec.size() == w * h;
            bool flat_ok = ramp_ok;
            for (std::size_t ky = 0; ky < h && ramp_ok; ++ky) {
                for (std::size_t kx = 0; kx < w; ++kx) {
                    const float angle =
                        -k_two_pi *
                        ((static_cast<float>(kx * x0) /
                          static_cast<float>(w)) +
                         (static_cast<float>(ky * y0) /
                          static_cast<float>(h)));
                    const vc_complex want{std::cos(angle), std::sin(angle)};
                    const vc_complex got = spec[(ky * w) + kx];
                    ramp_ok = ramp_ok && near_c(got, want, 1e-5F);
                    flat_ok = flat_ok && near(std::abs(got), 1.0F, 1e-5F);
                }
            }
            expect(ramp_ok,
                   "off-centre impulse gives the predicted phase ramp");
            expect(flat_ok, "off-centre impulse has flat unit magnitude");

            const complex_signal pinned = {
                vc_complex{1.0F, 0.0F},  vc_complex{0.0F, -1.0F},
                vc_complex{-1.0F, 0.0F}, vc_complex{0.0F, 1.0F},
                vc_complex{-1.0F, 0.0F}, vc_complex{0.0F, 1.0F},
                vc_complex{1.0F, 0.0F},  vc_complex{0.0F, -1.0F}};
            expect(max_abs_diff(spec, pinned) <= 1e-5F,
                   "impulse at (1,1) in 4x2 matches the hand-computed ramp");
        }

        // ---- 11. and why magnitude ALONE could not have done that ------
        //
        // Two impulses in different places. Identical magnitudes, every bin.
        // So the criterion "transform of an impulse has flat magnitude" is
        // satisfied by EVERY impulse position and therefore tests nothing
        // about placement -- which is exactly the shift-blindness P2 doc
        // F.1(b) flags, written down as an assertion rather than a warning.
        {
            constexpr std::size_t w = 4;
            constexpr std::size_t h = 2;
            std::vector<float> a(w * h, 0.0F);
            std::vector<float> b(w * h, 0.0F);
            a[1] = 1.0F;                 // (x=1, y=0)
            b[(1 * w) + 3] = 1.0F;       // (x=3, y=1)
            const complex_signal sa = dft2d(lift(a), w, h);
            const complex_signal sb = dft2d(lift(b), w, h);

            bool mags_identical = sa.size() == sb.size() && !sa.empty();
            for (std::size_t i = 0; i < sa.size() && mags_identical; ++i) {
                mags_identical = near(std::abs(sa[i]), std::abs(sb[i]), 1e-5F);
            }
            expect(mags_identical,
                   "two DIFFERENT impulses have identical magnitudes "
                   "(magnitude is shift-blind)");
            expect(max_abs_diff(sa, sb) > 0.5F,
                   "...but their complex spectra differ, so phase sees it");
        }

        // ---- 12. a pure 2-D sinusoid lands on a TWIN PAIR --------------
        //
        // cos(...) is two counter-rotating dots of half amplitude each, so a
        // single oriented stripe pattern shows up as TWO bright bins,
        // mirrored through the origin -- not one. This is what makes a
        // spectrum image show two spots, and it is normal, not a bug.
        //
        // 8x8, kx0 = 2, ky0 = 1, amplitude 1. The pair lands at (2,1) and
        // at (8-2, 8-1) = (6,7), each with magnitude M.N/2 = 32. Everything
        // else is zero.
        {
            constexpr std::size_t w = 8;
            constexpr std::size_t h = 8;
            std::vector<float> s(w * h);
            for (std::size_t y = 0; y < h; ++y) {
                for (std::size_t x = 0; x < w; ++x) {
                    const float phase =
                        k_two_pi * ((2.0F * static_cast<float>(x) /
                                     static_cast<float>(w)) +
                                    (1.0F * static_cast<float>(y) /
                                     static_cast<float>(h)));
                    s[(y * w) + x] = std::cos(phase);
                }
            }
            const complex_signal spec = dft2d(lift(s), w, h);
            const float peak = 0.5F * static_cast<float>(w * h); // 32

            bool twins_ok = spec.size() == w * h;
            if (twins_ok) {
                twins_ok = near(std::abs(spec[(1 * w) + 2]), peak, 1e-2F) &&
                           near(std::abs(spec[(7 * w) + 6]), peak, 1e-2F);
            }
            expect(twins_ok,
                   "a 2-D sinusoid spikes at BOTH twins, magnitude M.N/2");

            bool rest_quiet = twins_ok;
            for (std::size_t i = 0; i < spec.size() && rest_quiet; ++i) {
                const bool is_twin = (i == (1 * w) + 2) || (i == (7 * w) + 6);
                if (!is_twin) {
                    rest_quiet = std::abs(spec[i]) <= 1e-2F;
                }
            }
            expect(rest_quiet, "...and every other bin is zero");
        }

        // ---- 13. conjugate symmetry, and a real inverse ----------------
        //
        // For REAL input the spectrum satisfies
        //
        //     X[-kx, -ky] = conj(X[kx, ky])        indices mod w and mod h
        //
        // which is the 2-D form of the twin relation, and is why a real
        // image's spectrum carries no more information than the image did.
        //
        // The second assertion is 06_dft check 8b in 2-D: because the twins
        // are intact, the imaginary part of the inverse cancels pairwise and
        // the image comes back REAL. Keep this in mind at conv_theorem --
        // a filter that breaks the symmetry makes the inverse genuinely
        // complex, and calling .real() throws away that evidence.
        {
            constexpr std::size_t w = 8;
            constexpr std::size_t h = 4;
            std::vector<float> im(w * h);
            for (std::size_t i = 0; i < im.size(); ++i) {
                // deterministic, asymmetric, nothing special about it
                im[i] = std::sin(0.7F * static_cast<float>(i)) +
                        (0.4F * static_cast<float>(i % 5));
            }
            const complex_signal plane = lift(im);
            const complex_signal spec = dft2d(plane, w, h);

            bool herm = spec.size() == w * h;
            const float scale = std::max(rms(spec), 1e-12F);
            for (std::size_t ky = 0; ky < h && herm; ++ky) {
                for (std::size_t kx = 0; kx < w; ++kx) {
                    const vc_complex a = spec[(ky * w) + kx];
                    const vc_complex b =
                        spec[(((h - ky) % h) * w) + ((w - kx) % w)];
                    herm = herm &&
                           (std::abs(a - std::conj(b)) / scale <= 1e-5F);
                }
            }
            expect(herm,
                   "real input: X[-kx,-ky] == conj(X[kx,ky]) (2-D twins)");

            // The size guard is not ceremony. max_imag({}) is 0, so without
            // it this check PASSES on a stub that returns nothing -- a test
            // that is satisfied by the absence of an implementation is worse
            // than no test, because it reports green.
            const complex_signal back = idft2d(spec, w, h);
            expect(back.size() == w * h &&
                       max_imag(back) / std::max(rms(plane), 1e-12F) <= 1e-5F,
                   "inverse of a real image's spectrum is real");
        }

    } catch (const vc::vc_exception& e) {
        std::cerr << "UNEXPECTED vc_exception: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        return 1;
    }

    std::cout << "07_dft2d: " << passed << " / " << total
              << " checks passed\n";
    if (passed != total) {
        std::cout << "07_dft2d: FAIL\n";
        return 1;
    }
    std::cout << "07_dft2d: OK\n";
    return 0;
}
