// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 15_poisson_iter -- acceptance checks for poisson_iter: solving grad^2 phi = f
// by sweeping instead of by transforming.
//
// P2 section C's S8a. Reasoning lives in include/vc/pixelops/vc_poisson.h and
// curriculum/notes/poisson.txt; this file only states what must be true.
//
// ---- READ THIS BEFORE TRUSTING A GREEN RUN ----
//
// GROUPS 11, 12, 13 AND 14 PASS VACUOUSLY AGAINST THE UNIMPLEMENTED STUB, which
// throws unconditionally. Every one of them is PAIRED with an acceptance check
// that the stub fails immediately. Do not delete the acceptance halves. This is
// how 11_conv_theorem's first draft shipped four rejection checks that tested
// nothing.
//
// ---- WHICH GROUPS ARE PAPER-ANCHORED ----
//
// GROUPS 1, 3, 4, 5 AND 6 were computed ON PAPER with no reference to any code
// in this repository, and they are the anchors. Everything else compares
// poisson_iter against laplacian() or poisson_fft, so a misunderstanding shared
// between them would pass. If an anchor fails and nothing else does, suspect the
// paper. If everything else fails and the anchors pass, suspect the solver.
//
// ---- WHAT EACH GROUP CATCHES, AND WHAT IT IS BLIND TO ----
//
// 1   ONE JACOBI SWEEP FROM ZERO IS EXACTLY -f/4. All four neighbours are still
//     zero, so the update reduces to that, at every grid size and both
//     dimensions. No convergence involved, so it isolates the update rule from
//     everything else: it catches a sign error on f, a wrong divisor, and a
//     non-zero initial guess in one exact equality.
//     BLIND TO: the neighbour terms entirely -- they are all zero here.
//
// 2   residual[0] == 1.0 exactly, for any non-zero f, both sweeps. Free, exact,
//     and it pins the INDEXING convention: if residual[0] were the value after
//     the first sweep it would not be 1.0.
//     Paired with the all-zero f, where it must be 0.0 and not NaN.
//
// 3   THE HAND CASE. phi = [4,-1,-2,-1] on a 4x1 grid under wrap gives exactly
//     f = [-10,4,2,4] -- the same arithmetic 14_poisson group 4 uses -- and both
//     sweeps must converge back to it.
//
// 4a/4b JACOBI AND GAUSS-SEIDEL MUST DIFFER AFTER ONE SWEEP, with the
//     Gauss-Seidel values worked out by hand. This is the group that catches an
//     implementation where `gauss_seidel` quietly runs Jacobi: EVERY CONVERGENCE
//     TEST IN THIS FILE WOULD STILL PASS, because both converge to the same
//     answer. Only the path differs, so only a check on the path can see it.
//     The x=3 value reads a freshly-written phi[0] through the WRAP SEAM, which
//     is where a naive "left and up are stale at the border" reading diverges
//     from the contract.
//     BLIND TO THE y DIRECTION -- it is a 4x1 grid, one row, so ascending and
//     descending y are the same scan. Hence 4c.
//
// 4c  THE SWEEP DIRECTION ON A GENUINELY 2-D GRID, 4x4, hand-worked. Reversing
//     the y scan passed all 53 checks before this group existed. It does NOT
//     constrain which axis is outer: both lexicographic-ascending nestings are
//     bit-identical for a 5-point stencil, verified by mutation.
//
// 5   THE JACOBI STALL, AS AN EXACT EQUALITY RATHER THAN A VAGUE FAILURE.
//     On a pure checkerboard phi at 4x4, f = -8.phi, and Jacobi oscillates
//     between 2.phi and 0 forever while the relative residual stays at exactly
//     1.0. Asserting "did not converge" instead would be satisfied by a stub
//     that does nothing; asserting the exact plateau is not.
//     The cause is factor(2,2) = [cos(pi) + cos(pi)]/2 = -1: the error flips
//     sign and never shrinks.
//     NOTHING IS ASSERTED HERE ABOUT GAUSS-SEIDEL. Its iteration is not
//     symmetric, the argument does not transfer, and it has not been verified.
//
// 6   THE ONE-ROW GRID HAS NO STALL, AND THIS IS THE 1-D/2-D DISCRIMINATOR.
//     On a 4x1 grid the alternating field is mode (2,0), whose factor is
//     [cos(pi) + cos(0)]/2 = 0 -- it is solved EXACTLY IN ONE SWEEP. An
//     implementation that used the 1-D update (L+R-f)/2 would have factor
//     cos(pi) = -1 and stall forever on it. There is no height/2 bin on one row,
//     so the joint checkerboard mode does not exist there.
//     P2's plan row asserted the 1-D factor until 2026-10-15; this group is what
//     keeps the two apart.
//
// 7   CROSS-VALIDATION against poisson_fft -- two unrelated methods, same
//     answer. This is S8b's Gate 1, seeded early.
//     ** BOTH SIDES ARE ZERO-MEANED FIRST, and that is not cosmetic. ** The
//     residual is blind to a constant offset (group 8), so a run can converge
//     perfectly and sit a constant away. Comparing raw would fail a correct
//     solver.
//     ** RUN SMALL, 16x8. ** The slowest mode's factor is [cos(2pi/16)+1]/2 =
//     0.962, needing ~400 sweeps for 1e-7; at width 64 it is 0.9976 and needs
//     ~4,800. The sweep budget below is derived from that, not guessed.
//
// 8   THE RESIDUAL IS BLIND TO A CONSTANT OFFSET, demonstrated directly through
//     laplacian() rather than asserted. This is the justification for group 7's
//     normalisation, and it is checked independently of poisson_iter so it
//     cannot be explained away by a solver bug.
//
// 9   MONOTONICITY, asserted only for Jacobi and only above a floor.
//     Jacobi's every mode is multiplied by a factor of magnitude <= 1 each
//     sweep, so the residual provably cannot rise -- but float jitter near the
//     noise floor can, so the assertion stops there.
//     GAUSS-SEIDEL IS NOT ASSERTED PER STEP: its iteration matrix is not
//     symmetric and per-step monotonicity in this norm is unproven. It is only
//     required to end far below where it started.
//
// 10a THREE CHANNELS, agreeing with three separate one-channel solves. Catches
//     an interleaved/planar index swap -- invisible at one channel, the bug
//     that survived every one-channel test in conv_theorem -- and a mean
//     subtracted across the whole image rather than per channel.
//
// 10b THE RESIDUAL AGGREGATION RULE, pinned sweep by sweep against the
//     per-sweep maximum of single-channel runs. The field is chosen so the
//     channel with the TINY f is also the SLOWEST, which is what makes a
//     combined RMS visibly wrong: at sweep 12 it reads 2.6e-04 where the worst
//     channel reads 1.5e-01.
//     AN EARLIER VERSION ASSERTED residual[0] == 1.0 AND CLAIMED THAT CAUGHT A
//     COMBINED RMS. It does not -- a combined ratio is also 1.0 at sweep zero,
//     so the check was satisfied by both and its comment was simply wrong.
//
// 10c converged reflects the WORST channel, not the first one to finish.
//
// 10d one bad channel out of three is rejected. The precondition is PER
//     CHANNEL; a whole-buffer mean accepts channels of +5 and -5. 14_poisson
//     group 19's bug class, one function over.
//
// 10e THE EQUATION ITSELF, asserted directly: laplacian(phi, wrap) == f. Every
//     other convergence group compares phi against a known phi or against
//     poisson_fft, so all of them inherit whatever those share. This one
//     depends on neither, and in particular does not trust the reported
//     residual to have been computed correctly.
//
// 10f A 1x1 IMAGE. Under wrap every neighbour is the pixel itself, so the
//     stencil is identically zero, the only admissible f is zero, and the
//     answer is zero. The size class 11_conv_theorem found bugs at.
//
// 11  non-f32 rejection. PAIRED.
// 12  non-zero-mean rejection, with a hand-built f, since f = laplacian(phi)
//     satisfies the precondition by construction. PAIRED.
//     Also asserts the message names poisson_iter and NOT poisson_fft, which a
//     copy-paste of the shared helper's caller string would fail.
// 13  max_sweeps == 0 rejection. PAIRED.
// 14  tolerance rejection: negative AND NaN. NaN needs !(tol >= 0); a naive
//     `tol < 0` compares false against NaN and lets it through, after which
//     every convergence comparison is false and the run silently burns the full
//     budget. PAIRED.
//
// 15  NON-SQUARE BOTH WAYS, 8x4 and 4x8. On a square grid a swapped width and
//     height is invisible -- the blindness 07_dft2d and 14_poisson group 1 both
//     record.
//
// 16  THE f-BUILT-WITH-CLAMP TRAP, inherited from 14_poisson group 16 and
//     re-pinned here because it is a property of the PROBLEM, not of either
//     solver. A clamp-built f passes the zero-mean check, is solved, and is
//     returned wrong, with no exception and no NaN. The only defence is building
//     f with wrap. This group exists so that stays visible.
//
// 17  converged == false when the budget is exhausted, and the run returns
//     normally rather than throwing. "Did not converge in the budget you set" is
//     information the caller asked for.
//
// ---- MUTATION RESULTS, 2026-10-16 ----
//
// A green suite is not evidence that the suite bites. Six mutations were run
// against a working implementation; each was caught, by the group that claims
// to catch it:
//
//     residual as a COMBINED RMS               -> 10a, 10b, 10c
//     gauss_seidel secretly running jacobi     -> 4a, 4b, 16a
//     the 1-D update (L+R-f)/2 on one row      -> 1a, 3b, 4a, 6b
//     no mean subtraction at all               -> 3b, 4a, 15, 16a  (GS only)
//     ONE mean across all channels             -> 4a, 10a, 10b     (GS only)
//     zero-mean checked on channel 0 only      -> 10d, AND NOTHING ELSE
//
// A SECOND ROUND, 2026-10-16, ran fifteen more. Fourteen were caught:
//
//     max_sweeps ignored, always one sweep     -> 2b, 3b, and 18 others
//     no initial entry in the history          -> 2a, 2b
//     converged never set                      -> 3b, 5c, 7b, ...
//     converged always true                    -> 5b, 10c, 17
//     non-f32 check deleted                    -> 11a ONLY
//     max_sweeps == 0 check deleted            -> 13a ONLY
//     `tol < 0` instead of `!(tol >= 0)`       -> 14b ONLY
//     initial guess a non-zero CONSTANT        -> 2c, 10f
//     initial guess non-constant               -> 1a, 1b, 2a
//     residual with no denominator             -> 2a, 5b, 10c
//     damped jacobi (would cure the stall)     -> 1a, 1b, 4a, 5b
//     clamp instead of wrap in the update      -> 3b, 4a, 5b, and more
//     x scanned DESCENDING                     -> 4a, 4c
//     y scanned DESCENDING                     -> 4c ONLY
//
// FIVE OF THOSE ARE CAUGHT BY EXACTLY ONE CHECK. Deleting 10d, 11a, 13a, 14b or
// 4c does not weaken the suite, it blinds it to one defect each.
//
// THE FIFTEENTH WAS NOT CAUGHT, AND SHOULD NOT BE: swapping the loop nesting to
// x-outer/y-inner passes everything, because for a 5-point stencil it is
// BIT-IDENTICAL to y-outer/x-inner. Both are lexicographic ascending and the
// stencil reaches one pixel, so the same neighbours are already written under
// either. That survivor is the reason the header no longer claims the nesting
// is part of the contract -- it was asserting a constraint that does not exist,
// and claiming it was tested when nothing could have tested it.
//
// y-DESCENDING SURVIVED THE FIRST ROUND ENTIRELY. 4a is a 4x1 grid, one row, so
// it cannot see the y direction at all; 4c exists because of that mutation.
//
// Two of those lines are worth reading twice.
//
// THE LAST ONE WAS CAUGHT BY A SINGLE CHECK, and 10d did not exist before this
// review. Without it, a solver that validated only the first channel passed
// every other check in the file.
//
// THE TWO "GS only" LINES ARE NOT A WEAKNESS. Jacobi provably preserves the
// mean -- summing its update over all pixels gives sum(phi) - sum(f)/4, and
// sum(f) is zero by the precondition -- so a missing or global mean subtraction
// genuinely IS a no-op for it. The mutations confirm the header's reasoning
// instead of contradicting it.

