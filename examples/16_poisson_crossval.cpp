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
// 4  THE RESIDUAL DECREASES -- but NOT monotonically from sweep 0, and P2's
//    Gate 2 wording ("the residual curve is monotone decreasing") is WRONG as
//    stated. Two legitimate exceptions, both confirmed by simulation before
//    this check was written: the FIRST sweep from zero RAISES it (1.0 -> ~1.27
//    at 16x16), because the opening step is large and overshoots some modes;
//    and near the f32 floor it JITTERS by a few percent. So the assertion skips
//    sweep 1 and only applies while the residual is above 10x its final value.
//
// 5  ODD DIMENSIONS ARE REJECTED FOR red_black AND ACCEPTED FOR THE OTHER TWO.
//    Under wrap, an odd axis joins two same-coloured pixels and the independence
//    claim fails. The acceptance halves are what make the rejections non-vacuous.
//
// 6  THE JACOBI STALL, EXECUTABLE. A field carrying a checkerboard component
//    leaves jacobi's residual flat while the other two fall. This is
//    notes/poisson.txt section 17c as a test rather than an argument.
//
// 7  CROSS-VALIDATION AGAINST poisson_fft -- GATE 1, at 16x8 AND BELOW, which
//    is where P2 scoped it FROM A REAL RUN on 2026-10-16. Two routes sharing
//    almost no code: one divides a spectrum, one sweeps pixels. BLIND TO:
//    anything both get wrong, which is why group 1 exists.
//
// 8  32x32 AND 64x64, REPORTED AND NOT GATED. Outside Gate 1's scope on
//    purpose. The printed numbers are the deliverable; the 1e-3 assert is a
//    tripwire for a real regression, not a competing gate.
//
// 9  residual_plot REJECTIONS AND ACCEPTANCE: empty span, empty series, tiny
//    canvas, non-finite value, non-positive floor under log -- each paired with
//    a call that must succeed.
//
// ---- WHERE THE CONSTANTS IN THIS FILE CAME FROM, AND THE HEALTH WARNING ----
//
// ** THE TOLERANCES AND BUDGETS BELOW WERE SET FROM A SIMULATION OF THE SAME
// ARITHMETIC IN f32, NOT FROM THIS LIBRARY. ** The bodies did not exist when
// this file was written. They are chosen with margin and every one of them
// PRINTS what it actually achieved, so the first green run calibrates them.
// If a bound here is wrong, it is wrong in the direction of being too loose.
//
// What that simulation established, and why it forced four rewrites of this
// file before it was first compiled:
//
//   THE RELATIVE RESIDUAL HAS A HARD f32 FLOOR THAT SCALES WITH THE GRID. It is
//   not slow convergence -- at 16x16 it pins at ~5e-7 from sweep 500 and does
//   not move through sweep 6000. Roughly 5e-7 (16^2), 2e-6 (32^2), 9e-6 (64^2),
//   about 4x per doubling.
//
//   SO A TOLERANCE OF 1e-7 OR 1e-9 IS UNREACHABLE AT EVERY SIZE HERE. A first
//   draft of this file asked for 1e-9 with a 200000-sweep budget in three
//   groups. Every one would have burned the entire budget, failed its
//   `converged` assertion, and taken minutes to do it.
//
//   AND THE ERROR AGAINST poisson_fft TRACKS THAT FLOOR, roughly 1.3x it.
//
// ** THE ONE PLACE THAT SIMULATION WAS OVERRULED, AND IT IS THE IMPORTANT ONE.
// ** It suggested 1e-5 agreement would hold at 32x32, and an earlier draft of
// this file widened Gate 1 to match. P2 had already scoped Gate 1 to 16x8 on
// 2026-10-16 FROM A RUN OF THIS LIBRARY -- at width 64 the near-DC mode decays
// by 0.9976 per sweep, ~4,776 sweeps per 1e-5 reduction against ~297 at width
// 16, plus an f32 floor of about eps x 831. The simulated figure lost. A test
// that widens a gate on evidence weaker than the gate's own is worse than no
// test, and group 8 reports the larger sizes instead of gating them.
//
//   JACOBI TAKES ABOUT TWICE GAUSS-SEIDEL'S SWEEPS, consistently: 58/33 at 8^2,
//   234/123 at 16^2, 941/477 at 32^2, 3815/1897 at 64^2. Group 3 asserts the
//   ratio rather than just the ordering, because the ordering alone is satisfied
//   by a one-sweep difference.
//
//   RED-BLACK LANDS WITHIN A FEW PERCENT OF GAUSS-SEIDEL: 31/33, 122/123,
//   488/477, 1956/1897. Group 3's 2x allowance is deliberately far looser than
//   that, so a different-but-correct ordering is not failed.
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

