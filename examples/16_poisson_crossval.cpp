// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 16_poisson_crossval -- acceptance checks for S8b: red-black ordering, the
// residual plot, and the cross-validation of poisson_iter against poisson_fft.
//
// P2 section C's S8b, the graded piece. Reasoning lives in
// include/vc/pixelops/vc_poisson.h, include/vc/debug/vc_residual_plot.h and
// curriculum/notes/poisson.txt; this file only states what must be true.
//
// 15_poisson_iter covers jacobi and gauss_seidel. This file covers what S8b
// adds, and deliberately does not re-test what that one already pins.
//
// ---- READ THIS BEFORE TRUSTING A GREEN RUN ----
//
// GROUPS 5 AND 9 CONTAIN REJECTION CHECKS, which pass vacuously against any stub
// that throws unconditionally. EVERY ONE IS PAIRED with an acceptance check in
// the same group that a throwing stub fails. Do not delete the acceptance
// halves. 11_conv_theorem's first draft shipped four rejections that tested
// nothing, against a stub that threw for every input.
//
// ---- WHICH GROUPS ARE PAPER-ANCHORED ----
//
// GROUP 1 was computed on paper with no reference to any code in this
// repository -- sixteen numbers, listed in the group. It is the only check here
// that does not compare one piece of this library against another. Everything
// else cross-checks red_black against gauss_seidel or poisson_fft, so a
// misunderstanding shared between them passes. If group 1 fails and nothing else
// does, suspect the paper. If everything else fails and group 1 passes, suspect
// the solver.
//
// ---- WHAT EACH GROUP CATCHES, AND WHAT IT IS BLIND TO ----
//
// 1  ONE RED-BLACK SWEEP FROM ZERO, BY HAND, on 4x4. Pins the pass ORDER, the
//    parity convention, the in-place read in pass 2 and the mean subtraction,
//    all without any convergence. BLIND TO: everything about rates.
//
// 2  red_black AND gauss_seidel CONVERGE TO THE SAME phi. The answer, not the
//    route. BLIND TO: the rate -- a double-buffered red_black (i.e. an
//    accidental jacobi) passes this group completely.
//
// 3  THE RATE. red_black must reach tolerance in a sweep count comparable to
//    gauss_seidel and MUCH sooner than jacobi. THIS IS GATE 2 and it is the
//    only check in this file that catches the double-buffering mistake, because
//    that mistake produces the right answer slowly.
//
// 4  THE RESIDUAL IS MONOTONE for gauss_seidel and red_black. A curve that rises
//    means the sweep is diverging, which no end-state comparison detects.
//
// 5  ODD DIMENSIONS ARE REJECTED FOR red_black AND ACCEPTED FOR THE OTHER TWO.
//    Under wrap, an odd axis joins two same-coloured pixels and the independence
//    claim fails. The acceptance halves are what make the rejections non-vacuous.
//
// 6  THE JACOBI STALL, EXECUTABLE. A field carrying a checkerboard component
//    leaves jacobi's residual flat while the other two fall. This is
//    notes/poisson.txt section 17c as a test rather than an argument.
//
// 7  CROSS-VALIDATION AGAINST poisson_fft -- GATE 1. All three sweeps against
//    the transform solver, wrap on both sides, at 64x64 and below. Two routes
//    sharing almost no code: one divides a spectrum, one sweeps pixels.
//    BLIND TO: anything both get wrong, which is why group 1 exists.
//
// 8  THE GATE'S SIZE SCOPE IS REAL. 1e-5 is asserted at 64x64 and NOT at 256x256,
//    because vc_poisson.cpp measured 2.0e-04 there with a correct solver. Pins
//    the corrected criterion so a later tightening fails loudly.
//
// 9  residual_plot REJECTIONS AND ACCEPTANCE: empty span, empty series, tiny
//    canvas, non-finite value, non-positive floor under log -- each paired with
//    a call that must succeed.
//
// 10 THE PLOT IS DUMPED. Produces the PNG S8b owes. Not an assertion about
//    pixels; the artifact is for a human, and the checks around it only confirm
//    geometry and that every series survived into the render.