#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_types.h"
#include "vc/pixelops/vc_edge_policy.h"
#include "vc/pixelops/vc_laplacian.h"
#include "vc/pixelops/vc_poisson.h"

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

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

void expect(bool condition, const std::string& what) {
    expect(condition, what.c_str());
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

bool same_shape(const vc::vc_image& a, const vc::vc_image& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           a.channels() == b.channels();
}

// CHECKED SEPARATELY AND FIRST, because the comparison below cannot see a NaN:
// std::max(x, NaN) returns x, so an all-NaN image has a max_abs_diff of 0 and
// passes any tolerance. See 14_poisson's copy of this helper for the
// measurement. The relevant hazard here is a division by a zero denominator in
// the relative residual.
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

// Relative to the target's RMS, per P2 section F.3. all_finite() as a conjunct
// rather than a separate assertion a call site could forget.
bool recovers(const vc::vc_image& got, const vc::vc_image& want, float tol) {
    if (!all_finite(got)) {
        return false;
    }
    const float scale = std::max(rms_all_channels(want), 1e-12F);
    return max_abs_diff(got, want) <= tol * scale;
}

// Subtract each channel's own mean. Needed before ANY comparison between two
// solutions: the residual cannot see a constant offset, so two correct answers
// may legitimately differ by one.
vc::vc_image zero_meaned(const vc::vc_image& img) {
    vc::vc_image_writer out{img.width(), img.height(), img.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::channel_count c = 0; c < img.channels(); ++c) {
        const float m = mean_of(img, c);
        for (vc::image_dim y = 0; y < img.height(); ++y) {
            for (vc::image_dim x = 0; x < img.width(); ++x) {
                out.at<vc::buf_f32>(x, y, c) = pixel(img, x, y, c) - m;
            }
        }
    }
    return std::move(out).seal();
}

// Threw invalid_argument AND the message names `needle`. "It threw" is not
// evidence that the function under test did the throwing -- at<buf_f32>() on a
// u8 buffer throws invalid_argument by itself. See 14_poisson for the mutation
// that proved it.
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

constexpr float k_pi = std::numbers::pi_v<float>;

// The error-decay factor, written out independently of any implementation.
// Used only in comments and in group 7's sweep budget, so that the budget is
// derived rather than guessed.
float decay_factor(int k, int l, int w, int h) {
    const auto wx = 2.0F * k_pi * static_cast<float>(k) / static_cast<float>(w);
    const auto wy = 2.0F * k_pi * static_cast<float>(l) / static_cast<float>(h);
    return (std::cos(wx) + std::cos(wy)) / 2.0F;
}

} // namespace

