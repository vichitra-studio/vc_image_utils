// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// S4 -- gradient descent on a 2-D quadratic bowl, and its acceptance suite.
//
// WHAT THIS CHECKS, AND WHY IT IS BUILT THE WAY IT IS. The contract lives in
// vc/math/vc_optimize.h. This file is the part that would catch a wrong
// implementation, and most groups below exist because some specific wrong
// implementation would otherwise pass.
//
// THE THREE IMPLEMENTATIONS THIS SUITE IS BUILT AGAINST. P3 section I.3 names
// them: "converges to the known minimum within tolerance" is satisfied by code
// containing no gradient descent at all if it (1) returns the closed-form
// solution, (2) is initialised AT the minimum, or (3) has its minimum at the
// origin with a zero-initialised guess. None is a strawman; all three are
// plausible accidents. The answers here:
//
//   (1) is removed by the INTERFACE, not by a gate -- gradient_descent never
//       sees the matrix, so there is nothing to invert. See the header.
//   (2) and (3) are removed by the FIXTURES: both bowls have their minimum
//       OFF the origin, and both runs start far from it. So "did not move"
//       and "arrived" are different numbers, and the sweep in group 3 makes
//       the distinction at four step sizes rather than one.
//
// WHERE EXPECTED VALUES COME FROM, since the suite must not quietly encode the
// scaffolder's arithmetic as the standard. Four sources and no others:
//
//   - BY CONSTRUCTION from the fixtures. Both bowls are built as
//     0.5*(x-c)^T A (x-c), so the minimiser IS c and the minimum value IS
//     exactly 0. Nothing is evaluated and believed.
//   - HAND ARITHMETIC on two integers. Bowl E has trace 8 and determinant 12,
//     so its curvatures are 6 and 2. Bowl H has trace 101 and determinant 100,
//     so its curvatures are 100 and 1. Group 0 checks both against the
//     learner's own eigen_symmetric, which makes group 0 a regression test on
//     S2 as well.
//   - THE DEFINING IDENTITY of the method: one step from x is x - step*grad(x).
//     Group 2 gates that directly. It is section 7.1's content, not X4's.
//   - MEASUREMENT, in group 4 only, by bisection -- and there it is compared
//     against the learner's independently derived formula rather than
//     standing in for it.
//
// WHAT IS NOT IN THIS FILE, DELIBERATELY. The step-size boundary formula. Row
// 75's homework (X4) is to reason about step size, and the boundary is that
// reasoning's output. The suite MEASURES the boundary and asks the derivation
// to agree; it never states it. There is also no 1-D three-step trace with
// expected numbers anywhere here, for the same reason -- that is the hand
// example, and week 10 recorded what it costs when the scaffolder works the
// learner's example for them.
//
// WHY THE EASY BOWL IS ALSO ANISOTROPIC, which looks like a contradiction.
// Section 9's cut-first pin drops the hard bowl if the week runs long, keeping
// "the convex quadratic + the divergence demo". If the kept bowl were
// ISOTROPIC the surviving divergence gate would stop discriminating: on a
// bowl whose two curvatures are equal, a boundary formula built from the
// larger curvature, the smaller one, their mean, or a diagonal entry all give
// the same number and all bracket correctly. The cut would silently remove the
// gate's power along with the bowl. So bowl E is mildly anisotropic
// (curvatures 6 and 2) and ROTATED off the axes, which costs nothing and keeps
// the cut safe. Rotation matters on its own account: on a diagonal matrix the
// diagonal entries ARE the curvatures, which is week 10's accidental-invariant
// lesson waiting to happen, and it would make the eigen_symmetric call
// decorative.
//
// BOTH RUNS START OFF BOTH EIGENVECTOR AXES. Started along an eigenvector, a
// quadratic bowl is a 1-D problem: gradient descent walks straight in, and the
// zigzag gate in group 5 passes vacuously on an implementation that cannot
// zigzag. Group 0 asserts the starting offset has a non-trivial component
// along each eigenvector, as a property of the fixture.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/debug/vc_residual_plot.h"
#include "vc/io/vc_io_stb.h"
#include "vc/math/vc_linalg.h"
#include "vc/math/vc_optimize.h"

