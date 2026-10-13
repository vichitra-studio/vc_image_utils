// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 14_poisson -- acceptance checks for poisson_fft: solving grad^2 phi = f.
//
// P2 section C's S7. Reasoning lives in include/vc/pixelops/vc_poisson.h and
// curriculum/notes/poisson.txt; this file only states what must be true.
//
// ---- READ THIS BEFORE TRUSTING A GREEN RUN ----
//
// GROUPS 7, 11 AND 12 PASS VACUOUSLY AGAINST AN UNIMPLEMENTED STUB. They assert
// that a bad input THROWS, and a stub that throws unconditionally satisfies them
// without computing anything. This is exactly how 11_conv_theorem's first draft
// shipped four rejection checks that tested nothing -- the stub threw, the
// checks were happy, and the real rejection logic was never exercised.
//
// The defence is that each of those groups is paired with an ACCEPTANCE check:
// a valid input of the same shape that must NOT throw. A stub fails those
// immediately. Do not delete the acceptance halves.
//
// ---- WHICH GROUPS ARE EXACT AND WHY ----
//
// Group 4 is the only one whose expected values were computed ON PAPER, with no
// reference to any code in this repository. Every other group derives its
// expectation from laplacian() or from a formula implemented here, so a shared
// misunderstanding could pass them all. Group 4 is the anchor; if it and
// nothing else fails, suspect the paper. If everything else fails and group 4
// passes, suspect the solver.
//
// Groups 1, 4 and 6 are exact equalities: integer inputs, integer weights,
// exactly representable results. Groups built from cos() carry a tolerance
// because cos(pi) is not exactly -1 in float.
//
// ---- WHAT EACH GROUP CATCHES, AND WHAT IT IS BLIND TO ----
//
// 1   laplacian_scale against values worked out by hand at 4x4 and at 8x4.
//     The NON-SQUARE half is the one that matters: on a square grid a swapped
//     width and height is invisible, which is the same blindness 07_dft2d
//     records. At 8 wide by 4 high, scale(2,0) is -2 and scale(0,2) is -4; swap
//     the dimensions and both read -4.
//     BLIND TO: whether poisson_fft actually calls this.
//
// 2   laplacian_scale against the DFT OF THE ACTUAL STENCIL, bin by bin. The
//     independent route -- place make_laplacian()'s weights on the grid wrapped
//     around the origin, transform, and the real part must be the formula while
//     the imaginary part must be zero. THIS IS THE GROUP THAT CATCHES THE
//     CONTINUOUS -(u^2+v^2) TRAP, because the trap agrees at low frequency and
//     this compares every bin including the corner.
//     Also pins that the response is REAL, which is the symmetry claim.
//
// 3   that the forward operator really does only SCALE a wave. A single-
//     frequency image through laplacian(wrap) must come back as that same image
//     times one number, at every pixel. This is the whole premise of the
//     solver, checked rather than assumed.
//
// 4   THE HAND CASE. phi = [4,-1,-2,-1] on a 4x1 grid under wrap gives exactly
//     f = [-10,4,2,4], and the solve gives phi back. Worked on paper.
//     Note a 4x1 image under wrap reduces the 5-point stencil to the 1-D one:
//     the up and down neighbours are the row itself, so they contribute
//     centre + centre - 2.centre = 0. That is why a 2-D operator can be checked
//     against 1-D arithmetic here.
//
// 5   the round trip at a real size, non-square, multi-frequency.
//     BLIND TO: a sign error in the whole stencil, which cancels -- f is built
//     by the same operator the solver inverts. Group 1 and group 4 pin the sign
//     independently, which is why they must not be reordered after this one.
//
// 6   THE LEVEL IS UNRECOVERABLE, demonstrated rather than asserted. Adding a
//     constant to the input leaves f bit-for-bit identical, and the solver
//     returns the ZERO-MEAN answer either way -- not the input it happened to
//     come from.
//
// 7   zero-mean rejection. Needs a hand-built f, because f = laplacian(phi,wrap)
//     satisfies the precondition by construction and can never trip it.
//     PAIRED with an acceptance check. See the vacuous-pass warning above.
//
// 8   the returned image has zero mean, which is the choice the header commits
//     to and the only thing that makes the round trip exact.
//
// 9   non-square BOTH WAYS, 8x4 and 4x8. See group 1.
//
// 10  three channels, solved independently and agreeing with three separate
//     one-channel solves. Catches an interleaved/planar index confusion, which
//     is invisible at one channel -- the bug that survived every one-channel
//     test in conv_theorem.
//
// 11  non-f32 rejection. PAIRED. See the vacuous-pass warning.
//
// 12  out-of-range bin rejection in laplacian_scale. PAIRED.
//
// 13  1x1, the floor. Only bin 0 exists, f must be zero, and the answer is zero.
//
// 14  THE OTHER DIRECTION: laplacian(poisson_fft(f)) == f, on a hand-built
//     dipole f. Every other round trip builds f with laplacian() and so cancels
//     a whole-stencil error; this one does not, and it is the only group that
//     feeds the solver an f that did not come from the forward operator.
//
// 15  64x32, where the bin beside DC has a scale of about -0.0096 and dividing
//     by it amplifies ~100x. Nothing smaller exercises that, and it is the size
//     the tolerance constants should be measured against.
//
// 16  THE CLAMP TRAP. clamp-built f also sums to zero, so the precondition does
//     NOT catch it, and the answer comes back 66% wrong with no NaN and no
//     exception. Pinned as a test so the warning is executable.
//
// 17  an all-zero f, which is the RMS-floor path. Legal, and whether a relative
//     zero-mean threshold accepts it depends on a floor existing.
//
// 18  the hand case TRANSPOSED to 1 wide by 4 high. A width/height swap here
//     preserves the element count, so grid2d cannot catch it and group 9's
//     two-real-axis cases may not either.
//
// 21  a STRONG low-frequency component must not trip the imaginary-residue
//     check. Group 15's 64x32 field uses amplitude 1.0, which was hiding a
//     shipped bug: the residue was scaled against the INPUT's RMS, and
//     poisson_fft's output can be ~1660x its input near DC.
//
// 20  the zero-mean threshold's SCALING, not merely its existence. Every other
//     rejection check feeds a gross violation and so passes for any threshold
//     within orders of magnitude of right; this one feeds 10x the tolerance,
//     which a count-scaled threshold would wrongly accept.
//
// 19  zero mean is PER CHANNEL. A whole-image check accepts a +5/-5 pair and
//     returns garbage for both channels; the first reference implementation
//     written against this header made exactly that mistake.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/math/vc_dft.h"
#include "vc/math/vc_grid2d.h"
#include "vc/pixelops/vc_edge_policy.h"
#include "vc/pixelops/vc_laplacian.h"
#include "vc/pixelops/vc_poisson.h"