int main() {
    using vc::pixelops::laplacian;
    using vc::pixelops::poisson_fft;
    using vc::pixelops::poisson_iter;
    using vc::pixelops::poisson_iter_report;
    using vc::pixelops::poisson_sweep;
    using vc::pixelops::vc_edge_policy;

    // ---- WHY THESE TWO NUMBERS, AND NOT THE 1e-7 A FIRST DRAFT USED ----
    //
    // f32 PUTS A FLOOR UNDER THE RESIDUAL that no number of sweeps gets below.
    // Computing laplacian(phi) rounds at roughly eps x 8 x |phi|, so the
    // smallest reachable relative residual is about eps x 8 x ||phi|| / ||f||.
    // Sized against a float reference before this file was handed over:
    //
    //     grid     jacobi floor   gauss-seidel floor
    //      4x1       6.9e-07          8.6e-07
    //      8x4       1.0e-07          1.8e-07
    //      8x8       1.3e-07          6.9e-08
    //     16x8       3.1e-07          2.2e-07
    //
    // A FIRST DRAFT OF THIS FILE ASKED FOR 1e-7 AND WOULD HAVE FAILED A CORRECT
    // SOLVER at 16x8 and 8x4, where the floor sits above it. 1e-6 clears every
    // floor above with 3x to 10x of room.
    //
    // k_phi_tol IS LOOSER THAN k_tol ON PURPOSE. A small residual is not a small
    // error: error ~ residual / |smallest scale|, which at 16x8 is about 0.152,
    // so a residual of 1e-6 permits a phi error near 7e-6. Measured at 6.3e-6,
    // which leaves only 1.6x under a 1e-5 gate -- too tight for a tripwire, by
    // the same argument vc_poisson.cpp uses for its own 1e-3. 5e-5 gives ~8x.
    //
    // k_exact_tol IS TIGHTER THAN BOTH, at 1e-6, and is used only where the
    // expected value was computed on paper and is exactly representable -- the
    // one-sweep checks, the stall plateau, the laplacian identities. Those do
    // not come through the residual gate, so no amplification applies and there
    // is nothing to leave room for beyond float rounding.
    //
    // ** THESE WERE SIZED AGAINST A REFERENCE, NOT AGAINST THIS REPOSITORY. **
    // The build is the arbiter; if a floor here is optimistic, loosen the
    // constant and record the measured value rather than adding sweeps.
    constexpr float k_exact_tol = 1e-6F;
    constexpr float k_phi_tol = 5e-5F;
    constexpr float k_res_tol = 1e-6F;
    const std::vector<poisson_sweep> both = {poisson_sweep::jacobi,
                                             poisson_sweep::gauss_seidel};
    const auto name = [](poisson_sweep s) {
        return s == poisson_sweep::jacobi ? "jacobi" : "gauss_seidel";
    };

    try {
        // ---- 1. one Jacobi sweep from zero is exactly -f/4 -----------------
        //
        // All four neighbours are zero on the first sweep, so the update is
        // (0 + 0 + 0 + 0 - f)/4. Hand values: f = [-10,4,2,4] gives
        // [2.5, -1, -0.5, -1], whose mean is already zero, so the mean
        // subtraction cannot disturb it.
        {
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            const auto want = make_image(4, 1, 1, {2.5F, -1.0F, -0.5F, -1.0F});
            const auto got = poisson_iter(f, poisson_sweep::jacobi, 1, 0.0F);
            expect(recovers(got.phi, want, k_exact_tol),
                   "1a: one jacobi sweep from zero is exactly -f/4 (4x1)");
        }
        {
            // Same claim on a genuinely 2-D grid, so it is not a 1-D accident.
            const auto phi = from_rule(8, 4, [](int x, int y) {
                return std::cos(2.0F * k_pi * static_cast<float>(x) / 8.0F) +
                       (0.5F *
                        std::cos(2.0F * k_pi * static_cast<float>(y) / 4.0F));
            });
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            const auto got = poisson_iter(f, poisson_sweep::jacobi, 1, 0.0F);

            vc::vc_image_writer want{8, 4, 1, vc::buf_f32{0.0F}};
            for (vc::image_dim y = 0; y < 4; ++y) {
                for (vc::image_dim x = 0; x < 8; ++x) {
                    want.at<vc::buf_f32>(x, y, 0) = -pixel(f, x, y) / 4.0F;
                }
            }
            expect(recovers(got.phi, zero_meaned(std::move(want).seal()),
                            k_exact_tol),
                   "1b: one jacobi sweep from zero is -f/4 on a 2-D grid too");
        }

        // ---- 2. residual[0] is the value BEFORE any sweep ------------------
        {
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            for (const auto s : both) {
                const auto got = poisson_iter(f, s, 3, 0.0F);
                expect(!got.residual.empty() &&
                           std::abs(got.residual[0] - 1.0F) <= k_exact_tol,
                       std::string("2a: ") + name(s) +
                           ": residual[0] is exactly 1.0 -- it is the value "
                           "BEFORE any sweep, from a zero start");
                expect(
                    got.residual.size() == 4,
                    std::string("2b: ") + name(s) +
                        ": residual has max_sweeps + 1 entries when it never "
                        "converges");
            }
        }
        {
            // The one exception. An all-zero f has no residual to report, and
            // phi = 0 already solves it -- the clamped denominator must give
            // 0.0 and not 0/0.
            const auto f = make_image(4, 2, 1, std::vector<float>(8, 0.0F));
            for (const auto s : both) {
                const auto got = poisson_iter(f, s, 5, 1e-6F);
                expect(!got.residual.empty() && got.residual[0] == 0.0F &&
                           got.converged && all_finite(got.phi) &&
                           rms_all_channels(got.phi) == 0.0F,
                       std::string("2c: ") + name(s) +
                           ": an all-zero f gives residual 0 (not NaN), "
                           "converges at once, and returns phi = 0");
            }
        }

        // ---- 3. THE HAND CASE ----------------------------------------------
        //
        // Worked on paper, independent of every function here. On a 4x1 grid
        // under wrap the up and down neighbours are the row itself, so the
        // 5-point stencil reduces to the 1-D one -- centre + centre - 2.centre
        // contributes nothing. The largest non-DC decay factor is 0.5, so a few
        // dozen sweeps is ample.
        {
            const auto phi = make_image(4, 1, 1, {4.0F, -1.0F, -2.0F, -1.0F});
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(
                recovers(laplacian(phi, vc_edge_policy::wrap), f, k_exact_tol),
                "3a: the paper's f -- laplacian([4,-1,-2,-1], wrap) is "
                "[-10,4,2,4]");
            for (const auto s : both) {
                const auto got = poisson_iter(f, s, 400, k_res_tol);
                expect(got.converged && recovers(got.phi, phi, k_phi_tol),
                       std::string("3b: ") + name(s) +
                           ": recovers the paper's phi = [4,-1,-2,-1]");
            }
        }

        // ---- 4. the two sweeps must take DIFFERENT paths --------------------
        //
        // Hand-worked Gauss-Seidel, one sweep, 4x1, f = [-10,4,2,4], in place,
        // ascending x, up and down being the pixel's own old value:
        //
        //   x=0  (0 + 0 + 0 + 0 + 10)/4            =  2.5
        //   x=1  (2.5 + 0 + 0 + 0 - 4)/4           = -0.375       uses NEW x=0
        //   x=2  (-0.375 + 0 + 0 + 0 - 2)/4        = -0.59375     uses NEW x=1
        //   x=3  (-0.59375 + 2.5 + 0 + 0 - 4)/4    = -0.5234375   uses NEW x=0
        //                                                         THROUGH WRAP
        //   mean 1.0078125/4 = 0.251953125, subtracted from all four.
        //
        // Jacobi's first sweep is -f/4 = [2.5,-1,-0.5,-1]. If gauss_seidel
        // quietly runs Jacobi, every convergence check in this file still
        // passes and only this group fails.
        {
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            const auto j = poisson_iter(f, poisson_sweep::jacobi, 1, 0.0F);
            const auto g =
                poisson_iter(f, poisson_sweep::gauss_seidel, 1, 0.0F);

            const auto want_gs = make_image(
                4, 1, 1,
                {2.248046875F, -0.626953125F, -0.845703125F, -0.775390625F});
            expect(recovers(g.phi, want_gs, k_exact_tol),
                   "4a: one gauss_seidel sweep matches the hand arithmetic, "
                   "including the fresh phi[0] read through the wrap seam");
            expect(max_abs_diff(j.phi, g.phi) > 0.1F,
                   "4b: jacobi and gauss_seidel take different paths -- they "
                   "must NOT agree after one sweep");
        }

        // ---- 4c. the sweep DIRECTION, on a genuinely 2-D grid --------------
        //
        // 4a CANNOT SEE THE y DIRECTION: it runs on 4x1, where there is one row
        // and descending y is the same as ascending y. A mutation that reversed
        // the y scan passed all 53 checks before this group existed.
        //
        // 4x4 under wrap, f zero except f(0,0) = 4 and f(0,2) = -4 (zero mean),
        // one Gauss-Seidel sweep from zero, ascending in both axes. Worked by
        // hand; every value is dyadic, so the expected array is exact in f32
        // before the mean subtraction, and the mean itself is -0.05084228515625.
        //
        //     (0,0)  (0 + 0 + 0 + 0 - 4)/4                = -1
        //     (1,0)  (0 + -1 + 0 + 0 - 0)/4               = -0.25
        //     (2,0)  (0 + -0.25 + 0 + 0)/4                = -0.0625
        //     (3,0)  (-1 + -0.0625 + 0 + 0)/4             = -0.265625
        //     ... and so on down the rows, the last row reading the FIRST row
        //     through the wrap seam in y.
        //
        // Reversing the y scan moves the answer by 0.40 at its worst pixel, so
        // this is not a marginal discriminator.
        //
        // ** WHAT IS NOT CONSTRAINED: WHICH AXIS IS OUTER. ** x-outer/y-inner
        // and y-outer/x-inner give BIT-IDENTICAL results for a 5-point stencil,
        // because both are lexicographic ascending and the stencil reaches only
        // one pixel: under either order a pixel's LEFT and UP neighbours have
        // been written and its RIGHT and DOWN have not, wrap included. Verified
        // by mutation -- swapping the loop nesting changes nothing and no check
        // fails, correctly. An earlier version of the header called the nesting
        // "part of the contract"; it is not, and there is nothing to test.
        {
            std::vector<float> fv(16, 0.0F);
            fv[0] = 4.0F;  // (x=0, y=0)
            fv[8] = -4.0F; // (x=0, y=2)
            const auto f = make_image(4, 4, 1, fv);
            const auto want = make_image(
                4, 4, 1,
                {-0.94915771484375F, -0.19915771484375F, -0.01165771484375F,
                 -0.21478271484375F, -0.19915771484375F, -0.07415771484375F,
                 0.00396728515625F, -0.08978271484375F, 0.98834228515625F,
                 0.25396728515625F, 0.08990478515625F, 0.25982666015625F,
                 0.03521728515625F, 0.03521728515625F, 0.04107666015625F,
                 0.03033447265625F});
            const auto got =
                poisson_iter(f, poisson_sweep::gauss_seidel, 1, 0.0F);
            expect(recovers(got.phi, want, k_exact_tol),
                   "4c: one gauss_seidel sweep on a 4x4 matches the hand "
                   "arithmetic -- pins ASCENDING in BOTH axes, which the 4x1 "
                   "case in 4a cannot see");
        }

        // ---- 5. the Jacobi stall, exactly ----------------------------------
        //
        // factor(2,2) at 4x4 is [cos(pi) + cos(pi)]/2 = -1, so the error flips
        // sign and holds its magnitude. f = laplacian(P, wrap) = -8.P, and the
        // iterate oscillates 0 -> 2P -> 0 -> 2P while the relative residual
        // stays at exactly 1.0 at every index. Asserting "did not converge"
        // instead would be satisfied by a function that does nothing.
        {
            const auto board = from_rule(4, 4, [](int x, int y) {
                return ((x + y) % 2 == 0) ? 1.0F : -1.0F;
            });
            const auto f = laplacian(board, vc_edge_policy::wrap);
            expect(recovers(f,
                            from_rule(4, 4,
                                      [](int x, int y) {
                                          return ((x + y) % 2 == 0) ? -8.0F
                                                                    : 8.0F;
                                      }),
                            k_exact_tol),
                   "5a: laplacian of a checkerboard is -8 times it");

            const auto got = poisson_iter(f, poisson_sweep::jacobi, 12, 1e-6F);
            bool flat = !got.residual.empty();
            for (const float r : got.residual) {
                flat = flat && std::abs(r - 1.0F) <= k_exact_tol;
            }
            expect(flat && !got.converged,
                   "5b: jacobi on a pure checkerboard holds the relative "
                   "residual at exactly 1.0 forever -- factor(2,2) is -1");
        }
        {
            // The pair: the same grid without the checkerboard converges, so 5b
            // is about that one mode and not about the grid or the solver.
            const auto phi = from_rule(4, 4, [](int x, int y) {
                return std::cos(2.0F * k_pi * static_cast<float>(x) / 4.0F) +
                       (0.5F *
                        std::cos(2.0F * k_pi * static_cast<float>(y) / 4.0F));
            });
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            const auto got =
                poisson_iter(f, poisson_sweep::jacobi, 400, k_res_tol);
            expect(got.converged &&
                       recovers(got.phi, zero_meaned(phi), k_phi_tol),
                   "5c: the SAME 4x4 grid without a checkerboard component "
                   "converges -- the stall is the mode, not the grid");
        }

        // ---- 6. one row cannot stall, and this separates 1-D from 2-D -------
        //
        // On 4x1 the alternating field is mode (2,0): factor
        // [cos(pi) + cos(0)]/2 = 0, so it is solved EXACTLY in one sweep. The
        // 1-D update (L+R-f)/2 would give factor cos(pi) = -1 and stall on it.
        // There is no height/2 bin on one row, so the joint mode does not exist.
        {
            const auto phi = make_image(4, 1, 1, {1.0F, -1.0F, 1.0F, -1.0F});
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            expect(recovers(f, make_image(4, 1, 1, {-4.0F, 4.0F, -4.0F, 4.0F}),
                            k_exact_tol),
                   "6a: on 4x1 the 2-D stencil gives laplacian(alternating) = "
                   "-4 times it, not -8");
            const auto got = poisson_iter(f, poisson_sweep::jacobi, 1, 0.0F);
            expect(recovers(got.phi, phi, k_exact_tol),
                   "6b: jacobi solves the alternating 4x1 field EXACTLY in one "
                   "sweep -- a 1-D update would stall on it forever");
        }

        // ---- 7. cross-validation against poisson_fft ------------------------
        //
        // 16x8, the sweep budget derived from the slowest mode rather than
        // guessed: factor(1,0) = [cos(2pi/16) + cos(0)]/2 = 0.962, so reaching
        // 1e-7 takes ln(1e-7)/ln(0.962) ~ 415 sweeps. 4000 is ten times that.
        // No (8,4) component, which would stall.
        {
            const float slowest = decay_factor(1, 0, 16, 8);
            expect(slowest > 0.95F && slowest < 0.97F,
                   "7a: the slowest decay factor at 16x8 is ~0.962, which is "
                   "what the sweep budget below is sized from");

            const auto phi = from_rule(16, 8, [](int x, int y) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                return std::cos(2.0F * k_pi * fx / 16.0F) +
                       (0.6F * std::cos(2.0F * k_pi * 3.0F * fx / 16.0F)) +
                       (0.8F * std::sin(2.0F * k_pi * fy / 8.0F)) +
                       (0.3F * std::cos(2.0F * k_pi * 2.0F * fy / 8.0F));
            });
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            const auto reference = zero_meaned(poisson_fft(f));

            for (const auto s : both) {
                const auto got = poisson_iter(f, s, 4000, k_res_tol);
                expect(got.converged &&
                           recovers(zero_meaned(got.phi), reference, k_phi_tol),
                       std::string("7b: ") + name(s) +
                           ": agrees with poisson_fft at 16x8, both sides "
                           "zero-meaned first");
                expect(
                    recovers(zero_meaned(got.phi), zero_meaned(phi), k_phi_tol),
                    std::string("7c: ") + name(s) +
                        ": and agrees with the phi that built f");
            }
        }

        // ---- 8. the residual cannot see a constant offset -------------------
        //
        // Checked through laplacian() alone, so it holds whatever poisson_iter
        // does. This is why group 7 zero-means both sides.
        {
            const auto phi = from_rule(8, 4, [](int x, int y) {
                return std::cos(2.0F * k_pi * static_cast<float>(x) / 8.0F) +
                       (0.5F *
                        std::sin(2.0F * k_pi * static_cast<float>(y) / 4.0F));
            });
            const auto shifted = from_rule(8, 4, [&phi](int x, int y) {
                return pixel(phi, static_cast<vc::image_dim>(x),
                             static_cast<vc::image_dim>(y)) +
                       37.0F;
            });
            // NOT k_exact_tol, AND THE REASON IS ITSELF WORTH KNOWING. The
            // cancellation is EXACT in arithmetic -- the weights sum to zero --
            // but in f32 the stencil subtracts numbers near c from each other,
            // so the surviving error is about eps x c / rms(laplacian).
            // Measured against a float reference on this field:
            //
            //     c =   37    relative difference 2.0e-06
            //     c = 1000    relative difference 1.8e-04
            //
            // The identity is exact and the ARITHMETIC is what degrades, which
            // is the practical argument for zero-meaning a field before
            // differencing it. 1e-5 leaves ~5x over the measured value at c=37;
            // a first draft asked for 1e-6 and failed a correct implementation.
            expect(recovers(laplacian(shifted, vc_edge_policy::wrap),
                            laplacian(phi, vc_edge_policy::wrap), 1e-5F),
                   "8: adding a constant leaves laplacian unchanged under wrap "
                   "to within f32 cancellation, so two correct answers may "
                   "differ by one and the residual cannot tell");
        }

        // ---- 9. monotonicity, Jacobi only, above the noise floor ------------
        {
            const auto phi = from_rule(8, 8, [](int x, int y) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                return std::cos(2.0F * k_pi * fx / 8.0F) +
                       (0.7F * std::sin(2.0F * k_pi * 3.0F * fy / 8.0F));
            });
            const auto f = laplacian(phi, vc_edge_policy::wrap);

            const auto j = poisson_iter(f, poisson_sweep::jacobi, 300, 0.0F);
            bool nonrising = true;
            for (std::size_t i = 1; i < j.residual.size(); ++i) {
                if (j.residual[i - 1] > 1e-6F) {
                    nonrising = nonrising &&
                                j.residual[i] <= j.residual[i - 1] * 1.000'01F;
                }
            }
            expect(
                nonrising,
                "9a: jacobi's residual never rises while above 1e-6 -- every "
                "mode is multiplied by a factor of magnitude <= 1");

            // Gauss-Seidel is NOT asserted per step. A float reference showed zero
            // rises on THIS field, so the weaker assertion below is not hiding a
            // known failure -- but its iteration matrix is not symmetric, the
            // proof that covers Jacobi does not transfer, and one field is not
            // evidence about every field. Observed is not proved.
            const auto g =
                poisson_iter(f, poisson_sweep::gauss_seidel, 300, 0.0F);
            expect(
                g.residual.back() < g.residual.front() * 1e-4F,
                "9b: gauss_seidel's residual ends at least 1e-4 below where "
                "it started (per-step monotonicity deliberately unasserted)");
        }

        // ---- 10. three channels, and the residual AGGREGATION RULE --------
        //
        // THE FIELD IS CHOSEN TO DISCRIMINATE, which an earlier version of this
        // group was not. Channel 1 carries a SMALL amplitude at the SLOWEST
        // mode (1,0), factor 0.854; channels 0 and 2 carry a LARGE amplitude at
        // a FAST mode (3,0), factor 0.146. So the channel with the tiny f is
        // also the one that converges last, and a combined RMS -- dominated by
        // the large channels -- reports progress the small channel has not made.
        //
        // Measured against a float reference on exactly this pairing, after 12
        // sweeps: worst-channel 1.5e-01, combined RMS 2.6e-04. THREE ORDERS
        // APART.
        //
        // The earlier version asserted residual[0] == 1.0 and claimed that
        // caught a combined RMS. IT DOES NOT: at sweep zero a combined ratio is
        // ||f_all||/||f_all|| = 1.0 as well, so the check was satisfied by both
        // and the comment above it was wrong.
        {
            const auto fast = from_rule(8, 4, [](int x, int /*y*/) {
                return std::cos(2.0F * k_pi * 3.0F * static_cast<float>(x) /
                                8.0F);
            });
            const auto slow = from_rule(8, 4, [](int x, int /*y*/) {
                return 0.01F *
                       std::cos(2.0F * k_pi * static_cast<float>(x) / 8.0F);
            });
            const auto f_fast = laplacian(fast, vc_edge_policy::wrap);
            const auto f_slow = laplacian(slow, vc_edge_policy::wrap);

            std::vector<float> interleaved;
            interleaved.reserve(8ULL * 4 * 3);
            for (vc::image_dim y = 0; y < 4; ++y) {
                for (vc::image_dim x = 0; x < 8; ++x) {
                    interleaved.push_back(pixel(f_fast, x, y));
                    interleaved.push_back(pixel(f_slow, x, y));
                    interleaved.push_back(pixel(f_fast, x, y));
                }
            }
            const auto f3 = make_image(8, 4, 3, interleaved);

            for (const auto s : both) {
                const auto got3 = poisson_iter(f3, s, 2000, k_res_tol);
                const auto one_fast = poisson_iter(f_fast, s, 2000, k_res_tol);
                const auto one_slow = poisson_iter(f_slow, s, 2000, k_res_tol);
                const float scale_fast = rms_all_channels(one_fast.phi);
                const float scale_slow = rms_all_channels(one_slow.phi);

                bool agree = got3.phi.channels() == 3;
                for (vc::image_dim y = 0; y < 4 && agree; ++y) {
                    for (vc::image_dim x = 0; x < 8 && agree; ++x) {
                        agree = agree &&
                                std::abs(pixel(got3.phi, x, y, 0) -
                                         pixel(one_fast.phi, x, y)) <=
                                    k_phi_tol * scale_fast &&
                                std::abs(pixel(got3.phi, x, y, 1) -
                                         pixel(one_slow.phi, x, y)) <=
                                    k_phi_tol * scale_slow &&
                                std::abs(pixel(got3.phi, x, y, 2) -
                                         pixel(one_fast.phi, x, y)) <=
                                    k_phi_tol * scale_fast;
                    }
                }
                expect(agree,
                       std::string("10a: ") + name(s) +
                           ": three channels match three separate one-channel "
                           "solves -- catches an interleaved/planar index swap "
                           "(invisible at one channel) and a mean subtracted "
                           "across the whole image rather than per channel");

                // THE AGGREGATION RULE ITSELF, pinned sweep by sweep. Tolerance
                // zero so nothing stops early, a fixed budget so the histories
                // line up, and the three-channel history must equal the
                // per-sweep MAXIMUM of the single-channel ones. A combined RMS
                // sits far below that maximum from sweep 2 onward.
                const auto h3 = poisson_iter(f3, s, 12, 0.0F);
                const auto hf = poisson_iter(f_fast, s, 12, 0.0F);
                const auto hs = poisson_iter(f_slow, s, 12, 0.0F);
                bool tracks = h3.residual.size() == 13 &&
                              hf.residual.size() == 13 &&
                              hs.residual.size() == 13;
                for (std::size_t i = 0; i < 13 && tracks; ++i) {
                    const float want = std::max(hf.residual[i], hs.residual[i]);
                    tracks = tracks && std::abs(h3.residual[i] - want) <=
                                           1e-5F * std::max(want, 1e-12F);
                }
                expect(tracks,
                       std::string("10b: ") + name(s) +
                           ": the 3-channel residual is the per-sweep WORST "
                           "channel, not a combined RMS -- which at sweep 12 "
                           "reads 2.6e-04 against the true 1.5e-01");

                // And convergence is per channel too: the small-f channel is the
                // slow one, so a combined rule would declare victory early.
                expect(!h3.converged && h3.residual.back() > 1e-2F,
                       std::string("10c: ") + name(s) +
                           ": 12 sweeps is NOT enough for the slow small-f "
                           "channel, and converged must reflect the worst one");
            }
        }

        // ---- 10d. one bad channel out of three is still rejected -----------
        //
        // Mirrors 14_poisson group 19. A whole-buffer mean accepts a two-channel
        // f whose channels are +5 and -5 and returns a quietly wrong answer for
        // both; the precondition is PER CHANNEL.
        {
            std::vector<float> v;
            v.reserve(4ULL * 1 * 3);
            const std::vector<float> ok = {-10.0F, 4.0F, 2.0F, 4.0F};
            const std::vector<float> bad = {-10.0F, 4.0F, 2.0F, 8.0F};
            for (std::size_t i = 0; i < 4; ++i) {
                v.push_back(ok[i]);
                v.push_back(bad[i]); // only this channel violates
                v.push_back(ok[i]);
            }
            const auto f3 = make_image(4, 1, 3, v);
            expect(threw_naming(
                       [&] {
                           (void)poisson_iter(f3, poisson_sweep::jacobi, 10,
                                              k_res_tol);
                       },
                       "poisson_iter"),
                   "10d: a single non-zero-mean channel among three is "
                   "rejected -- the precondition is per channel, and the three "
                   "means here sum to a non-zero value only in that channel");
        }

        // ---- 10e. the equation itself, checked directly --------------------
        //
        // Every other convergence group compares phi against a known phi or
        // against poisson_fft. THIS ONE ASSERTS THE EQUATION, which is what the
        // function claims to solve, with no reference to how phi was reached and
        // no reliance on the reported residual being computed correctly.
        {
            const auto phi = from_rule(8, 8, [](int x, int y) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                return std::cos(2.0F * k_pi * fx / 8.0F) +
                       (0.4F * std::sin(2.0F * k_pi * 3.0F * fy / 8.0F)) +
                       (0.7F * std::cos(2.0F * k_pi * 2.0F * fx / 8.0F));
            });
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            for (const auto s : both) {
                const auto got = poisson_iter(f, s, 2000, k_res_tol);
                expect(got.converged &&
                           recovers(laplacian(got.phi, vc_edge_policy::wrap), f,
                                    1e-4F),
                       std::string("10e: ") + name(s) +
                           ": laplacian(phi, wrap) really is f -- the equation "
                           "asserted directly, independent of the reported "
                           "residual");
            }
        }

        // ---- 10f. a 1x1 image ----------------------------------------------
        //
        // Under wrap every neighbour of the only pixel is itself, so the stencil
        // is 4c - 4c = 0 for any c: laplacian is identically zero, the only
        // admissible f is zero, and the zero-mean answer is zero. Degenerate,
        // reachable, and the kind of size 11_conv_theorem found bugs at.
        {
            const auto f = make_image(1, 1, 1, {0.0F});
            for (const auto s : both) {
                const auto got = poisson_iter(f, s, 5, k_res_tol);
                expect(got.converged && got.phi.width() == 1 &&
                           got.phi.height() == 1 && all_finite(got.phi) &&
                           std::abs(pixel(got.phi, 0, 0)) <= k_exact_tol,
                       std::string("10f: ") + name(s) +
                           ": a 1x1 image solves to zero without dividing by "
                           "anything degenerate");
            }
        }

        // ---- 11..14 rejections, each PAIRED with an acceptance --------------
        {
            vc::vc_image_writer u8{4, 2, 1, vc::buf_u8{0}};
            const auto bad = std::move(u8).seal();
            expect(threw_naming(
                       [&] {
                           (void)poisson_iter(bad, poisson_sweep::jacobi, 10,
                                              1e-6F);
                       },
                       "poisson_iter"),
                   "11a: a non-f32 f is rejected, and the message names "
                   "poisson_iter");

            const auto good = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(did_not_throw([&] {
                       (void)poisson_iter(good, poisson_sweep::jacobi, 10,
                                          1e-6F);
                   }),
                   "11b: PAIR -- a valid f32 f is accepted (without this, 11a "
                   "passes against a stub that throws unconditionally)");
        }
        {
            // Hand-built: f = laplacian(phi, wrap) satisfies the precondition by
            // construction and can never trip it.
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 5.0F});
            expect(threw_naming(
                       [&] {
                           (void)poisson_iter(f, poisson_sweep::jacobi, 10,
                                              1e-6F);
                       },
                       "poisson_iter"),
                   "12a: an f whose mean is not zero is rejected, and the "
                   "message names poisson_iter -- NOT poisson_fft, whose "
                   "zero-mean check this shares");
            expect(!threw_naming(
                       [&] {
                           (void)poisson_iter(f, poisson_sweep::jacobi, 10,
                                              1e-6F);
                       },
                       "poisson_fft"),
                   "12b: and the shared helper was given this caller's name, "
                   "not the one it was extracted from");
        }
        {
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(threw_naming(
                       [&] {
                           (void)poisson_iter(f, poisson_sweep::jacobi, 0,
                                              1e-6F);
                       },
                       "poisson_iter"),
                   "13a: max_sweeps == 0 is rejected rather than returning a "
                   "zero image that would quietly fail a gate");
            expect(did_not_throw([&] {
                       (void)poisson_iter(f, poisson_sweep::jacobi, 1, 1e-6F);
                   }),
                   "13b: PAIR -- max_sweeps == 1 is accepted");
        }
        {
            const auto f = make_image(4, 1, 1, {-10.0F, 4.0F, 2.0F, 4.0F});
            expect(threw_naming(
                       [&] {
                           (void)poisson_iter(f, poisson_sweep::jacobi, 10,
                                              -1.0F);
                       },
                       "poisson_iter"),
                   "14a: a negative tolerance is rejected");
            expect(threw_naming(
                       [&] {
                           (void)poisson_iter(
                               f, poisson_sweep::jacobi, 10,
                               std::numeric_limits<float>::quiet_NaN());
                       },
                       "poisson_iter"),
                   "14b: a NaN tolerance is rejected -- needs !(tol >= 0), "
                   "because `tol < 0` is FALSE for NaN and lets it through");
            expect(did_not_throw([&] {
                       (void)poisson_iter(f, poisson_sweep::jacobi, 10, 0.0F);
                   }),
                   "14c: PAIR -- a tolerance of exactly 0 is legal and simply "
                   "never converges");
        }

        // ---- 15. non-square both ways ---------------------------------------
        {
            for (const auto dims :
                 std::vector<std::pair<vc::image_dim, vc::image_dim>>{{8, 4},
                                                                      {4, 8}}) {
                const auto w = dims.first;
                const auto h = dims.second;
                const auto phi = from_rule(w, h, [w, h](int x, int y) {
                    return std::cos(2.0F * k_pi * static_cast<float>(x) /
                                    static_cast<float>(w)) +
                           (0.5F *
                            std::sin(2.0F * k_pi * static_cast<float>(y) /
                                     static_cast<float>(h)));
                });
                const auto f = laplacian(phi, vc_edge_policy::wrap);
                for (const auto s : both) {
                    const auto got = poisson_iter(f, s, 2000, k_res_tol);
                    expect(got.converged &&
                               recovers(got.phi, zero_meaned(phi), k_phi_tol),
                           std::string("15: ") + name(s) + " at " +
                               std::to_string(w) + "x" + std::to_string(h) +
                               " -- a square grid cannot see a swapped width "
                               "and height");
                }
            }
        }

        // ---- 16. the clamp-built f trap, re-pinned --------------------------
        //
        // A property of the problem, not of either solver. A clamp-built f sums
        // to zero too, so the precondition accepts it, and the answer comes back
        // wrong with no exception and no NaN. The only defence is building f
        // with wrap. 14_poisson group 16 pins the same thing for poisson_fft.
        {
            const auto phi = from_rule(8, 4, [](int x, int y) {
                return (static_cast<float>(x) * 1.5F) -
                       (static_cast<float>(y) * 0.75F) +
                       (static_cast<float>(x * y) * 0.25F);
            });
            const auto f_wrap = laplacian(phi, vc_edge_policy::wrap);
            const auto f_clamp = laplacian(phi, vc_edge_policy::clamp);

            const auto good = poisson_iter(f_wrap, poisson_sweep::gauss_seidel,
                                           4000, k_res_tol);
            // k_phi_tol, not the 1e-3 an earlier draft used without saying
            // why. Measured against a float reference on this field: 4.1e-06,
            // which is 12x under k_phi_tol. A loose unexplained constant on the
            // GOOD half of a trap test is how the trap stops being a trap.
            expect(good.converged &&
                       recovers(good.phi, zero_meaned(phi), k_phi_tol),
                   "16a: a wrap-built f recovers phi");

            const auto bad = poisson_iter(f_clamp, poisson_sweep::gauss_seidel,
                                          4000, k_res_tol);
            expect(did_not_throw([&] {
                       (void)poisson_iter(f_clamp, poisson_sweep::gauss_seidel,
                                          10, k_res_tol);
                   }) &&
                       all_finite(bad.phi) &&
                       !recovers(bad.phi, zero_meaned(phi), 1e-2F),
                   "16b: a CLAMP-built f is accepted, solved and returned "
                   "WRONG -- no throw, no NaN. Build f with wrap; nothing "
                   "inside the solver can defend this");
        }

        // ---- 17. exhausting the budget reports, it does not throw ----------
        {
            const auto phi = from_rule(16, 8, [](int x, int /*y*/) {
                return std::cos(2.0F * k_pi * static_cast<float>(x) / 16.0F);
            });
            const auto f = laplacian(phi, vc_edge_policy::wrap);
            const auto got = poisson_iter(f, poisson_sweep::jacobi, 3, 1e-12F);
            expect(
                !got.converged && got.residual.size() == 4 &&
                    all_finite(got.phi),
                "17: a budget too small reports converged == false and "
                "returns normally -- the caller asked by supplying a budget");
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "15_poisson_iter: " << passed << " / " << total_checks
                  << " checks passed before the exception\n";
        std::cout << "15_poisson_iter: FAIL\n";
        return 1;
    }

    std::cout << "15_poisson_iter: " << passed << " / " << total_checks
              << " checks passed\n";
    if (passed != total_checks) {
        std::cout << "15_poisson_iter: FAIL\n";
        return 1;
    }
    std::cout << "15_poisson_iter: OK\n";
    return 0;
}