namespace {

using vc::math::gd_config;
using vc::math::gd_result;
using vc::math::gd_stop;
using vc::math::vc_mat2;
using vc::math::vc_vec2;

int g_passed = 0;
int g_skipped = 0;
int g_failed = 0;

bool check(bool condition, const std::string& what) {
    if (condition) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

void report(const std::string& what) {
    std::cout << "  . " << what << '\n';
}

// A STUB'S THROW IS NOT AN ANSWER TO ANY QUESTION -- the same helper, and the
// same reason, as 20_harris.cpp. Without it, "you have not written this yet"
// is reported as "your validation is wrong" on every rejection gate at once.
bool is_unwritten(const std::exception& e) {
    return std::string(e.what()).find("is NOT IMPLEMENTED") !=
           std::string::npos;
}

bool rejects(const std::function<void()>& f) {
    try {
        f();
    } catch (const vc::vc_exception& e) {
        return e.code() == vc::vc_error_code::invalid_argument;
    } catch (const std::exception& e) {
        if (is_unwritten(e)) {
            throw;
        }
        return false; // a non-vc_exception throw is never a rejection
    }
    return false;
}

// ---- the fixtures ---------------------------------------------------------
//
// THE OBJECTIVE IS A FIXTURE, NOT LIBRARY CODE, and that placement is a
// decision. A `quadratic_bowl{A, centre}` type in vc_optimize.h would be
// public API whose only caller is this file -- the bar the operator* note in
// vc_linalg.h exists to refuse -- and it would hand S5 a parameterisation it
// cannot use: a least-squares objective is 0.5*||X*theta - y||^2, whose
// minimiser is the answer, so writing it as "A and its centre" would mean
// knowing the answer first. S5 will build its objective as a lambda over the
// design matrix, and watching what shape that wants is section E's
// reconnaissance for the P4 optimizer.
//
// WRITTEN AS 0.5*(x-c)^T A (x-c), SO f(c) IS EXACTLY ZERO. The alternative
// form, 0.5*x^T A x - b^T x, has the same minimiser and a nonzero minimum
// value -- and a loss curve on a LOG axis then flattens onto that nonzero
// floor instead of falling, which hides the whole convergence. (See
// residual_plot's floor-clamping note: a line pinned at the floor is visibly
// degenerate, but a line pinned at an arbitrary constant just looks stalled.)
struct quadratic_bowl {
    vc_mat2 a;
    vc_vec2 centre;

    [[nodiscard]] float value(const vc_vec2& x) const {
        const vc_vec2 d = x - centre;
        const vc_vec2 ad = a * d;
        return 0.5F * vc::math::dot(d, ad);
    }

    [[nodiscard]] vc_vec2 gradient(const vc_vec2& x) const {
        return a * (x - centre);
    }
};

vc_mat2 sym(float p, float q, float r) {
    return vc_mat2{{{{{p, q}}, {{q, r}}}}};
}

// BOWL E -- the easy one, and the one the cut-first pin keeps.
// trace 8, determinant 12 -> curvatures 6 and 2, by hand on two integers.
// Off-axis: the eigenvectors are the diagonals, not the axes.
// Minimum at (3, -2), which is not the origin; the run starts at (-9, 7).
const quadratic_bowl k_bowl_easy{sym(4.0F, 2.0F, 4.0F), vc_vec2{3.0F, -2.0F}};
const vc_vec2 k_start_easy{-9.0F, 7.0F};

// BOWL H -- the ill-conditioned one. trace 101, determinant 100 -> curvatures
// 100 and 1, again by hand. Same rotation, condition number 100 instead of 3.
// Minimum at (-4, 6); the run starts at (8, 11), which is off both diagonals.
const quadratic_bowl k_bowl_hard{sym(50.5F, 49.5F, 50.5F),
                                 vc_vec2{-4.0F, 6.0F}};
const vc_vec2 k_start_hard{8.0F, 11.0F};

vc::math::objective_fn value_of(const quadratic_bowl& q) {
    return [q](const vc_vec2& x) { return q.value(x); };
}

vc::math::gradient_fn grad_of(const quadratic_bowl& q) {
    return [q](const vc_vec2& x) { return q.gradient(x); };
}

gd_result run(const quadratic_bowl& q,
              const vc_vec2& x0,
              float step,
              std::size_t budget,
              float tol) {
    return vc::math::gradient_descent(value_of(q), grad_of(q), x0,
                                      gd_config{.step = step,
                                                .max_iterations = budget,
                                                .gradient_tolerance = tol});
}

// ---- THE LEARNER'S SLOT, AND IT IS A FUNCTION RATHER THAN A CONSTANT -------
//
// Fill this in from X4. One line.
//
// WHAT IS BEING ASKED FOR: the largest fixed step for which this iteration
// still converges on a quadratic bowl, given the bowl's curvature. Such a step
// exists, it is computable, and `e` below is the output of the learner's own
// eigen_symmetric on the bowl's matrix -- which is to say, the curvature, in
// the two directions that matter, already separated out. Nothing else is
// needed.
//
// WHY A FUNCTION AND NOT A `constexpr float k_boundary = ...` SLOT. This tree
// has already lost two scaffolded constant-slots: 19_eigen2x2's group 6 sat
// permanently red until it was removed, and 20_harris's group 6 carried a
// "REPLACE with your own" placeholder beside two correct constants for long
// enough that the scaffolder twice read it as evidence the learner had not
// made the choice. A slot records nothing about whether the thinking happened.
//
// A function does three things a constant cannot. It throws the sentinel, so
// the gate that needs it is filed as SKIPPED and the suite stays honestly
// green rather than teaching you to ignore red output. It applies to BOTH
// bowls, so one formula is checked against two different curvatures and a
// coincidence cannot survive. And it is checked against a MEASUREMENT -- group
// 4 bisects for the boundary on your own gradient_descent -- so a wrong
// formula, or a right formula with the wrong scaling, fails either way.
//
// 0 SKIPPED IS A CLOSE CONDITION FOR WEEK 11. A gate that never runs is not a
// gate that passed; the end-of-run summary prints the count for that reason.
float predicted_step_boundary(const vc::math::vc_eigen_2x2& e) {
    // lambda_major > 0 IS THE WHOLE PRECONDITION: with no positive curvature
    // there is no bowl and no step size converges. An earlier version used
    // vc::math::degenerate_gap_tolerance here -- a bound on the GAP BETWEEN
    // two eigenvalues, borrowed as an absolute floor on one of them. That is
    // H.5.10's defect verbatim: an absolute tolerance applied to a matrix with
    // no natural scale, so scaling A by 1e-6 leaves the same geometry and the
    // same answer while the guard starts rejecting it. (std::abs() of a
    // positive compile-time constant was also a no-op.)
    if (!std::isfinite(e.lambda_major) || e.lambda_major <= 0.0F) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "predicted_step_boundary: lambda_major must be "
                               "finite and non zero");
    }
    return 2 / e.lambda_major;
}

// ---- measurement, used only in group 4 and to pick the sweep's steps ------
//
// Does a run of `budget` steps at `step` run AWAY? Two ways to say yes, and
// both are needed: the contract stops with gd_stop::diverged once a value
// stops being finite, but a step a hair above the boundary can grow for two
// thousand steps without overflowing a float, and that is still divergence.
bool runs_away(const quadratic_bowl& q,
               const vc_vec2& x0,
               float step,
               std::size_t budget) {
    const gd_result r = run(q, x0, step, budget, 0.0F);
    if (r.stop == gd_stop::diverged) {
        return true;
    }
    if (r.loss.empty()) {
        return true; // no history at all: treat as a failure to descend
    }
    return r.loss.back() > r.loss.front();
}

// MEASURED ONCE PER BOWL. Groups 3, 4, 5, 7 and 10 each wanted both
// boundaries, and each call is ~19 doubling probes plus 60 bisections, every
// probe a full run through two std::function indirections -- nine
// recomputations of two fixed numbers.
//
// IT IS NOT A SPEEDUP, AND THE REVIEW THAT SUGGESTED IT GUESSED WRONG ABOUT
// THAT: debug runtime measured 0.61 s before and 0.63 s after, i.e. unchanged.
// The bisection probes are capped at 2000 iterations while groups 3 and 10 run
// budgets of 200000 and 20000, which is where the time actually goes. It stays
// because computing the same constant nine ways is a correctness surface --
// nine chances for the groups to disagree about what the boundary is -- not
// because it is fast.
//
// THE KEY IS THE BOWL ALONE, NOT (bowl, x0). Both fixtures are file-scope
// constants used with one start each, so the two never come apart here; a
// third call site with a different x0 for the same bowl would silently get
// the first one's measurement. The boundary is a property of the bowl's
// curvature and not of where the run begins, so that answer would still be
// right -- but it would be right by accident, so: one start per bowl.
float boundary_of(const quadratic_bowl& q, const vc_vec2& x0);

// A HISTORY THAT IS SAFE TO INDEX. Every content gate below walks path[]
// against loss.size(). An implementation that returns only the endpoint, or
// appends to loss without appending to path -- BOTH named in vc_optimize.h as
// defects group 1 exists to catch -- would read PAST THE END of path here.
// check() is non-fatal by design, so the size gate printing FAILED does not
// stop the next line from running, and under the sanitizer preset ASan then
// aborts the process and takes every later group's output with it. A gate that
// crashes instead of failing has stopped being a gate. Seeding each content
// accumulator with this instead of `true` also keeps a malformed history from
// passing those gates vacuously.
[[nodiscard]] bool history_indexable(const gd_result& r) {
    return !r.loss.empty() && (r.loss.size() == r.path.size());
}