#include <algorithm>
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
        // PASS 2, the odd pixels, reading the evens PASS 1 JUST WROTE. Exactly
        // four odd pixels touch (0,0) under wrap -- (1,0), (3,0), (0,1), (0,3):
        //
        //     (1,0)  neighbour sum -1,  f = -4  ->  (-1 + 4)/4 =  0.75
        //     (3,0)  neighbour sum -1,  f =  0  ->  (-1 - 0)/4 = -0.25
        //     (0,1)  neighbour sum -1,  f =  0  ->             = -0.25
        //     (0,3)  neighbour sum -1,  f =  0  ->             = -0.25
        //     the other four odd pixels see no -1               =  0
        //
        // Sum = -1 + 0.75 - 0.25 - 0.25 - 0.25 = -1.0, so the mean is -0.0625
        // and subtract_mean adds 0.0625 everywhere. Final, all sixteen:
        //
        //     -0.9375   0.8125   0.0625  -0.1875
        //     -0.1875   0.0625   0.0625   0.0625
        //      0.0625   0.0625   0.0625   0.0625
        //     -0.1875   0.0625   0.0625   0.0625
        //
        // THE ONLY CHECK HERE NOT ANCHORED TO OTHER CODE IN THIS REPOSITORY. It
        // pins, in one shot: that pass 1 is EVEN and pass 2 is ODD (swap them
        // and (1,0) reads a zero rather than a -1); that pass 2 reads IN PLACE
        // (double-buffer it and every odd pixel sees 0); that the parity is
        // (x+y) and not x or y alone; that the wrap is present; and that the
        // mean is subtracted once per SWEEP, not once per PASS (per pass, the
        // -1 that pass 2 reads becomes -0.9375 and every number below changes).
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
        // ** BLIND TO THE RATE. ** A red_black written with two buffers is
        // really jacobi, converges to this same phi, and passes here without a
        // murmur. Group 3 is what catches that.
        //
        // Tolerance 1e-5, not the 1e-7 a first draft asked for: the f32 floor at
        // 32x32 is around 2e-6, so 1e-7 never arrives and `converged` is false
        // for both after the full budget.
        {
            const vc::vc_image f = smooth_field(32, 32);
            const auto rb =
                poisson_iter(f, poisson_sweep::red_black, 5000, 1e-5F);
            const auto gs =
                poisson_iter(f, poisson_sweep::gauss_seidel, 5000, 1e-5F);

            expect(rb.converged, "2a red_black converges inside the budget");
            expect(gs.converged, "2b gauss_seidel converges inside the budget");
            const auto d = rel_max_diff(rb.phi, gs.phi);
            std::cout << "  2: red_black vs gauss_seidel, rel " << d << '\n';
            expect(d <= 1e-4, "2c red_black and gauss_seidel agree on phi");
        }

        // ---- GROUP 3 -- THE RATE. THIS IS GATE 2 ----
        //
        // red_black must track gauss_seidel, NOT jacobi.
        //
        // 3b asserts the RATIO and not merely the ordering. "gauss_seidel took
        // fewer sweeps" is satisfied by a difference of one, which a broken
        // implementation can produce by accident; the simulated ratio is close
        // to 2 at every size, so 1.5 is a real floor with margin beneath it.
        //
        // 3c's 2x allowance is deliberately loose -- red_black and gauss_seidel
        // came within a few percent of each other in simulation, so this will
        // not fail a correct-but-differently-ordered implementation.
        //
        // 3d is THE ONE CHECK IN THIS FILE THAT CATCHES DOUBLE BUFFERING, which
        // produces the right answer at the wrong rate and passes every other
        // group here.
        //
        // Tolerance 1e-4: reachable at 32x32 for all three (1e-6 is below the
        // floor and none of them would converge).
        {
            const vc::vc_image f = smooth_field(32, 32);
            const auto jac =
                poisson_iter(f, poisson_sweep::jacobi, 20000, 1e-4F);
            const auto gs =
                poisson_iter(f, poisson_sweep::gauss_seidel, 20000, 1e-4F);
            const auto rb =
                poisson_iter(f, poisson_sweep::red_black, 20000, 1e-4F);

            const auto nj = sweeps_taken(jac);
            const auto ng = sweeps_taken(gs);
            const auto nr = sweeps_taken(rb);
            std::cout << "  3: sweeps to 1e-4 at 32x32 -- jacobi " << nj
                      << "  gauss_seidel " << ng << "  red_black " << nr
                      << "  (jac/gs " << (double(nj) / double(ng)) << ", rb/gs "
                      << (double(nr) / double(ng)) << ")\n";

            expect(jac.converged && gs.converged && rb.converged,
                   "3a all three converge on a field with no Nyquist "
                   "component");
            expect(double(nj) >= 1.5 * double(ng),
                   "3b jacobi needs at least 1.5x gauss_seidel's sweeps -- the "
                   "RATIO, not just the ordering");
            expect(double(nr) <= 2.0 * double(ng),
                   "3c GATE 2: red_black tracks gauss_seidel's rate");
            expect(nr < nj,
                   "3d GATE 2: red_black is NOT running at jacobi's rate -- "
                   "this is the check a double-buffered red_black fails");
        }

        // ---- GROUP 4 -- THE RESIDUAL DECREASES ----
        //
        // ** P2's GATE 2 SAYS "the residual curve is monotone decreasing" AND
        // THAT IS NOT TRUE OF A CORRECT SOLVER. ** Two legitimate exceptions,
        // both found by simulating this before asserting it:
        //
        //   SWEEP 1 RAISES IT. From a zero start the opening step is large and
        //   overshoots several modes; 16x16 goes 1.0 -> about 1.27 and only then
        //   begins to fall. Asserting monotonicity from sweep 0 fails every
        //   correct implementation.
        //
        //   NEAR THE FLOOR IT JITTERS. Once the residual reaches the f32 floor
        //   it stops being a convergence measurement and becomes roundoff; in
        //   simulation it wobbled by a few percent across the last decade.
        //
        // So: skip sweep 1, and assert only while the residual is above 10x its
        // final value. That window is where the curve carries information, and
        // a genuinely diverging sweep fails inside it immediately.
        //
        // jacobi is excluded entirely -- its mode factors are negative for half
        // the spectrum, so its residual legitimately oscillates while shrinking.
        {
            const vc::vc_image f = smooth_field(16, 16);
            for (const auto sweep :
                 {poisson_sweep::gauss_seidel, poisson_sweep::red_black}) {
                const auto r = poisson_iter(f, sweep, 2000, 1e-5F);
                bool ok = true;
                for (const auto& channel : r.residual) {
                    const auto floor_guard = channel.back() * 10.0F;
                    for (std::size_t i = 2; i < channel.size(); ++i) {
                        if (channel[i - 1] <= floor_guard) {
                            break;
                        }
                        if (channel[i] > channel[i - 1] * 1.01F) {
                            ok = false;
                        }
                    }
                }
                expect(ok,
                       std::string{"4 residual decreases from sweep 2 while "
                                   "above 10x the floor, for "} +
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
        // ** THE ACCEPTANCE HALVES ARE LOAD-BEARING. ** 5a-5c pass against any
        // stub that throws unconditionally; 5d-5f prove the rejection is
        // selective rather than a blanket failure.
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
        // phi_known = the pure checkerboard, +1 on even pixels and -1 on odd.
        // Every neighbour is its negative, so
        //
        //     laplacian(cb) = 4(-v) - 4v = -8v,   i.e.  f = -8 . cb
        //
        // zero-mean by construction. Jacobi from zero, by hand:
        //
        //     sweep 1   phi = (0 - f)/4 = 2.cb,  laplacian = -16.cb,
        //               r = f - that = 8.cb,  ||r||/||f|| = 8/8 = 1
        //     sweep 2   neighbours sum to -8.cb_value against the same f,
        //               so phi returns to 0
        //
        // It oscillates between 0 and 2.cb and the relative residual is EXACTLY
        // 1.0 forever. Not slow -- stationary. 6b asserts that rather than
        // merely asserting non-convergence, because "did not converge" is also
        // what a budget that is simply too small looks like.
        //
        // red_black on the same field is EXACT IN ONE SWEEP: pass 1 sets every
        // even to 2, pass 2 sees neighbours summing to 8 against f = +8 and
        // leaves every odd at 0, and subtracting the mean of 1 gives +1/-1,
        // which IS phi_known. Every value here is representable in f32, so 6e
        // is an equality in all but name.
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
        // Two solvers sharing almost no code: one divides a spectrum bin by bin,
        // one sweeps pixels. wrap on both sides -- poisson_fft solves the
        // PERIODIC problem and nothing else, so a sweep with any other boundary
        // would be a different problem and the disagreement would read as a
        // convergence bug.
        //
        // Both return the ZERO-MEAN solution, which is what makes a direct
        // comparison meaningful: the periodic problem fixes phi only up to an
        // added constant, and without that normalisation the two could both be
        // right and differ everywhere by the same amount.
        //
        // ---- WHY 16x8 AND BELOW, AND WHY THIS FILE DOES NOT ARGUE WITH IT ----
        //
        // P2 scoped Gate 1 to 16x8 on 2026-10-16 FROM A REAL RUN: at width 64
        // the near-DC mode decays by 0.9976 per sweep, so a single 1e-5
        // reduction costs ~4,776 sweeps against ~297 at width 16, and there is
        // an f32 floor of roughly eps x 831 on top.
        //
        // A SIMULATION WRITTEN WHILE SCAFFOLDING THIS FILE SUGGESTED 32x32 WOULD
        // ALSO CLEAR 1e-5, AND THAT SUGGESTION WAS DISCARDED. It was numpy, not
        // this library; the 16x8 figure came from this library. Where the two
        // disagree the measured one wins, and a test that quietly widens a gate
        // on simulated evidence is worse than no test. 32x32 and 64x64 are still
        // exercised -- in group 8, REPORTED rather than gated.
        //
        // The assert here is 1e-5, the gate itself, rather than
        // 15_poisson_iter's looser 5e-5: that file leaves tripwire room for the
        // two sweeps it covers, and this one is checking that red_black has not
        // made things worse than the gate allows.
        {
            const std::pair<vc::image_dim, vc::image_dim> sizes[] = {
                {8, 8}, {16, 8}, {8, 16}};
            for (const auto& [w, h] : sizes) {
                const vc::vc_image f = smooth_field(w, h);
                const vc::vc_image ref = poisson_fft(f);

                for (const auto sweep :
                     {poisson_sweep::jacobi, poisson_sweep::gauss_seidel,
                      poisson_sweep::red_black}) {
                    const auto r = poisson_iter(f, sweep, 40000, 1e-6F);
                    const auto d = rel_max_diff(r.phi, ref);
                    std::cout << "  7: " << w << "x" << h << " sweep "
                              << static_cast<int>(sweep) << " rel " << d
                              << " in " << sweeps_taken(r) << " sweeps\n";
                    expect(r.converged,
                           std::string{"7 reaches a 1e-6 residual at "} +
                               std::to_string(w) + "x" + std::to_string(h));
                    expect(d <= 1e-5,
                           std::string{"7 GATE 1 at "} + std::to_string(w) +
                               "x" + std::to_string(h) + ", sweep " +
                               std::to_string(static_cast<int>(sweep)) +
                               ": agrees with poisson_fft, rel " +
                               std::to_string(d));
                }
            }
        }

        // ---- GROUP 8 -- WHAT HAPPENS ABOVE THE GATE'S SIZE. REPORTED ----
        //
        // 32x32 and 64x64 are outside Gate 1's scope, and this group does NOT
        // assert the gate there. It exists so the shape of the degradation is
        // on the record and runs every build, rather than being a sentence in
        // a plan that nobody re-measures.
        //
        // The bound asserted is 1e-3 -- loose enough that it cannot be read as
        // a competing gate, tight enough that a genuine regression (a sign
        // error, a lost wrap) blows through it. THE PRINTED NUMBERS ARE THE
        // DELIVERABLE; the assert is a tripwire.
        //
        // Both runs stop on a 1e-5 RESIDUAL rather than running to a floor,
        // because the sweep counts above 64x64 are in the thousands and this
        // suite runs in the debug tree.
        //
        // 128x128 was in a first draft and is DROPPED -- it needs roughly 8,000
        // sweeps over four times the pixels, tens of seconds, for a number
        // 64x64 already makes.
        {
            for (const vc::image_dim n : {32U, 64U}) {
                const vc::vc_image f = smooth_field(n, n);
                const auto r =
                    poisson_iter(f, poisson_sweep::red_black, 40000, 1e-5F);
                const auto d = rel_max_diff(r.phi, poisson_fft(f));
                std::cout << "  8: " << n << "x" << n
                          << " OUTSIDE Gate 1's scope -- rel " << d << " after "
                          << sweeps_taken(r) << " sweeps, converged "
                          << (r.converged ? "yes" : "no") << '\n';
                expect(d <= 1e-3,
                       std::string{"8 red_black at "} + std::to_string(n) +
                           "x" + std::to_string(n) +
                           " stays within the reporting tripwire, rel " +
                           std::to_string(d));
            }
        }

        // ---- GROUP 9 -- residual_plot REJECTIONS AND ACCEPTANCE ----
        //
        // ** 9a-9e PASS VACUOUSLY AGAINST A THROWING STUB. 9f AND 9g ARE THE
        // PAIR THAT MAKES THEM MEAN SOMETHING. **
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
                "drawn, not rejected -- poisson_iter documents this case, "
                "and it is also the second half of 9a-9e's pairing");

            const std::vector<residual_series> ragged = {
                {"short", {1.0F, 0.1F}}, {"long", {1.0F, 0.5F, 0.2F, 0.05F}}};
            bool ragged_ok = true;
            try {
                (void)residual_plot(ragged);
            } catch (const std::exception&) {
                ragged_ok = false;
            }
            expect(ragged_ok,
                   "9h series of DIFFERENT LENGTHS are accepted -- channels "
                   "converge at different sweeps and red_black on a "
                   "checkerboard finishes in one, so group 10 produces this");
        }

        // ---- GROUP 10 -- THE PLOT IS DUMPED. THE ARTIFACT S8b OWES ----
        //
        // Three methods on two fields, on one axis: the picture that makes group
        // 3's ratio and group 6's stall legible at a glance. The jacobi curve on
        // the checkerboard is a horizontal line at 1.0 and the red_black curve
        // beside it drops off the bottom after one sweep.
        //
        // The checks here are weak ON PURPOSE. The output is for a human, and
        // asserting pixel values would pin a drawing style rather than a
        // property. What is asserted is that every series survived into the
        // call and that the canvas size was honoured.
        {
            const vc::vc_image smooth = smooth_field(32, 32);
            const vc::vc_image checker = from_rule(32, 32, [](int x, int y) {
                return ((x + y) % 2 == 0) ? -8.0F : 8.0F;
            });

            std::vector<residual_series> series;
            const std::vector<std::pair<std::string, vc::vc_image>> fields = {
                {"smooth", smooth}, {"checkerboard", checker}};
            const std::vector<std::pair<std::string, poisson_sweep>> sweeps = {
                {"jacobi", poisson_sweep::jacobi},
                {"gauss_seidel", poisson_sweep::gauss_seidel},
                {"red_black", poisson_sweep::red_black}};

            for (const auto& [label, f] : fields) {
                for (const auto& [name, sweep] : sweeps) {
                    const auto r = poisson_iter(f, sweep, 400, 1e-5F);
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
            std::cout << "  10: residual plot written under "
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