namespace {

int total_checks = 0;
int passed = 0;

void expect(bool condition, const char* what) {
    ++total_checks;
    if (condition) {
        ++passed;
    } else {
        std::cerr << "FAILED: " << what << '\n';
    }
}

vc::vc_image make_image(vc::image_dim w,
                        vc::image_dim h,
                        vc::channel_count ch,
                        const std::vector<float>& values) {
    vc::vc_image_writer out{w, h, ch, vc::buf_f32{0.0F}};
    std::size_t i = 0;
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            for (vc::channel_count c = 0; c < ch; ++c) {
                out.at<vc::buf_f32>(x, y, c) = values[i++];
            }
        }
    }
    return std::move(out).seal();
}

vc::vc_image from_rule(vc::image_dim w,
                       vc::image_dim h,
                       const std::function<float(int, int)>& f) {
    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(w) * h);
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            v.push_back(f(static_cast<int>(x), static_cast<int>(y)));
        }
    }
    return make_image(w, h, 1, v);
}

float pixel(const vc::vc_image& img,
            vc::image_dim x,
            vc::image_dim y,
            vc::channel_count c = 0) {
    return img.at<vc::buf_f32>(x, y, c);
}

float mean_of(const vc::vc_image& img, vc::channel_count c = 0) {
    double sum = 0.0;
    for (vc::image_dim y = 0; y < img.height(); ++y) {
        for (vc::image_dim x = 0; x < img.width(); ++x) {
            sum += static_cast<double>(pixel(img, x, y, c));
        }
    }
    const auto n = static_cast<double>(img.width()) * img.height();
    return static_cast<float>(sum / n);
}