#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_types.h"
#include "vc/debug/vc_image_dumper.h"
#include "vc/debug/vc_residual_plot.h"
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

vc::vc_image from_rule(vc::image_dim w,
                       vc::image_dim h,
                       const std::function<float(int, int)>& f) {
    vc::vc_image_writer out{w, h, 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            out.at<vc::buf_f32>(x, y, 0) =
                f(static_cast<int>(x), static_cast<int>(y));
        }
    }
    return std::move(out).seal();
}

// Subtract the mean of each channel, so a field built from a rule can be fed to
// solvers that require zero mean. Done here rather than trusting the rule to be
// balanced: a cosine over a grid is zero-mean only when the period divides the
// width, which is a condition easy to violate by editing a constant.
vc::vc_image zero_meaned(const vc::vc_image& src) {
    vc::vc_image_writer out{src.width(), src.height(), src.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::channel_count c = 0; c < src.channels(); ++c) {
        double sum = 0.0;
        for (vc::image_dim y = 0; y < src.height(); ++y) {
            for (vc::image_dim x = 0; x < src.width(); ++x) {
                sum += static_cast<double>(src.at<vc::buf_f32>(x, y, c));
            }
        }
        const auto mean = sum / static_cast<double>(src.pixel_count());
        for (vc::image_dim y = 0; y < src.height(); ++y) {
            for (vc::image_dim x = 0; x < src.width(); ++x) {
                out.at<vc::buf_f32>(x, y, c) = static_cast<vc::buf_f32>(
                    static_cast<double>(src.at<vc::buf_f32>(x, y, c)) - mean);
            }
        }
    }
    return std::move(out).seal();
}

// Worst absolute difference, relative to b's RMS. The gates are stated this way
// throughout P2 (section F.3) because an absolute threshold is wrong at both
// ends: it rejects a legitimately large field and accepts a badly wrong small
// one.
double rel_max_diff(const vc::vc_image& a, const vc::vc_image& b) {
    double worst = 0.0;
    double ssq = 0.0;
    for (vc::channel_count c = 0; c < b.channels(); ++c) {
        for (vc::image_dim y = 0; y < b.height(); ++y) {
            for (vc::image_dim x = 0; x < b.width(); ++x) {
                const auto av = static_cast<double>(a.at<vc::buf_f32>(x, y, c));
                const auto bv = static_cast<double>(b.at<vc::buf_f32>(x, y, c));
                worst = std::max(worst, std::abs(av - bv));
                ssq += bv * bv;
            }
        }
    }
    const auto n = static_cast<double>(b.pixel_count()) * b.channels();
    const auto rms = std::sqrt(ssq / n);
    return worst / std::max(rms, 1e-12);
}

// A smooth zero-mean field with NO Nyquist component, so jacobi converges and
// the three sweeps can be compared on equal terms. The exclusion is deliberate
// and is the subject of group 6: a field carrying k = width/2 stalls jacobi
// forever, which reads as a solver bug rather than as the method's known limit.
vc::vc_image smooth_field(vc::image_dim w, vc::image_dim h) {
    const auto turn = 2.0 * std::numbers::pi;
    return zero_meaned(from_rule(w, h, [&](int x, int y) {
        const auto fx = static_cast<double>(x) / static_cast<double>(w);
        const auto fy = static_cast<double>(y) / static_cast<double>(h);
        return static_cast<float>(std::cos(turn * fx) +
                                  (0.5 * std::sin(turn * 2.0 * fy)) +
                                  (0.25 * std::cos(turn * 3.0 * (fx + fy))));
    }));
}

// How many sweeps a run actually took. residual[c][0] is the value BEFORE any
// sweep, so the count is size() - 1, and the worst channel is the one that
// decides whether the run converged.
std::size_t sweeps_taken(const vc::pixelops::poisson_iter_report& r) {
    std::size_t worst = 0;
    for (const auto& channel : r.residual) {
        worst = std::max(worst, channel.size() - 1);
    }
    return worst;
}

} // namespace