// The boundary, by bisection on the step size. Deliberately knows nothing
// about the bowl except how to run on it.
//
// The bracket is FOUND rather than assumed: start at a step small enough that
// anything converges, double until it runs away, then halve the interval 60
// times. Divergence of a fixed-step iteration on a quadratic is monotone in
// the step, which is what makes bisection valid here -- and that monotonicity
// is itself part of what X4 reasons about.
float empirical_step_boundary(const quadratic_bowl& q, const vc_vec2& x0) {
    constexpr std::size_t k_budget = 2000;
    float lo = 1e-6F;
    float hi = lo;
    while (!runs_away(q, x0, hi, k_budget)) {
        lo = hi;
        hi *= 2.0F;
        if (hi > 1e6F) {
            return hi; // nothing diverges: the caller's gate will say so
        }
    }
    for (int i = 0; i < 60 && (hi - lo) > (1e-5F * lo); ++i) {
        const float mid = 0.5F * (lo + hi);
        if (runs_away(q, x0, mid, k_budget)) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return 0.5F * (lo + hi);
}

float boundary_of(const quadratic_bowl& q, const vc_vec2& x0) {
    static std::vector<std::pair<const quadratic_bowl*, float>> cache;
    for (const auto& e : cache) {
        if (e.first == &q) {
            return e.second;
        }
    }
    const float b = empirical_step_boundary(q, x0);
    cache.emplace_back(&q, b);
    return b;
}

float rel_error(float got, float want) {
    return std::abs(got - want) / std::max(1e-12F, std::abs(want));
}

// ---- the two pictures -----------------------------------------------------

vc::vc_image loss_curves(const std::vector<vc::debug::residual_series>& s) {
    return vc::debug::residual_plot(
        s,
        vc::debug::residual_plot_config{
            .width = 900, .height = 560, .floor = 1e-10F, .log_scale = true});
}

// The bowl as a contour map, with the path drawn over it.
//
// CONTOURS AND NOT A PLAIN HEAT MAP. The thing to see on bowl H is that the
// level sets are long thin ellipses and the path crosses them nearly at right
// angles -- the gradient points across the valley rather than along it, which
// is the whole conditioning lesson. A smooth gradient of brightness does not
// show where a level set is; a line does. The bands are geometric (each one a
// factor of two in loss) because the loss falls geometrically, so linear bands
// would crowd into the first few pixels.
//
// vc::image_dim IS UNSIGNED. The marker loops below use signed int offsets and
// bound-check BEFORE casting, because the obvious `for (vc::image_dim d = -r;
// d <= r; ++d)` wraps -r to about 4.29e9, fails immediately, and draws nothing
// -- silently, with no warning, since every operand has the same unsigned
// type. That defect shipped twice in week 10 (corner_peaks' neighbour sweep,
// and 20_harris's own marker loop).
vc::vc_image contour_with_path(const quadratic_bowl& q,
                               const std::vector<vc_vec2>& path) {
    constexpr vc::image_dim k_n = 480;
    const int lim = static_cast<int>(k_n);

    float lo_x = q.centre.x;
    float hi_x = q.centre.x;
    float lo_y = q.centre.y;
    float hi_y = q.centre.y;
    for (const vc_vec2& p : path) {
        lo_x = std::min(lo_x, p.x);
        hi_x = std::max(hi_x, p.x);
        lo_y = std::min(lo_y, p.y);
        hi_y = std::max(hi_y, p.y);
    }
    // One square window for both axes, so a circular level set looks circular
    // and the hard bowl's ellipses are not an artefact of unequal scaling.
    const float cx = 0.5F * (lo_x + hi_x);
    const float cy = 0.5F * (lo_y + hi_y);
    const float half =
        1.25F * std::max(0.5F, std::max(hi_x - lo_x, hi_y - lo_y) * 0.5F);

    const auto to_world = [&](int px, int py) {
        const float u =
            (static_cast<float>(px) + 0.5F) / static_cast<float>(k_n);
        const float v =
            (static_cast<float>(py) + 0.5F) / static_cast<float>(k_n);
        return vc_vec2{cx - half + (2.0F * half * u),
                       cy - half + (2.0F * half * v)};
    };

    std::vector<float> f(static_cast<std::size_t>(k_n) * k_n, 0.0F);
    float f_max = 0.0F;
    for (int py = 0; py < lim; ++py) {
        for (int px = 0; px < lim; ++px) {
            const float v = q.value(to_world(px, py));
            f[(static_cast<std::size_t>(py) * k_n) +
              static_cast<std::size_t>(px)] = v;
            f_max = std::max(f_max, v);
        }
    }
    const float f_ref = std::max(1e-20F, f_max);
    const auto band = [&](int px, int py) {
        const float v = f[(static_cast<std::size_t>(py) * k_n) +
                          static_cast<std::size_t>(px)];
        if (v <= 0.0F) {
            return -1000;
        }
        return static_cast<int>(std::floor(std::log2(v / f_ref)));
    };

    vc::vc_image_writer out{k_n, k_n, 3, vc::buf_f32{0.0F}};
    for (int py = 0; py < lim; ++py) {
        for (int px = 0; px < lim; ++px) {
            const float v = f[(static_cast<std::size_t>(py) * k_n) +
                              static_cast<std::size_t>(px)];
            // sqrt so the floor of the bowl is not one flat dark blob.
            const float u = std::sqrt(std::max(0.0F, v) / f_ref);
            float rgb[3] = {0.10F + (0.88F * u), 0.12F + (0.70F * u),
                            0.18F + (0.10F * u)};
            const bool edge =
                (px + 1 < lim && band(px, py) != band(px + 1, py)) ||
                (py + 1 < lim && band(px, py) != band(px, py + 1));
            if (edge) {
                for (float& ch : rgb) {
                    ch *= 0.35F;
                }
            }
            const auto ux = static_cast<vc::image_dim>(px);
            const auto uy = static_cast<vc::image_dim>(py);
            for (vc::channel_count c = 0; c < 3; ++c) {
                out.at<vc::buf_f32>(ux, uy, c) =
                    std::clamp(rgb[static_cast<std::size_t>(c)], 0.0F, 1.0F);
            }
        }
    }

    const auto to_pixel = [&](const vc_vec2& p) {
        const float u = (p.x - (cx - half)) / (2.0F * half);
        const float v = (p.y - (cy - half)) / (2.0F * half);
        // MINUS A HALF, because to_world above samples PIXEL CENTRES:
        // u = (px + 0.5) / k_n, whose inverse is px = u*k_n - 0.5. Without it
        // lround sends the centre of pixel px to px + 1, and the path, the
        // start dot and the minimum's cross are all drawn one pixel off the
        // contour field they exist to be read against.
        return std::pair<int, int>{
            static_cast<int>(std::lround((u * static_cast<float>(k_n)) - 0.5F)),
            static_cast<int>(
                std::lround((v * static_cast<float>(k_n)) - 0.5F))};
    };
    const auto plot = [&](int qx, int qy, int r, const float col[3]) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int ax = qx + dx;
                const int ay = qy + dy;
                if (ax < 0 || ax >= lim || ay < 0 || ay >= lim) {
                    continue;
                }
                const auto ux = static_cast<vc::image_dim>(ax);
                const auto uy = static_cast<vc::image_dim>(ay);
                for (vc::channel_count c = 0; c < 3; ++c) {
                    out.at<vc::buf_f32>(ux, uy, c) =
                        col[static_cast<std::size_t>(c)];
                }
            }
        }
    };

    // THE PATH IS DRAWN AS SEGMENTS, NOT AS DOTS, and the first version of
    // this function got that wrong. Plotting only the iterates gives a scatter
    // of isolated points: early steps are huge and land far apart, so the one
    // thing the picture exists to show -- that consecutive steps REVERSE
    // across the valley -- was invisible exactly where it is strongest. Joined
    // up, the sawtooth is unmistakable.
    //
    // Every iterate, and not every tenth: the zigzag IS the consecutive pairs,
    // and thinning them would draw a smooth curve through a sawtooth.
    constexpr float k_path[3] = {0.55F, 0.85F, 1.00F};
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const auto [ax, ay] = to_pixel(path[i]);
        const auto [bx, by] = to_pixel(path[i + 1]);
        const int steps =
            std::max(1, std::max(std::abs(bx - ax), std::abs(by - ay)));
        for (int t = 0; t <= steps; ++t) {
            const float u = static_cast<float>(t) / static_cast<float>(steps);
            plot(ax + static_cast<int>(
                          std::lround(u * static_cast<float>(bx - ax))),
                 ay + static_cast<int>(
                          std::lround(u * static_cast<float>(by - ay))),
                 0, k_path);
        }
    }
    for (const vc_vec2& p : path) {
        const auto [qx, qy] = to_pixel(p);
        plot(qx, qy, 1, k_path);
    }
    // The start, larger, and the true minimum as a cross -- a HOLLOW shape at
    // the minimum would be better but a cross already leaves its own centre
    // pixel visible, which a filled dot would hide.
    constexpr float k_start_col[3] = {1.00F, 1.00F, 1.00F};
    if (!path.empty()) {
        const auto [sx, sy] = to_pixel(path.front());
        plot(sx, sy, 3, k_start_col);
    }
    constexpr float k_min_col[3] = {1.00F, 0.18F, 0.18F};
    const auto [mx, my] = to_pixel(q.centre);
    for (int d = -9; d <= 9; ++d) {
        if (std::abs(d) <= 1) {
            continue; // hollow: the marked pixel stays visible
        }
        plot(mx + d, my, 1, k_min_col);
        plot(mx, my + d, 1, k_min_col);
    }
    return std::move(out).seal();
}

} // namespace