float rms_all_channels(const vc::vc_image& img) {
    double sum = 0.0;
    std::size_t n = 0;
    for (vc::image_dim y = 0; y < img.height(); ++y) {
        for (vc::image_dim x = 0; x < img.width(); ++x) {
            for (vc::channel_count c = 0; c < img.channels(); ++c) {
                const auto v = static_cast<double>(pixel(img, x, y, c));
                sum += v * v;
                ++n;
            }
        }
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

float rms_of(const vc::vc_image& img, vc::channel_count c = 0) {
    double sum = 0.0;
    for (vc::image_dim y = 0; y < img.height(); ++y) {
        for (vc::image_dim x = 0; x < img.width(); ++x) {
            const auto v = static_cast<double>(pixel(img, x, y, c));
            sum += v * v;
        }
    }
    const auto n = static_cast<double>(img.width()) * img.height();
    return static_cast<float>(std::sqrt(sum / n));
}

bool same_shape(const vc::vc_image& a, const vc::vc_image& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           a.channels() == b.channels();
}

// Every sample is finite. CHECKED SEPARATELY AND FIRST, because the comparison
// below cannot see a NaN.
//
// std::max(x, NaN) returns x -- `x < NaN` is false, so the maximum is reported
// as the value that was already there. An all-NaN image therefore has a
// max_abs_diff of 0.0 and sails through any tolerance. Measured, not reasoned:
// std::max(0.0f, std::abs(std::nan(""))) is 0.0f under this toolchain.
//
// That is not a theoretical hazard here. THE MOST LIKELY S7 BUG IS DIVIDING BY
// laplacian_scale(0,0), WHICH IS EXACTLY ZERO, and the NaN that produces spreads
// to every pixel through idft2d. Without this helper the headline gate is blind
// to the headline failure.
bool all_finite(const vc::vc_image& img) {
    for (vc::image_dim y = 0; y < img.height(); ++y) {
        for (vc::image_dim x = 0; x < img.width(); ++x) {
            for (vc::channel_count c = 0; c < img.channels(); ++c) {
                if (!std::isfinite(img.at<vc::buf_f32>(x, y, c))) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Worst absolute difference. Returns infinity on a shape mismatch rather than
// reading out of bounds off the smaller image.
float max_abs_diff(const vc::vc_image& a, const vc::vc_image& b) {
    if (!same_shape(a, b)) {
        return std::numeric_limits<float>::infinity();
    }
    float worst = 0.0F;
    for (vc::image_dim y = 0; y < a.height(); ++y) {
        for (vc::image_dim x = 0; x < a.width(); ++x) {
            for (vc::channel_count c = 0; c < a.channels(); ++c) {
                worst = std::max(
                    worst, std::abs(pixel(a, x, y, c) - pixel(b, x, y, c)));
            }
        }
    }
    return worst;
}

bool exactly_equal(const vc::vc_image& a, const vc::vc_image& b) {
    if (!same_shape(a, b)) {
        return false;
    }
    for (vc::image_dim y = 0; y < a.height(); ++y) {
        for (vc::image_dim x = 0; x < a.width(); ++x) {
            for (vc::channel_count c = 0; c < a.channels(); ++c) {
                if (pixel(a, x, y, c) != pixel(b, x, y, c)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Relative to the target's RMS, per P2 section F.3.
//
// all_finite() FIRST and as a conjunct, not as a separate assertion that could
// be forgotten at a call site -- see its comment for why a NaN would otherwise
// pass. rms_of() spans every channel, so a 3-channel comparison is not scaled
// by channel 0 alone.
bool recovers(const vc::vc_image& got, const vc::vc_image& want, float tol) {
    if (!all_finite(got)) {
        return false;
    }
    const float scale = std::max(rms_all_channels(want), 1e-12F);
    return max_abs_diff(got, want) <= tol * scale;
}

// Threw invalid_argument AND the message names `needle`.
//
// Needed because "it threw" is not evidence that the function being tested did
// the throwing. Found by mutation: deleting poisson_fft's f32 check left group
// 11 green, because at<buf_f32>() on a u8 buffer throws invalid_argument by
// itself, with the message
//
//     vc_pixel_buffer::as<T>(): requested dtype does not match stored dtype
//
// So the check was decorative -- it passed whether or not poisson_fft validated
// anything, and it would have accepted a function that threw halfway through its
// channel loop rather than up front. Naming the caller is what distinguishes an
// intentional precondition from an incidental accessor failure.
bool threw_naming(const std::function<void()>& f, const char* needle) {
    try {
        f();
    } catch (const vc::vc_exception& e) {
        return e.code() == vc::vc_error_code::invalid_argument &&
               std::string(e.what()).find(needle) != std::string::npos;
    } catch (...) {
        return false;
    }
    return false;
}

bool did_not_throw(const std::function<void()>& f) {
    try {
        f();
    } catch (...) {
        return false;
    }
    return true;
}

vc::math::grid2d extent_of(vc::math::grid_dim w, vc::math::grid_dim h) {
    return vc::math::grid2d::checked(static_cast<vc::element_count>(w) * h, w,
                                     h, "14_poisson");
}

constexpr float k_pi = std::numbers::pi_v<float>;

// The formula, written out independently of the implementation so the two can
// disagree. Used only by group 1's non-square half, where hand values would be
// tedious to list for every bin.
float scale_by_hand(int k, int l, int w, int h) {
    return -4.0F +
           2.0F * std::cos(2.0F * k_pi * static_cast<float>(k) /
                           static_cast<float>(w)) +
           2.0F * std::cos(2.0F * k_pi * static_cast<float>(l) /
                           static_cast<float>(h));
}

} // namespace

int main() {
    using vc::pixelops::laplacian;
    using vc::pixelops::laplacian_scale;
    using vc::pixelops::make_laplacian;
    using vc::pixelops::poisson_fft;
    using vc::pixelops::vc_edge_policy;

    constexpr float k_tol = 1e-5F;

    try {
        // ---- 1. laplacian_scale against hand values -------------------------
        {
            const auto e4 = extent_of(4, 4);

            // scale(0,0) = -4 + 2cos(0) + 2cos(0) = -4 + 2 + 2 = 0.
            // EXACT, not approximate: cos(0.0F) is exactly 1.0F, so this is the
            // one value that may be asserted bit for bit -- and it is the one
            // the solver must special-case, so it is worth pinning hard.
            expect(laplacian_scale(0, 0, e4) == 0.0F,
                   "4x4: scale(0,0) is EXACTLY zero");

            struct {
                vc::math::grid_dim k;
                vc::math::grid_dim l;
                float want;
                const char* what;
            } const cases[] = {
                {1, 0, -2.0F, "4x4: scale(1,0) == -2"},
                {0, 1, -2.0F, "4x4: scale(0,1) == -2"},
                {2, 0, -4.0F, "4x4: scale(2,0) == -4"},
                {0, 2, -4.0F, "4x4: scale(0,2) == -4"},
                {1, 1, -4.0F, "4x4: scale(1,1) == -4"},
                {2, 2, -8.0F, "4x4: scale(2,2) == -8, the checkerboard"},
                {3, 0, -2.0F, "4x4: scale(3,0) == -2, mirror of (1,0)"},
            };
            for (const auto& c : cases) {
                expect(std::abs(laplacian_scale(c.k, c.l, e4) - c.want) < k_tol,
                       c.what);
            }

            // NON-SQUARE, and this is the half that earns its keep. On 8 wide
            // by 4 high the two axes divide by DIFFERENT numbers, so a swapped
            // width and height changes the answer. On a square grid it does
            // not, which is why a square-only test is worth very little here.
            const auto e84 = extent_of(8, 4);
            expect(std::abs(laplacian_scale(2, 0, e84) - (-2.0F)) < k_tol,
                   "8x4: scale(2,0) == -2 (quarter lap along x)");
            expect(std::abs(laplacian_scale(0, 2, e84) - (-4.0F)) < k_tol,
                   "8x4: scale(0,2) == -4 (half lap along y) -- differs from "
                   "scale(2,0), so a width/height swap is visible");
            expect(std::abs(laplacian_scale(4, 0, e84) - (-4.0F)) < k_tol,
                   "8x4: scale(4,0) == -4 (half lap along x)");

            bool all_match = true;
            for (vc::math::grid_dim l = 0; l < 4; ++l) {
                for (vc::math::grid_dim k = 0; k < 8; ++k) {
                    all_match =
                        all_match && std::abs(laplacian_scale(k, l, e84) -
                                              scale_by_hand(static_cast<int>(k),
                                                            static_cast<int>(l),
                                                            8, 4)) < k_tol;
                }
            }
            expect(all_match, "8x4: every bin matches the formula written out "
                              "independently in this file");
        }

        // ---- 2. the scale IS the stencil's own transform ---------------------
        //
        // The independent route, and the one that cannot be fooled by the
        // continuous -(u^2+v^2) trap. Place the stencil's nine weights on the
        // grid WRAPPED AROUND THE ORIGIN -- tap (kx,ky) goes to
        // ((kx % L) + L) % L -- transform, and compare.
        {
            const vc::math::grid_dim w = 8;
            const vc::math::grid_dim h = 4;
            const auto e = extent_of(w, h);

            const auto stencil = make_laplacian();
            vc::math::vc_complex_signal plane(e.count(),
                                              vc::math::vc_complex{0.0F, 0.0F});
            for (int ky = -1; ky <= 1; ++ky) {
                for (int kx = -1; kx <= 1; ++kx) {
                    const auto ix = static_cast<std::size_t>(
                        ((kx % static_cast<int>(w)) + static_cast<int>(w)) %
                        static_cast<int>(w));
                    const auto iy = static_cast<std::size_t>(
                        ((ky % static_cast<int>(h)) + static_cast<int>(h)) %
                        static_cast<int>(h));
                    plane[(iy * w) + ix] +=
                        vc::math::vc_complex{stencil.at(kx, ky), 0.0F};
                }
            }

            const vc::math::vc_complex_signal spectrum =
                vc::math::dft2d(plane, e);

            bool real_matches = true;
            bool imag_is_zero = true;
            for (vc::math::grid_dim l = 0; l < h; ++l) {
                for (vc::math::grid_dim k = 0; k < w; ++k) {
                    const auto& bin =
                        spectrum[(static_cast<std::size_t>(l) * w) + k];
                    real_matches =
                        real_matches &&
                        std::abs(bin.real() - laplacian_scale(k, l, e)) < 1e-4F;
                    imag_is_zero = imag_is_zero && std::abs(bin.imag()) < 1e-4F;
                }
            }
            expect(real_matches,
                   "the DFT of the actual stencil equals laplacian_scale at "
                   "EVERY bin -- which is what rules out the continuous "
                   "-(u^2+v^2) form, since that one agrees only at low "
                   "frequency");
            expect(imag_is_zero,
                   "the stencil's transform is REAL at every bin, because the "
                   "stencil is symmetric -- a response with a rotation in it "
                   "would mean the weights lost their mirror symmetry");
        }

        // ---- 3. the forward operator only SCALES a wave ----------------------
        //
        // The premise the whole solver rests on, checked rather than assumed.
        {
            // Every row is [1, 0, -1, 0]: one wave along x at k=1, flat in y.
            const auto wave = from_rule(4, 4, [](int x, int /*y*/) {
                constexpr float row[] = {1.0F, 0.0F, -1.0F, 0.0F};
                return row[x];
            });
            const auto got = laplacian(wave, vc_edge_policy::wrap);

            // Integers throughout, so this is an exact equality.
            bool scaled = true;
            for (vc::image_dim y = 0; y < 4; ++y) {
                for (vc::image_dim x = 0; x < 4; ++x) {
                    scaled =
                        scaled && pixel(got, x, y) == -2.0F * pixel(wave, x, y);
                }
            }
            expect(scaled,
                   "a single wave through laplacian(wrap) comes back as "
                   "THE SAME WAVE times one number, exactly");

            const auto e = extent_of(4, 4);
            expect(std::abs(laplacian_scale(1, 0, e) - (-2.0F)) < k_tol,
                   "and that number is laplacian_scale(1,0) -- the measured "
                   "scale and the formula agree");

            // The flat wave, which is the hole. Same shape of check.
            const auto flat = from_rule(4, 4, [](int, int) { return 7.0F; });
            const auto flat_out = laplacian(flat, vc_edge_policy::wrap);
            bool annihilated = true;
            for (vc::image_dim y = 0; y < 4; ++y) {
                for (vc::image_dim x = 0; x < 4; ++x) {
                    annihilated = annihilated && pixel(flat_out, x, y) == 0.0F;
                }
            }
            expect(annihilated,
                   "the flat wave is annihilated exactly -- scale(0,0) is 0, "
                   "which is why bin 0 cannot be divided");
        }

        // ---- 4. THE HAND CASE, computed on paper ----------------------------
        //
        // phi = [4, -1, -2, -1] on a 4x1 grid, wrap. Under wrap a height-1 image
        // reduces the 5-point stencil to the 1-D one, because the up and down
        // neighbours ARE the centre row: centre + centre - 2.centre = 0.
        //
        //     y[0] = phi1 - 2.phi0 + phi3 = -1 -  8 - 1 = -10
        //     y[1] = phi2 - 2.phi1 + phi0 = -2 +  2 + 4 =   4
        //     y[2] = phi3 - 2.phi2 + phi1 = -1 +  4 - 1 =   2
        //     y[3] = phi0 - 2.phi3 + phi2 =  4 +  2 - 2 =   4
        //
        // phi sums to zero, so it IS the zero-mean solution and the solver must
        // return it unchanged rather than some other member of the family.
        {
            const auto phi = make_image(4, 1, 1, {4.0F, -1.0F, -2.0F, -1.0F});
            const auto f = laplacian(phi, vc_edge_policy::wrap);

            const auto want_f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(exactly_equal(f, want_f),
                   "the hand case: laplacian([4,-1,-2,-1], wrap) is exactly "
                   "[-10,4,2,4] -- the only expectation in this file that was "
                   "computed without reference to any code here");

            expect(std::abs(mean_of(phi)) < 1e-6F,
                   "the hand case's phi has zero mean, so it is the member of "
                   "the family the solver is obliged to return");

            const auto got = poisson_fft(f);
            expect(got.width() == 4 && got.height() == 1 && got.channels() == 1,
                   "the hand case: geometry follows f");
            expect(got.pixels()->dtype() == vc::pixel_dtype::f32,
                   "the hand case: dtype follows f -- the header promises "
                   "geometry, channel count AND dtype, and nothing else here "
                   "checked the third");
            expect(recovers(got, phi, k_tol),
                   "the hand case: poisson_fft recovers [4,-1,-2,-1]");
        }

        // ---- 5. round trip, non-square, multi-frequency ---------------------
        {
            const auto phi = from_rule(8, 4, [](int x, int y) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                // A sum of waves, so the mean is zero by construction rather
                // than by subtraction -- every one of these sums to zero over
                // the grid, which is the circle-sum fact.
                return 3.0F * std::cos(2.0F * k_pi * fx / 8.0F) +
                       2.0F * std::cos(2.0F * k_pi * 2.0F * fx / 8.0F) +
                       1.5F * std::cos(2.0F * k_pi * fy / 4.0F);
            });
            expect(std::abs(mean_of(phi)) < 1e-5F * rms_of(phi),
                   "the 8x4 test field has zero mean by construction");

            // WRAP, and the header says why: a forward pass under any other
            // policy builds the f of a DIFFERENT problem, and the round trip
            // then misses at the border while looking right in the interior.
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            const auto got = poisson_fft(f);
            expect(recovers(got, phi, k_tol),
                   "8x4 round trip: poisson_fft(laplacian(phi, wrap)) == phi");
        }

        // ---- 6. THE LEVEL IS UNRECOVERABLE ----------------------------------
        {
            const auto phi = make_image(4, 1, 1, {4.0F, -1.0F, -2.0F, -1.0F});
            const auto lifted =
                make_image(4, 1, 1, {11.0F, 6.0F, 5.0F, 6.0F}); // phi + 7

            const auto f_phi = laplacian(phi, vc_edge_policy::wrap);
            const auto f_lifted = laplacian(lifted, vc_edge_policy::wrap);

            // 7 and not 10: the first version of this demonstration used 10,
            // and f[0] happens to be -10, which invited the reading "we add the
            // negative of f's first element". There is no such relation. Any
            // constant whatever leaves f untouched.
            expect(exactly_equal(f_phi, f_lifted),
                   "adding 7 to every pixel leaves f BIT FOR BIT identical -- "
                   "the weights sum to zero, so the constant cancels");

            const auto got = poisson_fft(f_lifted);
            expect(recovers(got, phi, k_tol),
                   "solving that f returns the ZERO-MEAN member [4,-1,-2,-1], "
                   "not the [11,6,5,6] it was actually built from -- the level "
                   "was destroyed and is chosen, not recovered");
        }

        // ---- 7. zero-mean precondition --------------------------------------
        //
        // VACUOUS AGAINST A STUB on its own; the acceptance half below is what
        // makes it mean something.
        {
            const auto bad = make_image(4, 1, 1, {1.0F, 1.0F, 1.0F, 1.0F});
            expect(threw_naming([&] { (void)poisson_fft(bad); }, "poisson_fft"),
                   "an f with non-zero mean is rejected BY poisson_fft -- no "
                   "image has such an f, since the forward operator under wrap "
                   "always outputs zero mean");

            const auto good = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(did_not_throw([&] { (void)poisson_fft(good); }),
                   "ACCEPTANCE HALF: a zero-mean f of the same shape is "
                   "accepted -- without this the rejection above passes "
                   "against any stub that throws");
        }

        // ---- 8. the answer has zero mean ------------------------------------
        {
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            const auto got = poisson_fft(f);
            expect(std::abs(mean_of(got)) <=
                       k_tol * std::max(rms_of(got), 1e-12F),
                   "the returned image has zero mean, which is the choice the "
                   "header commits to");
        }

        // ---- 9. non-square both ways ----------------------------------------
        {
            const auto rule = [](int x, int y) {
                return std::cos(2.0F * k_pi * static_cast<float>(x) / 8.0F) +
                       0.5F *
                           std::cos(2.0F * k_pi * static_cast<float>(y) / 4.0F);
            };

            const auto wide = from_rule(8, 4, rule);
            const auto tall =
                from_rule(4, 8, [&](int x, int y) { return rule(y, x); });

            expect(recovers(poisson_fft(laplacian(wide, vc_edge_policy::wrap)),
                            wide, k_tol),
                   "8 wide by 4 high round trip");
            expect(recovers(poisson_fft(laplacian(tall, vc_edge_policy::wrap)),
                            tall, k_tol),
                   "4 wide by 8 high round trip -- the transpose, which a "
                   "swapped width/height would break while leaving the square "
                   "cases green");
        }

        // ---- 10. three channels, solved independently ------------------------
        {
            std::vector<float> values;
            const auto chan = [](int x, int y, int c) {
                return std::cos(2.0F * k_pi * static_cast<float>((c + 1) * x) /
                                8.0F) +
                       0.5F * static_cast<float>(c) *
                           std::cos(2.0F * k_pi * static_cast<float>(y) / 4.0F);
            };
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 8; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        values.push_back(chan(x, y, c));
                    }
                }
            }
            const auto rgb = make_image(8, 4, 3, values);
            const auto got = poisson_fft(laplacian(rgb, vc_edge_policy::wrap));

            expect(got.channels() == 3, "three channels in, three out");

            bool agrees = true;
            for (int c = 0; c < 3; ++c) {
                const auto single = from_rule(
                    8, 4, [&](int x, int y) { return chan(x, y, c); });
                const auto solo =
                    poisson_fft(laplacian(single, vc_edge_policy::wrap));
                for (vc::image_dim y = 0; y < 4; ++y) {
                    for (vc::image_dim x = 0; x < 8; ++x) {
                        agrees =
                            agrees &&
                            std::abs(pixel(got, x, y,
                                           static_cast<vc::channel_count>(c)) -
                                     pixel(solo, x, y)) <= 1e-4F;
                    }
                }
            }
            expect(agrees,
                   "each channel of a 3-channel solve matches its own "
                   "1-channel solve -- the buffer is INTERLEAVED, and this is "
                   "the check a planar index would fail while every "
                   "single-channel test above stayed green");
        }

        // ---- 11. non-f32 rejection -------------------------------------------
        //
        // VACUOUS AGAINST A STUB on its own. Paired below.
        {
            vc::vc_image_writer u8{4, 1, 1, vc::buf_u8{0}};
            const auto img = std::move(u8).seal();
            expect(threw_naming([&] { (void)poisson_fft(img); }, "poisson_fft"),
                   "a non-f32 f is rejected BY poisson_fft -- the message must "
                   "name it, because at<buf_f32>() on a u8 buffer throws "
                   "invalid_argument on its own and would make this check pass "
                   "with no validation present at all");

            const auto f32 = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(did_not_throw([&] { (void)poisson_fft(f32); }),
                   "ACCEPTANCE HALF: the same shape in f32 is accepted");
        }

        // ---- 12. out-of-range bin -------------------------------------------
        //
        // VACUOUS AGAINST A STUB on its own. Paired below.
        {
            const auto e = extent_of(4, 4);
            expect(threw_naming([&] { (void)laplacian_scale(4, 0, e); },
                                "laplacian_scale"),
                   "laplacian_scale rejects k == width, and says so by name");
            expect(threw_naming([&] { (void)laplacian_scale(0, 4, e); },
                                "laplacian_scale"),
                   "laplacian_scale rejects l == height, and says so by name");
            expect(did_not_throw([&] { (void)laplacian_scale(3, 3, e); }),
                   "ACCEPTANCE HALF: the last valid bin is accepted");
        }

        // ---- 13. 1x1, the floor ---------------------------------------------
        {
            const auto one = make_image(1, 1, 1, {0.0F});
            const auto e = extent_of(1, 1);
            expect(laplacian_scale(0, 0, e) == 0.0F,
                   "1x1: the only bin is bin 0, and its scale is zero");

            const auto got = poisson_fft(one);
            expect(got.width() == 1 && got.height() == 1,
                   "1x1: geometry follows f");
            expect(std::abs(pixel(got, 0, 0)) < 1e-6F,
                   "1x1: the answer is zero -- the only information a 1x1 f "
                   "could carry is its level, and the level is exactly what is "
                   "not recoverable");
        }

        // ---- 14. THE OTHER DIRECTION, on an f no image produced ------------
        //
        // Every gate above runs poisson_fft(laplacian(phi)) and compares against
        // phi. That shape of test has a blind spot the plan already names: f is
        // manufactured by the very operator being inverted, so a whole-stencil
        // error cancels inside it. It also only ever feeds the solver an f that
        // IS in the range of the forward operator.
        //
        // This runs the composition the other way -- laplacian(poisson_fft(f))
        // against f -- on an f built by hand. A dipole: +1 at one pixel, -1 at
        // another, zero everywhere else. It sums to exactly zero, so it is
        // admissible, and nothing about it came from laplacian().
        {
            std::vector<float> v(8 * 4, 0.0F);
            v[(1 * 8) + 2] = 1.0F;
            v[(3 * 8) + 5] = -1.0F;
            const auto f = make_image(8, 4, 1, v);
            expect(mean_of(f) == 0.0F,
                   "the dipole f sums to exactly zero, so it is admissible");

            const auto phi = poisson_fft(f);
            expect(all_finite(phi),
                   "the dipole solve produces no NaN or infinity -- the "
                   "specific thing that happens if bin 0 is DIVIDED by its "
                   "zero scale instead of being set");

            const auto back = laplacian(phi, vc_edge_policy::wrap);
            expect(recovers(back, f, k_tol),
                   "laplacian(poisson_fft(f), wrap) == f for an f that no "
                   "image produced -- the gate that does NOT cancel a "
                   "whole-stencil error, because f was not built by laplacian");
        }

        // ---- 15. a size where the near-DC scales are genuinely small --------
        //
        // Every grid above is 8x4 or smaller, where the only scales in play are
        // 0, -2, -4 and -8. That hides the solver's hardest numerical case.
        //
        // At 64 wide the bin next to DC has scale
        //     -4 + 2cos(2.pi/64) + 2cos(0)  =  about -0.0096
        // so dividing by it AMPLIFIES that bin by roughly 100x, and with it any
        // float error already in F. Small grids never exercise that, and it is
        // where a tolerance earns or loses its justification.
        {
            const auto e = extent_of(64, 32);
            const float near_dc = laplacian_scale(1, 0, e);
            expect(near_dc < 0.0F && std::abs(near_dc) < 0.011F &&
                       std::abs(near_dc) > 0.009F,
                   "64x32: the bin beside DC has a scale of about -0.0096, so "
                   "dividing by it amplifies about 100x");

            const auto phi = from_rule(64, 32, [](int x, int y) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                // Deliberately includes k=1, the bin with the tiny scale.
                return 1.0F * std::cos(2.0F * k_pi * fx / 64.0F) +
                       0.5F * std::cos(2.0F * k_pi * 7.0F * fx / 64.0F) +
                       0.75F * std::cos(2.0F * k_pi * 3.0F * fy / 32.0F);
            });
            const auto got = poisson_fft(laplacian(phi, vc_edge_policy::wrap));
            expect(all_finite(got), "64x32: the answer is finite");
            expect(recovers(got, phi, k_tol),
                   "64x32 round trip, including the bin whose scale is ~0.01 "
                   "-- THIS IS THE CASE THE TOLERANCE CONSTANTS SHOULD BE "
                   "MEASURED AGAINST, not the 4x1 one");
        }

        // ---- 16. THE CLAMP TRAP, AND THE ZERO-MEAN CHECK DOES NOT CATCH IT --
        //
        // The plan and the header both say "build f with wrap". This group is
        // why that warning is load-bearing rather than tidy-minded, and the
        // numbers are measured, not reasoned.
        //
        // The natural assumption is that the zero-mean precondition acts as a
        // guard. IT DOES NOT. Measured with laplacian() on a 5x4 field:
        //
        //     wrap     sum of output = 0
        //     clamp    sum of output = 0        <-- also zero
        //     zero     sum of output = -206.5
        //     reflect  sum of output = -19.5
        //
        // clamp sums to zero too, and not only on a constant -- laplacian.txt
        // 8b covers the constant case, this is a general field. So a clamp-built
        // f sails through the check.
        //
        // What it then returns, on the 8x4 field below: worst recovery error
        // 0.66 RELATIVE TO phi's RMS. Two thirds wrong, with no exception and
        // no NaN. The wrap-built f on the same field recovers to 5.6e-07.
        //
        // This is pinned as a test rather than left as prose so that it is
        // executable: if a later change ever makes poisson_fft tolerant of a
        // clamp-built f, this fails and says so.
        {
            const auto phi = from_rule(8, 4, [](int x, int y) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                return 3.0F * std::cos(2.0F * k_pi * fx / 8.0F) +
                       2.0F * std::cos(2.0F * k_pi * 2.0F * fx / 8.0F) +
                       1.5F * std::cos(2.0F * k_pi * fy / 4.0F);
            });
            const auto f_clamp = laplacian(phi, vc_edge_policy::clamp);

            expect(did_not_throw([&] { (void)poisson_fft(f_clamp); }),
                   "a clamp-built f is ACCEPTED -- it sums to zero, so the "
                   "precondition has nothing to object to");

            const auto got = poisson_fft(f_clamp);
            expect(all_finite(got),
                   "and the answer it returns is finite -- no NaN, no "
                   "exception, nothing to notice");
            expect(!recovers(got, phi, 0.1F),
                   "but it is WRONG by far more than 10% of phi's RMS "
                   "(measured 66%). The zero-mean check is NOT a guard against "
                   "the wrong boundary policy; only building f with wrap is");

            // And the control, so the group cannot pass by the solver simply
            // being broken for everything.
            const auto f_wrap = laplacian(phi, vc_edge_policy::wrap);
            expect(recovers(poisson_fft(f_wrap), phi, k_tol),
                   "CONTROL: the same phi through wrap recovers exactly, so "
                   "the failure above is the policy and not the solver");
        }

        // ---- 17. AN ALL-ZERO f, WHICH IS THE RMS-FLOOR PATH --------------
        //
        // Legal: zeros have zero mean, and the answer is zeros. Worth its own
        // group because of HOW the zero-mean check is likely to be written. A
        // relative threshold scaled by f's own RMS collapses to zero here, and
        // whether that reads as "accept" or "reject" depends entirely on whether
        // a floor was put under it.
        //
        // This is not hypothetical. vc_conv_frequency.cpp records a live false
        // positive of exactly this shape: a sum-to-zero kernel on a flat region
        // drove the output RMS to 0, the floor took over, and the threshold
        // collapsed below float noise. The Laplacian IS a sum-to-zero kernel.
        {
            const auto zeros =
                make_image(4, 4, 1, std::vector<float>(16, 0.0F));
            expect(did_not_throw([&] { (void)poisson_fft(zeros); }),
                   "an all-zero f is accepted -- its mean IS zero, and a "
                   "threshold that collapses to exactly 0 must not reject it");

            const auto got = poisson_fft(zeros);
            expect(all_finite(got), "an all-zero f gives a finite answer, not "
                                    "0/0 at every bin");
            expect(max_abs_diff(got, zeros) == 0.0F,
                   "and that answer is exactly zero");
        }

        // ---- 18. THE HAND CASE TRANSPOSED: 1 wide, 4 high -------------------
        //
        // Group 4 ran [4,-1,-2,-1] as a 4x1 ROW. The same values as a 1x4 COLUMN
        // must give the same f, because the stencil is its own transpose and the
        // degenerate axis contributes nothing either way.
        //
        // The degenerate extreme is where a width/height swap is most likely to
        // survive: group 9's 8x4 and 4x8 both have two real axes, so a swap
        // changes the arithmetic; here one axis is length 1, and a swap turns a
        // 1x4 into a 4x1 WITHOUT changing the sample count. That is precisely
        // the shape of mistake grid2d cannot catch, since it only certifies
        // width x height == count.
        {
            const auto phi = make_image(1, 4, 1, {4.0F, -1.0F, -2.0F, -1.0F});
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            const auto want = make_image(1, 4, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(
                exactly_equal(f, want),
                "1x4 column: laplacian gives the same [-10,4,2,4] as the 4x1 "
                "row, because the stencil is its own transpose");

            const auto got = poisson_fft(f);
            expect(got.width() == 1 && got.height() == 4,
                   "1x4 column: geometry is 1 wide and 4 high, NOT the "
                   "transpose -- a swap here keeps the element count and so "
                   "slips past grid2d entirely");
            expect(recovers(got, phi, k_tol),
                   "1x4 column: recovers [4,-1,-2,-1]");
        }

        // ---- 19. ZERO MEAN IS PER CHANNEL, NOT OVER THE WHOLE IMAGE --------
        //
        // An ambiguity in the header, caught by writing a reference
        // implementation against it: "f must have zero mean" does not say
        // whether that is measured per channel or over the whole buffer. It has
        // to be PER CHANNEL, because each channel is solved independently and
        // each one's bin 0 is set to zero on its own.
        //
        // The two readings differ on exactly this input: channel 0 has mean +5,
        // channel 1 has mean -5, and the whole-image mean is 0. A whole-image
        // check accepts it and returns a quietly wrong answer for BOTH channels.
        // The first reference implementation written against this header made
        // precisely that mistake and passed every other group.
        {
            const auto f = make_image(2, 1, 2,
                                      // (x=0): ch0=+5, ch1=-5
                                      // (x=1): ch0=+5, ch1=-5
                                      {5.0F, -5.0F, 5.0F, -5.0F});
            expect(
                threw_naming([&] { (void)poisson_fft(f); }, "poisson_fft"),
                "f is rejected when a CHANNEL has non-zero mean, even though "
                "the whole-image mean is exactly zero -- channels are solved "
                "independently, so the precondition is per channel");

            // Control: per-channel zero mean, whole-image zero mean. Accepted.
            const auto ok = make_image(2, 1, 2, {5.0F, -5.0F, -5.0F, 5.0F});
            expect(did_not_throw([&] { (void)poisson_fft(ok); }),
                   "ACCEPTANCE HALF: both channels individually zero-mean is "
                   "accepted");
        }

        // ---- 20. THE ZERO-MEAN THRESHOLD'S SCALING, NOT JUST ITS EXISTENCE --
        //
        // Every other rejection check feeds a GROSS violation -- a mean of 1 or
        // 5 against a tolerance near 1e-5 -- so they pass for any threshold
        // within many orders of magnitude of right. They pin that a check
        // exists, not that it is calibrated.
        //
        // That hole was real: a first implementation compared the mean against
        // tol x RMS x COUNT rather than tol x RMS, which is `count` times too
        // loose -- 16x here, 2048x at 64x32, where it would have accepted a 2%
        // DC offset. All 59 other checks were green.
        //
        // This group feeds a mean/RMS of 1e-4, ten times the tolerance and well
        // inside the count-scaled one. The correct threshold rejects it; the
        // count-scaled threshold (1e-5 x 1 x 16 = 1.6e-4) accepts it.
        {
            const auto field = [](float offset) {
                std::vector<float> v;
                v.reserve(16);
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        v.push_back((((x + y) % 2) == 0 ? 1.0F : -1.0F) +
                                    offset);
                    }
                }
                return make_image(4, 4, 1, v);
            };

            // mean/RMS is 1e-4: ten times k_mean_rel_tol, and BELOW the
            // count-scaled threshold of 1.6e-4.
            expect(
                threw_naming([&] { (void)poisson_fft(field(1e-4F)); },
                             "poisson_fft"),
                "a mean of 1e-4 against an RMS of 1 is rejected -- ten times "
                "the tolerance, but a threshold wrongly scaled by the sample "
                "count would accept it");

            // Comfortably inside the tolerance. Must still be accepted.
            expect(did_not_throw([&] { (void)poisson_fft(field(1e-7F)); }),
                   "ACCEPTANCE HALF: a mean of 1e-7 against the same RMS is "
                   "accepted, so the check above is calibration and not just "
                   "blanket rejection");
        }

        // ---- 21. THE RESIDUE THRESHOLD MUST NOT DEPEND ON AMPLITUDE -------
        //
        // The imaginary-residue check was once scaled against the INPUT's RMS,
        // copied from vc_conv_frequency.cpp. That reasoning does not transfer:
        // convolve_frequency's output can COLLAPSE, so it must not scale by the
        // output; poisson_fft's output can be ~1660x LARGER than its input near
        // DC, and the residue tracks the OUTPUT. Measured at 64x32, varying only
        // the amplitude of the one-cycle component:
        //
        //     amp     residue / INPUT rms     residue / OUTPUT rms
        //       1     5.2e-05                 1.30e-05
        //      10     5.0e-04                 1.75e-05
        //     100     1.7e-03                 1.75e-05
        //     500     1.8e-03                 1.75e-05
        //
        // THE OUTPUT-SCALED RATIO IS FLAT; THE INPUT-SCALED ONE GROWS. So the
        // property to pin is INVARIANCE, not one magic amplitude -- an earlier
        // version of this group used 10.0, and widening the tolerance from 1e-4
        // to 1e-3 silently disarmed it without changing a line of the test.
        //
        // Sweeping amplitudes catches any threshold whose behaviour depends on
        // signal level, whatever the tolerance happens to be.
        {
            for (const float amp : {1.0F, 10.0F, 100.0F}) {
                const auto phi = from_rule(64, 32, [amp](int x, int y) {
                    const auto fx = static_cast<float>(x);
                    const auto fy = static_cast<float>(y);
                    return (amp * std::cos(2.0F * k_pi * fx / 64.0F)) +
                           (0.5F * std::cos(2.0F * k_pi * 7.0F * fx / 64.0F)) +
                           (0.75F * std::cos(2.0F * k_pi * 3.0F * fy / 32.0F));
                });
                const auto f = laplacian(phi, vc_edge_policy::wrap);

                const std::string what =
                    "one-cycle amplitude " + std::to_string(amp) +
                    ": a valid f is solved and recovered, whatever its level "
                    "-- an input-scaled residue threshold throws here";
                bool ok = did_not_throw([&] { (void)poisson_fft(f); });
                if (ok) {
                    const auto got = poisson_fft(f);
                    ok = all_finite(got) && recovers(got, phi, k_tol);
                }
                expect(ok, what.c_str());
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "14_poisson: " << passed << " / " << total_checks
                  << " checks passed before the exception\n";
        std::cout << "14_poisson: FAIL\n";
        return 1;
    }

    std::cout << "14_poisson: " << passed << " / " << total_checks
              << " checks passed\n";
    if (passed != total_checks) {
        std::cout << "14_poisson: FAIL\n";
        return 1;
    }
    std::cout << "14_poisson: OK\n";
    return 0;
}