int main() {
    using namespace vc::pixelops;
    using vc::debug::residual_plot;
    using vc::debug::residual_plot_config;
    using vc::debug::residual_series;

    try {
        // ---- GROUP 1 -- ONE RED-BLACK SWEEP FROM ZERO, BY HAND ----
        //
        // 4x4, wrap, f zero everywhere except f(0,0) = +4 and f(1,0) = -4 (so
        // the mean is zero and poisson_iter accepts it). One sweep, tolerance 0
        // so it cannot stop early.
        //
        // PASS 1, the even pixels (x+y even). Every neighbour is odd and still
        // zero, so each reduces to -f/4. Only (0,0) has a non-zero f:
        //
        //     phi(0,0) = -4/4 = -1,  every other even pixel = 0
        //
        // PASS 2, the odd pixels, reading the evens PASS 1 JUST WROTE. Four of
        // them touch (0,0); under wrap those are (1,0), (3,0), (0,1) and (0,3):
        //
        //     (1,0)  nbrs 0 + (-1) + 0 + 0 = -1,  f = -4  ->  (-1 + 4)/4 =  0.75
        //     (3,0)  nbrs -1 (x+1 wraps to 0)             ->  (-1 - 0)/4 = -0.25
        //     (0,1)  nbrs -1 (y-1 ... no: x-1 wraps)      ->             = -0.25
        //     (0,3)  nbrs -1 (y+1 wraps to 0)             ->             = -0.25
        //     the other four odd pixels see no -1          ->             =  0
        //
        // Sum = -1 + 0.75 - 0.25 - 0.25 - 0.25 = -1.0, so the mean is -0.0625
        // and subtract_mean adds 0.0625 to every pixel. Final, all sixteen:
        //
        //     -0.9375   0.8125   0.0625  -0.1875
        //     -0.1875   0.0625   0.0625   0.0625
        //      0.0625   0.0625   0.0625   0.0625
        //     -0.1875   0.0625   0.0625   0.0625
        //
        // THIS GROUP IS THE ONLY ONE HERE NOT ANCHORED TO OTHER CODE IN THIS
        // REPOSITORY. It pins, in one shot: that pass 1 is EVEN and pass 2 is
        // ODD (swap them and (1,0) reads a zero instead of a -1); that pass 2
        // reads IN PLACE (double-buffer it and every odd pixel sees 0); that the
        // parity is (x+y) and not x or y alone; that the wrap is present; and
        // that the mean is subtracted once per SWEEP and not once per PASS.
        {
            const vc::vc_image f = from_rule(4, 4, [](int x, int y) {
                if (x == 0 && y == 0) {
                    return 4.0F;
                }
                if (x == 1 && y == 0) {
                    return -4.0F;
                }
                return 0.0F;
            });

            const auto r = poisson_iter(f, poisson_sweep::red_black, 1, 0.0F);

            const std::vector<float> want = {
                -0.9375F, 0.8125F, 0.0625F, -0.1875F, //
                -0.1875F, 0.0625F, 0.0625F, 0.0625F,  //
                0.0625F,  0.0625F, 0.0625F, 0.0625F,  //
                -0.1875F, 0.0625F, 0.0625F, 0.0625F};

            bool all_match = true;
            std::size_t i = 0;
            for (vc::image_dim y = 0; y < 4; ++y) {
                for (vc::image_dim x = 0; x < 4; ++x) {
                    const auto got = r.phi.at<vc::buf_f32>(x, y, 0);
                    if (std::abs(got - want[i]) > 1e-6F) {
                        all_match = false;
                        std::cerr << "  (" << x << "," << y << ") got " << got
                                  << " want " << want[i] << '\n';
                    }
                    ++i;
                }
            }
            expect(all_match,
                   "1a one red-black sweep from zero matches all sixteen "
                   "hand-computed values");
            expect(sweeps_taken(r) == 1,
                   "1b tolerance 0 runs exactly the budget, never early-stops");
        }

        // ---- GROUP 2 -- red_black AND gauss_seidel REACH THE SAME phi ----
        //
        // Same equation, same boundaries, same zero start, different visiting
        // order. The fixed point does not depend on the order, so the answers
        // must agree -- to solver tolerance, not to the bit.
        //
        // ** THIS GROUP IS BLIND TO THE RATE. ** A red_black written with two
        // buffers is really jacobi, converges to this same phi, and passes here
        // without a murmur. Group 3 is what catches that.
        {
            const vc::vc_image f = smooth_field(32, 32);
            const auto rb =
                poisson_iter(f, poisson_sweep::red_black, 20000, 1e-7F);
            const auto gs =
                poisson_iter(f, poisson_sweep::gauss_seidel, 20000, 1e-7F);

            expect(rb.converged, "2a red_black converges inside the budget");
            expect(gs.converged, "2b gauss_seidel converges inside the budget");
            expect(rel_max_diff(rb.phi, gs.phi) <= 1e-4,
                   "2c red_black and gauss_seidel agree on phi");
        }

        // ---- GROUP 3 -- THE RATE. THIS IS GATE 2 ----
        //
        // red_black must track gauss_seidel, NOT jacobi. The factor of 2 is
        // deliberate slack: the two orderings are not required to be identical,
        // only comparable, and the gap to jacobi on this field is far larger
        // than 2x. If red_black lands near jacobi's count, the colouring is
        // wrong -- in practice either two buffers instead of one, or a parity
        // computed on x alone.
        //
        // The sweep COUNTS are printed, not just compared, because the ratio is
        // the number S8b's write-up reports and a bare pass/fail hides it.
        {
            const vc::vc_image f = smooth_field(32, 32);
            const auto jac =
                poisson_iter(f, poisson_sweep::jacobi, 20000, 1e-6F);
            const auto gs =
                poisson_iter(f, poisson_sweep::gauss_seidel, 20000, 1e-6F);
            const auto rb =
                poisson_iter(f, poisson_sweep::red_black, 20000, 1e-6F);

            std::cout << "  sweeps to 1e-6 at 32x32:  jacobi "
                      << sweeps_taken(jac) << "  gauss_seidel "
                      << sweeps_taken(gs) << "  red_black " << sweeps_taken(rb)
                      << '\n';

            expect(
                jac.converged && gs.converged && rb.converged,
                "3a all three converge on a field with no Nyquist component");
            expect(sweeps_taken(gs) < sweeps_taken(jac),
                   "3b gauss_seidel beats jacobi, as 14b predicts");
            expect(sweeps_taken(rb) <= 2 * sweeps_taken(gs),
                   "3c GATE 2: red_black tracks gauss_seidel's rate");
            expect(sweeps_taken(rb) < sweeps_taken(jac),
                   "3d GATE 2: red_black is NOT running at jacobi's rate -- "
                   "this is the check a double-buffered red_black fails");
        }

        // ---- GROUP 4 -- THE RESIDUAL IS MONOTONE ----
        //
        // A rising residual means the sweep is diverging. No comparison of end
        // states detects it, because a diverging run that happens to be stopped
        // at a budget still produces an image.
        //
        // jacobi is NOT asserted monotone here: its error mode factors are
        // cos-based and NEGATIVE for half the spectrum, so the residual may
        // oscillate while shrinking in magnitude. That is its documented
        // behaviour, not a defect, and asserting monotonicity on it would be
        // asserting something untrue.
        {
            const vc::vc_image f = smooth_field(16, 16);
            for (const auto sweep :
                 {poisson_sweep::gauss_seidel, poisson_sweep::red_black}) {
                const auto r = poisson_iter(f, sweep, 500, 1e-6F);
                bool monotone = true;
                for (const auto& channel : r.residual) {
                    for (std::size_t i = 1; i < channel.size(); ++i) {
                        if (channel[i] > channel[i - 1] * 1.000001F) {
                            monotone = false;
                        }
                    }
                }
                expect(monotone,
                       std::string{"4 residual decreases every sweep for "} +
                           (sweep == poisson_sweep::red_black
                                ? "red_black"
                                : "gauss_seidel"));
            }
        }

        // ---- GROUP 5 -- ODD DIMENSIONS, REJECTED AND ACCEPTED ----
        //
        // Under wrap, column w-1 neighbours column 0, and their parities differ
        // by w-1. An odd w makes them equal, so two same-coloured pixels are
        // adjacent and "update every red at once" stops being true.
        //
        // ** THE ACCEPTANCE HALVES ARE LOAD-BEARING. ** Checks 5a-5c pass
        // against any stub that throws unconditionally; 5d-5f are what prove the
        // rejection is selective rather than a blanket failure.
        {
            const auto rejects = [](vc::image_dim w, vc::image_dim h) {
                try {
                    const vc::vc_image f = smooth_field(w, h);
                    (void)poisson_iter(f, poisson_sweep::red_black, 5, 0.0F);
                    return false;
                } catch (const vc::vc_exception& e) {
                    return e.code() == vc::vc_error_code::invalid_argument;
                }
            };

            expect(rejects(5, 4), "5a red_black rejects an odd WIDTH");
            expect(rejects(4, 5), "5b red_black rejects an odd HEIGHT");
            expect(rejects(5, 5), "5c red_black rejects both odd");

            expect(!rejects(4, 4), "5d red_black ACCEPTS even x even");

            const vc::vc_image odd = smooth_field(5, 5);
            bool jacobi_ok = true;
            bool gs_ok = true;
            try {
                (void)poisson_iter(odd, poisson_sweep::jacobi, 5, 0.0F);
            } catch (const std::exception&) {
                jacobi_ok = false;
            }
            try {
                (void)poisson_iter(odd, poisson_sweep::gauss_seidel, 5, 0.0F);
            } catch (const std::exception&) {
                gs_ok = false;
            }
            expect(jacobi_ok, "5e jacobi still ACCEPTS an odd grid");
            expect(gs_ok, "5f gauss_seidel still ACCEPTS an odd grid");
        }

        // ---- GROUP 6 -- THE JACOBI STALL, AS A TEST ----
        //
        // notes/poisson.txt section 17c, made executable.
        //
        // Take phi_known = the pure checkerboard, +1 on even pixels and -1 on
        // odd. Every neighbour is its negative, so
        //
        //     laplacian(cb) = 4(-v) - 4v = -8v,   i.e.  f = -8 . cb
        //
        // which is zero-mean by construction. Now run jacobi from zero, by hand:
        //
        //     sweep 1   phi = (0 - f)/4 = 2.cb,  laplacian = -16.cb,
        //               r = f - that = 8.cb,  ||r||/||f|| = 8/8 = 1
        //     sweep 2   neighbours sum to -8.cb_value, f is the same,
        //               phi = 0 again
        //
        // It oscillates between 0 and 2.cb and the relative residual is EXACTLY
        // 1.0 forever. Not slow -- stationary.
        //
        // red_black on the same field is exact in ONE sweep: pass 1 sets every
        // even to 2, pass 2 sees neighbours summing to 8 against f = +8 and
        // leaves every odd at 0, and subtracting the mean of 1 gives +1/-1,
        // which IS phi_known.
        //
        // That contrast is the single clearest thing S8b produces, and it is why
        // P2 section D's "cut jacobi first" was overridden.
        {
            const vc::vc_image f = from_rule(16, 16, [](int x, int y) {
                return ((x + y) % 2 == 0) ? -8.0F : 8.0F;
            });

            const auto jac = poisson_iter(f, poisson_sweep::jacobi, 60, 1e-6F);
            const auto rb =
                poisson_iter(f, poisson_sweep::red_black, 60, 1e-6F);

            expect(!jac.converged,
                   "6a jacobi does NOT converge on a pure checkerboard");
            bool stalled = true;
            for (const auto& channel : jac.residual) {
                for (const auto v : channel) {
                    if (std::abs(v - 1.0F) > 1e-4F) {
                        stalled = false;
                    }
                }
            }
            expect(stalled,
                   "6b jacobi's relative residual is EXACTLY 1.0 at every "
                   "sweep -- stationary, not merely slow");

            expect(rb.converged,
                   "6c red_black DOES converge on the same field");
            expect(sweeps_taken(rb) <= 2,
                   "6d red_black solves the pure checkerboard in one sweep");

            const vc::vc_image want = from_rule(16, 16, [](int x, int y) {
                return ((x + y) % 2 == 0) ? 1.0F : -1.0F;
            });
            expect(rel_max_diff(rb.phi, want) <= 1e-5,
                   "6e and the answer it reaches is the checkerboard itself");
        }

        // ---- GROUP 7 -- CROSS-VALIDATION AGAINST poisson_fft. GATE 1 ----
        //
        // Two solvers that share almost no code: one divides a spectrum bin by
        // bin, one sweeps pixels. wrap on both sides -- poisson_fft solves the
        // PERIODIC problem and nothing else, so a sweep with any other boundary
        // would be a different problem and the disagreement would read as a
        // convergence bug.
        //
        // Both return the ZERO-MEAN solution, which is what makes a direct
        // comparison meaningful: the periodic problem fixes phi only up to an
        // added constant, and without that normalisation the two could be right
        // and differ everywhere by the same amount.
        {
            for (const vc::image_dim n : {8U, 16U, 32U, 64U}) {
                const vc::vc_image f = smooth_field(n, n);
                const vc::vc_image ref = poisson_fft(f);

                for (const auto sweep :
                     {poisson_sweep::jacobi, poisson_sweep::gauss_seidel,
                      poisson_sweep::red_black}) {
                    const auto r = poisson_iter(f, sweep, 200000, 1e-9F);
                    const auto d = rel_max_diff(r.phi, ref);
                    expect(d <= 1e-5,
                           std::string{"7 GATE 1 at "} + std::to_string(n) +
                               "x" + std::to_string(n) + ", sweep " +
                               std::to_string(static_cast<int>(sweep)) +
                               ": agrees with poisson_fft, rel " +
                               std::to_string(d));
                }
            }
        }

        // ---- GROUP 8 -- THE GATE'S SIZE SCOPE IS REAL ----
        //
        // P2's criterion said "1e-5 relative to RMS" with no size attached, and
        // vc_poisson.cpp measured the round-trip error at 3.3e-06 (64^2),
        // 4.8e-05 (128^2) and 2.0e-04 (256^2) WITH A CORRECT SOLVER -- near-DC
        // amplification, not a defect. The plan now scopes the gate to 64^2 and
        // below, and this group is what stops a later edit from quietly
        // tightening it back.
        //
        // So: assert 1e-5 at 64^2, and at 128^2 assert only the LOOSER bound
        // that the measurement supports. A failure here at 128^2 means something
        // got worse; a 1e-5 PASS at 128^2 would mean the measurement was wrong
        // and this comment needs rewriting.
        {
            const vc::vc_image f64 = smooth_field(64, 64);
            const auto d64 = rel_max_diff(
                poisson_iter(f64, poisson_sweep::red_black, 200000, 1e-9F).phi,
                poisson_fft(f64));
            expect(d64 <= 1e-5,
                   "8a 1e-5 holds at 64x64, which is where P2 now scopes it");

            const vc::vc_image f128 = smooth_field(128, 128);
            const auto d128 = rel_max_diff(
                poisson_iter(f128, poisson_sweep::red_black, 400000, 1e-9F).phi,
                poisson_fft(f128));
            std::cout << "  cross-validation rel error:  64^2 " << d64
                      << "   128^2 " << d128 << '\n';
            expect(d128 <= 1e-3,
                   "8b 128x128 agrees to the looser bound the measurement "
                   "supports, not to 1e-5");
        }

        // ---- GROUP 9 -- residual_plot REJECTIONS AND ACCEPTANCE ----
        //
        // ** 9a-9e PASS VACUOUSLY AGAINST A THROWING STUB. 9f IS THE PAIR. **
        {
            const auto throws = [](const std::function<void()>& fn) {
                try {
                    fn();
                    return false;
                } catch (const vc::vc_exception& e) {
                    return e.code() == vc::vc_error_code::invalid_argument;
                }
            };

            const std::vector<residual_series> good = {
                {"a", {1.0F, 0.5F, 0.25F, 0.125F}}};

            expect(throws([] {
                       (void)residual_plot(std::span<const residual_series>{});
                   }),
                   "9a an empty series span is rejected");
            expect(throws([] {
                       const std::vector<residual_series> s = {{"a", {}}};
                       (void)residual_plot(s);
                   }),
                   "9b a series with no values is rejected");
            expect(throws([&] {
                       (void)residual_plot(good, {.width = 32, .height = 32});
                   }),
                   "9c a canvas below 64 on either axis is rejected");
            expect(
                throws([] {
                    const std::vector<residual_series> s = {
                        {"a", {1.0F, std::numeric_limits<float>::quiet_NaN()}}};
                    (void)residual_plot(s);
                }),
                "9d a non-finite value is rejected, not skipped");
            expect(throws([&] { (void)residual_plot(good, {.floor = 0.0F}); }),
                   "9e floor <= 0 under a log axis is rejected");

            const vc::vc_image plot = residual_plot(good);
            expect(plot.width() == 800 && plot.height() == 500 &&
                       plot.channels() == 3,
                   "9f a valid call SUCCEEDS at the documented default size -- "
                   "this is what makes 9a-9e non-vacuous");

            const std::vector<residual_series> with_zero = {
                {"all-zero channel", {0.0F, 0.0F}}};
            bool zero_ok = true;
            try {
                (void)residual_plot(with_zero);
            } catch (const std::exception&) {
                zero_ok = false;
            }
            expect(
                zero_ok,
                "9g a legitimately zero residual is CLAMPED to the floor and "
                "drawn, not rejected -- poisson_iter documents this case");
        }

        // ---- GROUP 10 -- THE PLOT IS DUMPED. THE ARTIFACT S8b OWES ----
        //
        // Three methods on one axis, the picture that makes group 3's numbers
        // and group 6's stall legible. The checks here are weak on purpose: the
        // output is for a human, and asserting pixel values would pin a drawing
        // style rather than a property.
        {
            const vc::vc_image smooth = smooth_field(32, 32);
            const vc::vc_image checker = from_rule(32, 32, [](int x, int y) {
                return ((x + y) % 2 == 0) ? -8.0F : 8.0F;
            });

            std::vector<residual_series> series;
            for (const auto& [label, f] :
                 std::vector<std::pair<std::string, vc::vc_image>>{
                     {"smooth", smooth}, {"checkerboard", checker}}) {
                for (const auto& [name, sweep] :
                     std::vector<std::pair<std::string, poisson_sweep>>{
                         {"jacobi", poisson_sweep::jacobi},
                         {"gauss_seidel", poisson_sweep::gauss_seidel},
                         {"red_black", poisson_sweep::red_black}}) {
                    const auto r = poisson_iter(f, sweep, 400, 1e-8F);
                    series.push_back({label + "/" + name, r.residual.at(0)});
                }
            }

            expect(series.size() == 6,
                   "10a six curves: three sweeps on two fields");

            const vc::vc_image plot =
                residual_plot(series, {.width = 1000, .height = 600});
            expect(plot.width() == 1000 && plot.height() == 600,
                   "10b the plot honours a non-default canvas size");

            vc::debug::set_enabled(true);
            vc::debug::dump("poisson_residual", plot);
            std::cout << "  residual plot written under "
                      << vc::debug::output_dir().string() << '\n';
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "16_poisson_crossval: " << passed << " / " << total_checks
                  << " checks passed before the exception\n";
        std::cout << "16_poisson_crossval: FAIL\n";
        return 1;
    }

    std::cout << "16_poisson_crossval: " << passed << " / " << total_checks
              << " checks passed\n";
    if (passed != total_checks) {
        std::cout << "16_poisson_crossval: FAIL\n";
        return 1;
    }
    std::cout << "16_poisson_crossval: OK\n";
    return 0;
}