int main() {
    // UNBUFFERED, for the same reason as 20_harris: ctest captures stdout
    // through a pipe, where std::cout is fully buffered, so a throw that
    // aborts the run loses every group header still sitting in the buffer.
    std::cout << std::unitbuf;
    std::cout << std::fixed << std::setprecision(6);

    // ---- stub probe --------------------------------------------------------
    // ONE library function, so one probe -- but the same two-way distinction
    // as 20_harris. The sentinel means "not written yet" and exits GREEN; any
    // other throw on this valid 3-step problem is a real defect and must fail
    // the run here rather than be reported later as a broken rejection gate.
    {
        try {
            (void)run(k_bowl_easy, k_start_easy, 0.05F, 3, 0.0F);
        } catch (const std::exception& e) {
            if (is_unwritten(e)) {
                std::cout << "21_gradient_descent: NOT IMPLEMENTED -- "
                          << e.what() << '\n';
                std::cout << "  0 gates can run until the body exists.\n";
                return EXIT_SUCCESS;
            }
            std::cout << "21_gradient_descent: THREW on a valid 3-step run. "
                         "That is a BUG, not an unwritten stub:\n  "
                      << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    std::cout
        << "21_gradient_descent: implementation present -- running gates.\n";

    const auto run_group = [](const char* name,
                              const std::function<void()>& body) {
        try {
            body();
        } catch (const std::exception& e) {
            const std::string what = e.what();
            if (what.find("is NOT IMPLEMENTED") != std::string::npos) {
                ++g_skipped;
                std::cout << "  group " << name
                          << ": SKIPPED from here -- needs something that is "
                             "not written yet: "
                          << what << '\n';
                return;
            }
            ++g_failed;
            std::cout << "  group " << name << ": THREW on valid input -- "
                      << what << '\n';
        }
    };

    // ---- group 0: the fixtures are what this file claims they are ---------
    //
    // A suite whose fixtures are wrong reports defects that are not there, and
    // week 10 spent a day on exactly that (section H.6.2: a fixture tuned at
    // one parameter setting encodes that setting as an accidental invariant,
    // and four gates broke at once when the setting moved). So the claims in
    // the fixture comments are gated, not asserted.
    //
    // THIS IS ALSO A REGRESSION TEST ON S2. eigen_symmetric is week 10's
    // graded deliverable and this is its first reuse; the curvatures it
    // returns here are known from trace and determinant by hand.
    run_group("0", [&] {
        std::cout << "group 0: the fixtures\n";
        struct named {
            const char* tag;
            const quadratic_bowl& q;
            const vc_vec2& x0;
            float lam_major;
            float lam_minor;
        };
        const named bowls[2] = {{"E", k_bowl_easy, k_start_easy, 6.0F, 2.0F},
                                {"H", k_bowl_hard, k_start_hard, 100.0F, 1.0F}};

        for (const named& b : bowls) {
            const std::string t = std::string("bowl ") + b.tag;
            // Symmetric by construction via sym(), but a matrix that is not
            // symmetric has no real eigenbasis and eigen_symmetric rejects it,
            // so assert the thing the next line depends on.
            check(b.q.a.m[0][1] == b.q.a.m[1][0], t + ": a is symmetric");
            check(b.q.a.m[0][1] != 0.0F,
                  t + ": a is NOT diagonal -- its curvatures are not its "
                      "diagonal entries, and the eigen_symmetric call is not "
                      "decorative");

            const vc::math::vc_eigen_2x2 e = vc::math::eigen_symmetric(b.q.a);
            check(rel_error(e.lambda_major, b.lam_major) < 1e-5F,
                  t + ": major curvature is " + std::to_string(b.lam_major) +
                      " (trace and determinant, by hand)");
            check(rel_error(e.lambda_minor, b.lam_minor) < 1e-5F,
                  t + ": minor curvature is " + std::to_string(b.lam_minor));
            check(e.lambda_minor > 0.0F,
                  t + ": positive definite -- it is a bowl, not a saddle");

            // f(c) == 0 EXACTLY, which is what keeps the log-axis loss curve
            // falling instead of flattening onto a nonzero floor.
            check(b.q.value(b.q.centre) == 0.0F,
                  t + ": the minimum value is exactly 0");
            check(vc::math::norm(b.q.gradient(b.q.centre)) == 0.0F,
                  t + ": the gradient at the centre is exactly 0");

            check(vc::math::norm(b.q.centre) > 1.0F,
                  t + ": the minimum is OFF the origin");
            const vc_vec2 off = b.x0 - b.q.centre;
            check(vc::math::norm(off) > 5.0F,
                  t + ": the run starts far from the minimum");

            // Off BOTH eigenvector axes. Along one, the bowl is a 1-D problem
            // and group 5's zigzag gate passes vacuously.
            const float c_major = std::abs(vc::math::dot(off, e.v_major));
            const float c_minor = std::abs(vc::math::dot(off, e.v_minor));
            check(c_major > 1.0F && c_minor > 1.0F,
                  t +
                      ": the starting offset has a real component along EACH "
                      "eigenvector (" +
                      std::to_string(c_major) + ", " + std::to_string(c_minor) +
                      ")");
        }
        report("bowl E condition number 3, bowl H condition number 100");
    });

    // ---- group 1: the shape of the history --------------------------------
    //
    // Section I.3's "gate the path, not the endpoint" starts here, with the
    // weakest version of it: the path has to EXIST and be indexed the way the
    // header says. An implementation that records after the step instead of
    // before loses f(x0) and is off by one everywhere; one that returns only
    // the endpoint makes row 75's two pictures impossible.
    run_group("1", [&] {
        std::cout << "group 1: the history's shape\n";
        constexpr std::size_t k_budget = 25;
        const gd_result r =
            run(k_bowl_easy, k_start_easy, 0.05F, k_budget, 0.0F);

        check(r.loss.size() == r.iterations + 1,
              "loss.size() == iterations + 1");
        check(r.path.size() == r.iterations + 1,
              "path.size() == iterations + 1");
        check(r.iterations == k_budget,
              "with gradient_tolerance 0 it uses the whole budget");
        check(r.stop == gd_stop::iteration_budget,
              "and says so: stop == iteration_budget");

        const bool g1_ok = history_indexable(r);
        check(g1_ok && r.path.front().x == k_start_easy.x &&
                  r.path.front().y == k_start_easy.y,
              "path[0] IS x0, bit for bit");
        check(g1_ok && r.loss.front() == k_bowl_easy.value(k_start_easy),
              "loss[0] IS f(x0) -- the value BEFORE any step");
        check(g1_ok && r.x.x == r.path.back().x && r.x.y == r.path.back().y,
              "x == path.back()");

        bool all_finite = g1_ok;
        bool loss_matches_path = g1_ok;
        for (std::size_t i = 0; i < std::min(r.loss.size(), r.path.size());
             ++i) {
            all_finite = all_finite && std::isfinite(r.loss[i]) &&
                         std::isfinite(r.path[i].x) &&
                         std::isfinite(r.path[i].y);
            loss_matches_path =
                loss_matches_path &&
                rel_error(r.loss[i], k_bowl_easy.value(r.path[i])) < 1e-4F;
        }
        check(all_finite, "every recorded value is finite");
        check(loss_matches_path,
              "loss[k] IS f(path[k]) -- the history is of one run, not two");
    });

    // ---- group 2: it iterates, it moves, and it moves the right way -------
    //
    // THE DEFINING IDENTITY, and it is section 7.1's content rather than X4's:
    // one step from x lands at x - step*grad(x). Gating it is what makes every
    // convergence check below mean "by gradient descent" instead of "somehow".
    // A sign flip, a missing factor of step, a gradient used as a direction
    // without its magnitude, and an update that walks the gradient of the
    // WRONG point all fail here and nowhere else.
    run_group("2", [&] {
        std::cout << "group 2: the update rule itself\n";
        constexpr float k_step = 0.05F;
        const gd_result r = run(k_bowl_easy, k_start_easy, k_step, 4, 0.0F);

        check(r.iterations > 1,
              "it takes more than one step -- not a closed-form shortcut "
              "wearing an iteration count");
        check(r.path.size() > 1 && (r.path[1].x != k_start_easy.x ||
                                    r.path[1].y != k_start_easy.y),
              "the first step actually moves");

        bool every_step_is_the_rule = true;
        for (std::size_t k = 0; k + 1 < r.path.size(); ++k) {
            const vc_vec2 g = k_bowl_easy.gradient(r.path[k]);
            const vc_vec2 want{r.path[k].x - (k_step * g.x),
                               r.path[k].y - (k_step * g.y)};
            const vc_vec2 err = r.path[k + 1] - want;
            const float scale = std::max(1.0F, vc::math::norm(want));
            every_step_is_the_rule = every_step_is_the_rule &&
                                     (vc::math::norm(err) <= 1e-4F * scale);
        }
        check(every_step_is_the_rule,
              "every step k+1 is path[k] - step*grad(path[k])");
    });

    // ---- group 3: it converges from far away, on both bowls, at four steps -
    //
    // SWEPT, because section H.6.2's lesson is that a fixture tuned at one
    // setting encodes that setting. One step size would let a scaling error in
    // the update hide inside the tolerance; four, on two bowls whose
    // curvatures differ by a factor of fifty, will not.
    //
    // The steps are expressed as fractions of the MEASURED boundary, so this
    // group needs no formula and no learner input. The monotone claims are
    // expected to hold for every step strictly inside the boundary; WHY they
    // hold is X4's to explain, and this file does not.
    run_group("3", [&] {
        std::cout << "group 3: convergence, swept over step and conditioning\n";
        constexpr float k_fracs[4] = {0.1F, 0.3F, 0.5F, 0.9F};
        struct named {
            const char* tag;
            const quadratic_bowl& q;
            const vc_vec2& x0;
        };
        const named bowls[2] = {{"E", k_bowl_easy, k_start_easy},
                                {"H", k_bowl_hard, k_start_hard}};

        for (const named& b : bowls) {
            const float boundary = boundary_of(b.q, b.x0);
            const float start_dist = vc::math::norm(b.x0 - b.q.centre);
            for (const float frac : k_fracs) {
                const std::string t = std::string("bowl ") + b.tag + " at " +
                                      std::to_string(frac) +
                                      " of the measured boundary";
                const gd_result r =
                    run(b.q, b.x0, frac * boundary, 200000, 1e-6F);

                check(r.stop == gd_stop::gradient_small,
                      t + ": stops on the gradient test, not the budget");
                const float err = vc::math::norm(r.x - b.q.centre);
                check(err <= 1e-2F * start_dist,
                      t + ": arrives at the known minimum (relative error " +
                          std::to_string(err / start_dist) + ")");

                bool loss_down = history_indexable(r);
                bool dist_down = loss_down;
                for (std::size_t k = 0;
                     k + 1 < std::min(r.loss.size(), r.path.size()); ++k) {
                    loss_down =
                        loss_down && (r.loss[k + 1] <=
                                      (r.loss[k] * (1.0F + 1e-5F)) + 1e-12F);
                    const float d0 = vc::math::norm(r.path[k] - b.q.centre);
                    const float d1 = vc::math::norm(r.path[k + 1] - b.q.centre);
                    dist_down = dist_down && (d1 <= (d0 * (1.0F + 1e-5F)));
                }
                check(loss_down, t + ": the loss never goes up");
                check(dist_down,
                      t + ": the distance to the minimum never goes up");
                report(t + ": " + std::to_string(r.iterations) + " steps");
            }
        }
    });

    // ---- group 4: the step-size boundary, measured and then derived -------
    //
    // THIS IS THE GROUP THE WEEK IS FOR, and it is built as a CONSTRUCTION
    // rather than a consequence (section H.6.3). "It blows up at a large
    // enough step" is satisfied by almost any broken code; what is not is a
    // bracket around a specific number that two independent routes agree on.
    //
    //   - MEASURED: bisection on the step size, using the learner's own
    //     gradient_descent and nothing else.
    //   - BRACKETED: 0.95 of that measurement converges, 1.05 diverges. The
    //     bracket is the claim that the boundary is sharp, which is what makes
    //     it worth deriving.
    //   - DERIVED: predicted_step_boundary(), from X4, on the SAME two bowls.
    //     Skipped until written, and 0 skipped is a week-11 close condition.
    run_group("4", [&] {
        std::cout << "group 4: the step-size boundary\n";
        struct named {
            const char* tag;
            const quadratic_bowl& q;
            const vc_vec2& x0;
        };
        const named bowls[2] = {{"E", k_bowl_easy, k_start_easy},
                                {"H", k_bowl_hard, k_start_hard}};

        float measured[2] = {0.0F, 0.0F};
        for (std::size_t i = 0; i < 2; ++i) {
            const named& b = bowls[i];
            const std::string t = std::string("bowl ") + b.tag;
            measured[i] = boundary_of(b.q, b.x0);
            report(t + ": measured boundary " + std::to_string(measured[i]));

            check(measured[i] > 0.0F && measured[i] < 1e5F,
                  t + ": a finite boundary exists at all");
            check(!runs_away(b.q, b.x0, 0.95F * measured[i], 4000),
                  t + ": converges at 0.95 of the measured boundary");
            check(runs_away(b.q, b.x0, 1.05F * measured[i], 4000),
                  t + ": diverges at 1.05 of it -- the boundary is sharp");
        }

        // The derivation, against the measurement. Note what is NOT asserted:
        // nothing here says what the formula is, and nothing says which bowl
        // property it should use.
        for (std::size_t i = 0; i < 2; ++i) {
            const named& b = bowls[i];
            const std::string t = std::string("bowl ") + b.tag;
            const float got =
                predicted_step_boundary(vc::math::eigen_symmetric(b.q.a));
            report(t + ": X4 predicts " + std::to_string(got) +
                   ", measurement says " + std::to_string(measured[i]));
            check(rel_error(got, measured[i]) < 0.02F,
                  t + ": the derived boundary matches the measured one to 2%");
        }
    });

    // ---- group 5: conditioning, as two numbers ----------------------------
    //
    // Row 75 asks for "the path zigzags (visible in the contour plot) and
    // converges slower". The contour plot is group 6; a picture is not a gate.
    // These are the two numbers behind it.
    //
    // SLOWER: at the same FRACTION of each bowl's own boundary, the
    // ill-conditioned bowl needs far more steps. Comparing at the same
    // fraction rather than the same absolute step is what makes this a
    // statement about conditioning instead of about step size.
    //
    // THIS GATE IS A MEASUREMENT OF A SENTENCE IN THE READING. Deisenroth's
    // Remark at the end of section 7.1.1 (book 229): the speed of convergence
    // depends on the CONDITION NUMBER, "the ratio of the most curved direction
    // versus the least curved direction", and "poorly conditioned problems are
    // long, thin valleys: they are very curved in one direction, but very flat
    // in the other". Bowl E is 3, bowl H is 100, and 21_path_zigzag.png is
    // that long thin valley drawn.
    //
    // He defines it with SINGULAR values (his section 4.5), not eigenvalues.
    // The two agree here only because both bowls are symmetric positive
    // definite; do not carry that equivalence to a general matrix.
    //
    // ZIGZAG, AS A SIGN RATHER THAN AS A SHAPE: consecutive steps point in
    // opposite-ish directions, so their dot product is negative. THE GATE IS
    // DELIBERATELY PAIRED -- the same bowl, at a tenth of the step, must NOT
    // zigzag. One assertion without the other is satisfied by an
    // implementation that always zigzags, or never does. What makes the sign
    // flip at one step size and not the other is X4's account to give.
    run_group("5", [&] {
        std::cout << "group 5: the conditioning lesson, as numbers\n";
        const float be = boundary_of(k_bowl_easy, k_start_easy);
        const float bh = boundary_of(k_bowl_hard, k_start_hard);

        const gd_result re =
            run(k_bowl_easy, k_start_easy, 0.5F * be, 200000, 1e-6F);
        const gd_result rh =
            run(k_bowl_hard, k_start_hard, 0.5F * bh, 200000, 1e-6F);
        report("steps to the same relative gradient: bowl E " +
               std::to_string(re.iterations) + ", bowl H " +
               std::to_string(rh.iterations));
        check(rh.iterations > 10 * re.iterations,
              "the condition-100 bowl needs more than ten times the steps of "
              "the condition-3 bowl, at the same fraction of its own boundary");

        const auto consecutive_dots = [](const gd_result& r,
                                         std::size_t pairs) {
            std::vector<float> d;
            for (std::size_t k = 0; k + 2 < r.path.size() && d.size() < pairs;
                 ++k) {
                const vc_vec2 s0 = r.path[k + 1] - r.path[k];
                const vc_vec2 s1 = r.path[k + 2] - r.path[k + 1];
                d.push_back(vc::math::dot(s0, s1));
            }
            return d;
        };

        const gd_result zig =
            run(k_bowl_hard, k_start_hard, 0.9F * bh, 60, 0.0F);
        const std::vector<float> zd = consecutive_dots(zig, 10);
        check(zd.size() == 10, "ten consecutive step pairs to look at");
        bool all_neg = !zd.empty();
        for (const float d : zd) {
            all_neg = all_neg && (d < 0.0F);
        }
        check(all_neg,
              "near the boundary on bowl H, consecutive steps oppose each "
              "other -- that IS the zigzag");

        const gd_result smooth =
            run(k_bowl_hard, k_start_hard, 0.1F * bh, 60, 0.0F);
        const std::vector<float> sd = consecutive_dots(smooth, 40);
        bool all_pos = !sd.empty();
        for (const float d : sd) {
            all_pos = all_pos && (d > 0.0F);
        }
        check(all_pos,
              "at a tenth of the boundary the SAME bowl does not zigzag -- so "
              "the gate above is about the step, not about the fixture");
    });

    // ---- group 6: the stop criteria, including the degenerate start -------
    run_group("6", [&] {
        std::cout << "group 6: stopping\n";
        // Starting AT the minimum. The header pins this case because the
        // natural relative test divides by ||grad f(x0)||, which is exactly 0
        // here. It is also section I.3's second strawman seen from the other
        // side: this is the ONE run where "did not move" is the right answer,
        // which is why no convergence gate above starts here.
        const gd_result at_min =
            run(k_bowl_hard, k_bowl_hard.centre, 0.01F, 500, 1e-5F);
        check(at_min.iterations == 0,
              "started at the minimum: 0 steps, no pointless move");
        check(at_min.stop == gd_stop::gradient_small,
              "and it says gradient_small rather than a budget stop");
        check(at_min.loss.size() == 1 && at_min.loss.front() == 0.0F,
              "the history is one entry, the exact minimum value");

        // A budget smaller than the problem needs.
        const gd_result squeezed =
            run(k_bowl_hard, k_start_hard, 1e-4F, 5, 1e-5F);
        check(squeezed.iterations == 5 &&
                  squeezed.stop == gd_stop::iteration_budget,
              "a 5-step budget on a slow run stops at 5 and says so");

        // A tolerance of 1 stops at the first test: the gradient at x0 is
        // trivially within one times itself.
        const gd_result instant =
            run(k_bowl_easy, k_start_easy, 0.05F, 500, 1.0F);
        check(instant.stop == gd_stop::gradient_small &&
                  instant.iterations <= 1,
              "gradient_tolerance 1 is satisfied immediately");

        // THE TOLERANCE IS RELATIVE, SO THE SAME BOWL AT A DIFFERENT SCALE
        // MUST BEHAVE IDENTICALLY. Scaling A by a constant scales every
        // gradient by it and changes no geometry; with the step scaled back
        // the inverse way, every iterate is the same point and the run must
        // take the same number of steps and stop for the same reason.
        //
        // THIS GATE EXISTS BECAUSE THE OBVIOUS LENGTH BREAKS IT. A norm
        // computed as sqrt(x*x + y*y) forms a square that underflows to zero
        // for small-but-perfectly-representable components, so the reference
        // norm reads as exactly 0, the stationary-start carve-out fires at
        // iteration 0, and the function reports gradient_small WITHOUT EVER
        // MOVING -- identical geometry, opposite answer. 1e-26 is chosen so
        // the components are ordinary floats and only their SQUARES are not.
        constexpr float k_tiny = 1e-26F;
        const quadratic_bowl tiny_e{
            sym(4.0F * k_tiny, 2.0F * k_tiny, 4.0F * k_tiny),
            k_bowl_easy.centre};
        const gd_result big = run(k_bowl_easy, k_start_easy, 0.1F, 500, 1e-5F);
        const gd_result small =
            run(tiny_e, k_start_easy, 0.1F / k_tiny, 500, 1e-5F);
        check(big.iterations == small.iterations && big.stop == small.stop,
              "the same bowl scaled by 1e-26 takes the same steps and stops "
              "the same way -- the tolerance is relative in fact, not just on "
              "paper");
        check(small.iterations > 1,
              "and it actually ran: a scaled bowl is not 'already converged'");

        // THE LAST ALLOWED STEP STILL COUNTS AS CONVERGENCE. A loop that
        // tests only before stepping skips the point it lands on, so a run
        // reaching the tolerance on its final permitted step gets reported as
        // iteration_budget with the converged answer already in hand.
        const gd_result loose =
            run(k_bowl_hard, k_start_hard, 0.01F, 5000, 1e-5F);
        const gd_result exact =
            run(k_bowl_hard, k_start_hard, 0.01F, loose.iterations, 1e-5F);
        check(loose.stop == gd_stop::gradient_small &&
                  exact.stop == gd_stop::gradient_small &&
                  exact.iterations == loose.iterations,
              "a budget of exactly the steps needed still reports "
              "gradient_small, not iteration_budget");
    });

    // ---- group 7: divergence, and it has to stay drawable -----------------
    //
    // The contract says a diverging run stops with gd_stop::diverged and keeps
    // its history FINITE, because vc::debug::residual_plot rejects a
    // non-finite value anywhere in any series -- so an implementation that
    // appends inf produces a run that cannot be plotted, and row 75's
    // divergence demo dies at the plotting call rather than at a gate. The
    // last check here IS the plotting call.
    run_group("7", [&] {
        std::cout << "group 7: divergence\n";
        const float bh = boundary_of(k_bowl_hard, k_start_hard);
        const gd_result d =
            run(k_bowl_hard, k_start_hard, 3.0F * bh, 400, 0.0F);

        check(d.stop == gd_stop::diverged,
              "well past the boundary it reports diverged");
        check(d.iterations + 1 == d.loss.size() &&
                  d.iterations + 1 == d.path.size(),
              "the history is still iterations + 1 long");
        bool finite = history_indexable(d);
        for (std::size_t i = 0; i < std::min(d.loss.size(), d.path.size());
             ++i) {
            finite = finite && std::isfinite(d.loss[i]) &&
                     std::isfinite(d.path[i].x) && std::isfinite(d.path[i].y);
        }
        check(finite,
              "and every recorded value is finite -- the non-finite iterate is "
              "NOT appended");
        check(std::isfinite(d.x.x) && std::isfinite(d.x.y),
              "x is the last finite iterate");
        check(d.loss.size() > 2 && d.loss.back() > d.loss.front(),
              "the loss RISES -- which is the picture the demo is for");

        std::vector<vc::debug::residual_series> s;
        s.push_back({"bowl H, 3x the boundary", d.loss});
        const vc::vc_image plot = loss_curves(s);
        check(plot.width() > 0,
              "and residual_plot accepts the history, so the demo can be "
              "drawn");
    });

    // ---- group 8: rejections ----------------------------------------------
    run_group("8", [&] {
        std::cout << "group 8: what it refuses\n";
        const vc::math::objective_fn f = value_of(k_bowl_easy);
        const vc::math::gradient_fn g = grad_of(k_bowl_easy);
        const auto call = [&](const vc::math::objective_fn& ff,
                              const vc::math::gradient_fn& gg,
                              const vc_vec2& x0, const gd_config& c) {
            return [&ff, &gg, x0, c] {
                (void)vc::math::gradient_descent(ff, gg, x0, c);
            };
        };
        // VALID, AND IT NOW HAS TO BE SPELLED OUT. With no usable default
        // on gd_config::step, `gd_config{}` carries step 0 -- which is itself
        // rejected. The two gates below that use `ok` would then pass because
        // the STEP was refused, never reaching the empty-callable or
        // non-finite-x0 check they are named for: a vacuous pass, and the
        // exact defect class this file exists to refuse.
        const gd_config ok{.step = 0.05F};
        const vc::math::objective_fn none_f;
        const vc::math::gradient_fn none_g;

        check(rejects(call(none_f, g, k_start_easy, ok)),
              "an empty objective is rejected, not called");
        check(rejects(call(f, none_g, k_start_easy, ok)),
              "an empty gradient is rejected");
        check(rejects(call(f, g, k_start_easy,
                           gd_config{.step = 0.0F,
                                     .max_iterations = 10,
                                     .gradient_tolerance = 0.0F})),
              "step 0 is rejected -- it is 'never moves' as a configuration");
        check(rejects(call(f, g, k_start_easy,
                           gd_config{.step = -0.05F,
                                     .max_iterations = 10,
                                     .gradient_tolerance = 0.0F})),
              "a negative step is rejected -- that is gradient ASCENT");
        check(rejects(call(
                  f, g, k_start_easy,
                  gd_config{.step = std::numeric_limits<float>::quiet_NaN(),
                            .max_iterations = 10,
                            .gradient_tolerance = 0.0F})),
              "a NaN step is rejected");
        check(rejects(
                  call(f, g, k_start_easy,
                       gd_config{.step = std::numeric_limits<float>::infinity(),
                                 .max_iterations = 10,
                                 .gradient_tolerance = 0.0F})),
              "an infinite step is rejected");
        check(rejects(call(f, g, k_start_easy,
                           gd_config{.step = 0.05F,
                                     .max_iterations = 0,
                                     .gradient_tolerance = 0.0F})),
              "a zero budget is rejected -- an empty history says nothing");
        check(rejects(call(
                  f, g, vc_vec2{std::numeric_limits<float>::quiet_NaN(), 0.0F},
                  ok)),
              "a non-finite x0 is rejected");
        check(rejects(call(f, g, k_start_easy,
                           gd_config{.step = 0.05F,
                                     .max_iterations = 10,
                                     .gradient_tolerance = -1.0F})),
              "a negative gradient_tolerance is rejected");

        // A FINITE x0 DOES NOT MAKE f(x0) FINITE. At 1e20 the bowl's value
        // overflows float32 while every component of x0 is an ordinary
        // number. Seeding that unchecked puts a non-finite value in loss[0]
        // on a run that never diverges, which is the one thing the DIVERGENCE
        // MUST STAY PLOTTABLE contract forbids -- residual_plot would then
        // refuse a history the function reported as a success.
        check(rejects(call(f, g, vc_vec2{1e20F, 1e20F}, ok)),
              "an x0 where f(x0) overflows is rejected, not reported as a "
              "successful run");
        check(rejects(
                  call(f, g, k_start_easy,
                       gd_config{.step = 0.05F,
                                 .max_iterations = 10,
                                 .gradient_tolerance =
                                     std::numeric_limits<float>::quiet_NaN()})),
              "a NaN gradient_tolerance is rejected");
        report("a bowl it cannot solve and a step that is too large are NOT "
               "rejected -- those are results, reported through gd_stop");
    });

    // ---- group 9: no hidden state -----------------------------------------
    run_group("9", [&] {
        std::cout << "group 9: determinism\n";
        const gd_result a = run(k_bowl_hard, k_start_hard, 0.01F, 200, 0.0F);
        const gd_result b = run(k_bowl_hard, k_start_hard, 0.01F, 200, 0.0F);
        bool identical = history_indexable(a) && history_indexable(b) &&
                         a.loss.size() == b.loss.size() &&
                         a.iterations == b.iterations && a.stop == b.stop;
        for (std::size_t i = 0; identical && i < a.loss.size(); ++i) {
            identical = (a.loss[i] == b.loss[i]) &&
                        (a.path[i].x == b.path[i].x) &&
                        (a.path[i].y == b.path[i].y);
        }
        check(identical, "two identical calls produce bit-identical histories");
    });

    // ---- group 10: the two pictures (report only) -------------------------
    //
    // NOT GATES, and that is deliberate -- 19_eigen2x2 set the precedent for
    // reporting a number the suite has no opinion about. "The path plot is
    // readable" is row 75's criterion and it is not expressible as an
    // assertion; what IS checked is that the files get written and that the
    // numbers printed beside them are the ones the pictures claim.
    //
    // LOOK AT THEM. 21_path_hard.png is the whole conditioning lesson in one
    // frame: level sets as long thin ellipses, and the path crossing them
    // almost at right angles because the gradient points across the valley
    // rather than along it.
    run_group("10", [&] {
        std::cout << "group 10: the dumps (report only)\n";
        const auto png =
            vc::io::write_config{.format = vc::io::vc_image_format::png};
        vc::io::stb_image_writer writer;
        const std::string out = std::string(VC_EXAMPLES_OUTPUT_DIR);

        const float be = boundary_of(k_bowl_easy, k_start_easy);
        const float bh = boundary_of(k_bowl_hard, k_start_hard);

        const gd_result easy =
            run(k_bowl_easy, k_start_easy, 0.5F * be, 20000, 1e-6F);
        const gd_result hard =
            run(k_bowl_hard, k_start_hard, 0.5F * bh, 20000, 1e-6F);
        const gd_result zig =
            run(k_bowl_hard, k_start_hard, 0.9F * bh, 120, 0.0F);
        const gd_result div =
            run(k_bowl_hard, k_start_hard, 3.0F * bh, 400, 0.0F);

        std::vector<vc::debug::residual_series> conv;
        conv.push_back({"bowl E (condition 3)", easy.loss});
        conv.push_back({"bowl H (condition 100)", hard.loss});
        writer.write(out + "/21_loss_curves.png", loss_curves(conv), png);

        std::vector<vc::debug::residual_series> blow;
        blow.push_back({"bowl H, half the boundary", hard.loss});
        blow.push_back({"bowl H, three times the boundary", div.loss});
        writer.write(out + "/21_loss_divergence.png", loss_curves(blow), png);

        writer.write(out + "/21_path_easy.png",
                     contour_with_path(k_bowl_easy, easy.path), png);
        writer.write(out + "/21_path_hard.png",
                     contour_with_path(k_bowl_hard, hard.path), png);
        writer.write(out + "/21_path_zigzag.png",
                     contour_with_path(k_bowl_hard, zig.path), png);

        report("21_loss_curves.png       bowl E in " +
               std::to_string(easy.iterations) + " steps, bowl H in " +
               std::to_string(hard.iterations));
        report("21_loss_divergence.png   loss " +
               std::to_string(div.loss.front()) + " -> " +
               std::to_string(div.loss.back()) + " over " +
               std::to_string(div.iterations) + " steps");
        report("21_path_easy.png         " + std::to_string(easy.path.size()) +
               " iterates");
        report("21_path_hard.png         " + std::to_string(hard.path.size()) +
               " iterates");
        report("21_path_zigzag.png       " + std::to_string(zig.path.size()) +
               " iterates at 0.9 of the boundary");
        std::cout << "  . written to " << out << '\n';
    });

    std::cout << "21_gradient_descent: " << g_passed << " / "
              << (g_passed + g_failed) << " checks passed\n";
    if (g_skipped > 0) {
        std::cout << "   (" << g_skipped
                  << " group(s) SKIPPED -- something they need is not written "
                     "yet. A gate that never ran is not a gate that passed, "
                     "and 0 skipped is a week-11 close condition.)\n";
    }
    return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
