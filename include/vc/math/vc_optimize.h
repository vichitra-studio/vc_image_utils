// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cmath> // std::isfinite -- used by gd_config below
#include <cstddef>
#include <functional>
#include <vector>

#include "vc/core/vc_scalar.h" // vc::float32 -- leaf header
#include "vc/math/vc_linalg.h" // vc_vec2

namespace vc::math {

// S4 -- GRADIENT DESCENT ON A 2-D OBJECTIVE.
//
// ---- WHY IT IS HERE AND NOT IN pixelops -------------------------------------
//
// P3 section F.3 places an operation by asking "does it take a vc_image?".
// This one takes two callables and a point, so it is vc::math, beside
// vc_linalg and vc_dft. (Section I.2 of the phase doc says "a
// pixelops-sibling header", which was loose wording for "a sibling of the
// pixelops headers"; F.3's rule is the one that decides, and it says
// vc::math.)
//
// Whether vc::math eventually grows a vc::optim leaf is section E's
// reconnaissance, to be answered by having built this and lsq_fit -- not now.
//
// ---- THE OBJECTIVE ARRIVES AS TWO CALLABLES, AND THAT IS A GATE -------------
//
// The obvious alternative is to pass the quadratic itself -- its matrix and
// its minimiser -- and let the optimizer work on that. DO NOT, and the reason
// is not taste. P3 section I.3 names three implementations that satisfy
// "converges to the known minimum within tolerance" while containing no
// gradient descent at all, and the first is "returns the closed-form
// solution". If this function were handed the matrix A, that implementation
// is four lines and passes every convergence check in the suite.
//
// Handed only `f` and `grad`, IT CANNOT BE WRITTEN. There is no A to invert.
// The interface removes the defect instead of testing for it, which is the
// same preference -- a construction over a consequence -- that week 10's
// section H.6.3 arrived at the hard way.
//
// The cost is std::function's type erasure: one indirect call per evaluation,
// two per iteration. For a toy on a 2-D bowl that is nothing. If a later phase
// wants this in a hot loop it becomes a template and moves into the header;
// that is a P4 decision and it is booked, not pre-empted.
//
// ---- 2-D, DELIBERATELY ------------------------------------------------------
//
// vc_vec2 and not a span, for three reasons that all point the same way:
//
//   - the path over a CONTOUR PLOT is half of row 75's deliverable, and a
//     contour plot of anything above two dimensions is a different exercise;
//   - the step-size boundary is set by the bowl's curvature, and the curvature
//     of a 2-D quadratic is a 2x2 symmetric matrix -- which is exactly what
//     S2's eigen_symmetric reads. The chain S2 -> S4 only exists at 2x2;
//   - a line fit is two parameters, so S5's cross-check -- running THIS
//     optimizer on the least-squares objective -- fits without widening
//     anything.
//
// THAT LAST POINT PRESUPPOSES THE ANSWER TO SECTION I.5(a): a line fit, not an
// affine fit. An affine fit is six parameters, there is no general solver in
// this tree, and an affine lsq_fit therefore CANNOT use this optimizer for its
// cross-check. If that scope question is answered the other way, this
// signature is the thing that has to change first.

// f: R^2 -> R, and its gradient. Both must be finite-valued wherever the
// iteration actually goes; see `diverged` below for what happens when they
// are not.
using objective_fn = std::function<vc::float32(const vc_vec2&)>;
using gradient_fn = std::function<vc_vec2(const vc_vec2&)>;

// WHY THE RUN STOPPED. Three outcomes, and the suite gates on which one
// occurred -- not merely on the final point. "Arrived at the minimum" and
// "ran out of budget two steps in" produce similar-looking endpoints on an
// easy bowl and mean completely different things.
enum class gd_stop {
    // ||grad f(x)|| fell to or below config.gradient_tolerance times the
    // gradient norm AT x0. See the tolerance note on gd_config.
    gradient_small,
    // config.max_iterations steps were taken and the gradient test never
    // passed. NOT a failure: the ill-conditioned bowl legitimately ends here
    // at a small step, and that is the conditioning lesson.
    iteration_budget,
    // The next iterate, or its loss, was not finite. The step was too large
    // for this bowl's curvature. See "DIVERGENCE MUST STAY PLOTTABLE".
    diverged
};

struct gd_config {
    // FIXED. It is not adapted, not backtracked, and there is no momentum
    // term. This is a pin, not an omission.
    //
    // It is worth stating because the reading lands immediately before this
    // build and describes both alternatives. VERIFIED 2026-11-25 against the
    // page itself, not recalled:
    //
    //   - section 7.1.1 Step-size (book 229) gives the two adaptive
    //     heuristics, citing Toussaint 2012: "When the function value
    //     increases after a gradient step, the step-size was too large. Undo
    //     the step and decrease the step-size." And: "When the function value
    //     decreases the step could have been larger. Try to increase the
    //     step-size."
    //   - section 7.1.2 is Momentum, announced on that same page as "a method
    //     that smoothes out erratic behavior of gradient updates and dampens
    //     oscillations".
    //
    // NEITHER IS BUILT HERE, and the second sentence above is the clearest
    // statement of why. "Dampens oscillations" is group 5's zigzag, described
    // from the other side -- momentum is the book's ANSWER to the thing this
    // slice exists to make you look at. Smoothing it away before you have seen
    // it, measured its sign, and watched it in 21_path_zigzag.png would remove
    // the lesson and keep the convenience. Likewise an adapting step
    // essentially never diverges, which deletes the divergence demo and makes
    // the step-size boundary unobservable. Build the version whose failure is
    // visible first; momentum belongs to whoever picks this up at P4.
    //
    // The margin note on that page is worth carrying forward too: "The
    // step-size is also called the learning rate." That is the word Karpathy
    // L3 and L4 will use for this same number.
    //
    // THERE IS DELIBERATELY NO USABLE DEFAULT. 0 is rejected by the validator
    // below, so a caller who does not state a step gets a named error instead
    // of a number nobody chose.
    //
    // An earlier draft defaulted this to 0.1, which is the exact thing this
    // slice argues against. Eq 7.6 makes the step an INPUT, like x0 -- the
    // algorithm cannot choose it and does not try -- and the sentence right
    // after that equation is "for SUITABLE step-size gamma_i", with "suitable"
    // left undefined for the rest of the section. A library that quietly
    // supplies 0.1 has answered a question the book deliberately hands to the
    // caller, and has answered it with a number that is right for nothing in
    // particular. For a quadratic the caller can compute a correct one from
    // the curvature; for anything else the caller has to measure it. Either
    // way it is the caller's to state.
    vc::float32 step = 0.0F;

    // Hard cap on the number of steps TAKEN. Reaching it is a legitimate
    // outcome (gd_stop::iteration_budget), not an error.
    std::size_t max_iterations = 500;

    // RELATIVE TO THE GRADIENT NORM AT x0, and this is spelled out rather
    // than left to the reader because an absolute tolerance scaled against
    // the wrong quantity is this tree's recurring defect -- eigen2x2 shipped
    // it twice in one week (section H.5.10: the asymmetry tolerance and the
    // degenerate-gap tolerance were both absolute, and both wrong on a bowl
    // whose entries are large).
    //
    // A quadratic bowl has no natural scale. Scaling A by 1000 scales every
    // gradient by 1000 without changing the geometry or the answer, so a
    // fixed absolute threshold stops early on a shallow bowl and never on a
    // steep one. The test is therefore:
    //
    //     ||grad f(x_k)|| <= gradient_tolerance * ||grad f(x0)||
    //
    // 0 IS LEGAL. The test becomes ||grad f(x_k)|| <= 0, which a nonzero
    // gradient can never satisfy, so in practice it means "run the whole
    // budget" and the suite uses it to measure iteration counts a tolerance
    // would truncate.
    //
    // IT IS NOT A PROMISE THAT THE BUDGET IS ALWAYS SPENT, and an earlier
    // wording here said it was. The comparison is <=, so an iterate that lands
    // EXACTLY on the minimum -- reachable, e.g. A = I with step 1 -- makes the
    // gradient exactly 0 and stops with gradient_small before the budget. That
    // <= is required by the stationary-start clause below, so the two clauses
    // cannot both be absolute; this is the one that gives. A run that stops
    // early under tolerance 0 has genuinely finished: there is nowhere to go.
    //
    // ONE EXPLICIT CASE, because it is reachable and the natural code gets it
    // wrong: if ||grad f(x0)|| is exactly 0 -- the caller started AT a
    // stationary point -- the test is satisfied at iteration 0, and the
    // function returns immediately with iterations == 0 and
    // gd_stop::gradient_small. It does not divide by zero and it does not
    // take a pointless step.
    //
    // THAT CARVE-OUT IS WHY THE LENGTH MUST BE A hypot AND NOT A sqrt OF A SUM
    // OF SQUARES. "Exactly 0" has to mean the gradient really is zero. Scale a
    // bowl down far enough -- same geometry, same answer, same step count --
    // and a sqrt(x*x + y*y) length underflows to exactly 0 for a gradient that
    // is nothing of the kind, this clause fires at iteration 0, and the
    // function reports success without moving. vc::math::norm() uses hypot for
    // exactly this reason; see its note in vc_linalg.h.
    vc::float32 gradient_tolerance = 1e-5F;

    // nullptr when this config is usable; otherwise the clause that failed,
    // worded for the caller who has to fix it.
    //
    // NOT a bool. The knowledge of what makes a config valid belongs here, in
    // the struct that defines the fields -- but a bool collapses THREE
    // distinct mistakes into one message, and "configuration is invalid" sends
    // the caller to read the source to find out which field they got wrong.
    // These three are the config's share of the eight rejections listed under
    // WHAT IT REJECTS below; the other five concern f, grad, x0 and the two
    // values f and grad return at x0 -- gradient_descent's arguments and what
    // they evaluate to, not this struct's business.
    const char* validation_error() const {
        if (!std::isfinite(step) || step <= 0.0F) {
            return "config.step must be finite and > 0 -- 0 never moves, and a "
                   "negative step is gradient ascent";
        }
        if (max_iterations == 0) {
            return "config.max_iterations must be > 0 -- there is no run to "
                   "report otherwise";
        }
        if (!std::isfinite(gradient_tolerance) || gradient_tolerance < 0.0F) {
            return "config.gradient_tolerance must be finite and >= 0 -- 0 is "
                   "legal and means run the whole budget";
        }
        return nullptr;
    }
};

// THE WHOLE RUN, NOT THE ANSWER. Returning only the final point would make
// row 75's two deliverables -- the loss-vs-iteration curve and the path over a
// contour plot -- impossible to produce from the outside, and it would make
// section I.3's "gate the path, not the endpoint" unenforceable.
struct gd_result {
    // The last FINITE iterate. Equal to path.back() always, including after
    // divergence.
    vc_vec2 x{.x = 0.0F, .y = 0.0F};

    // loss[k] == f(path[k]). INDEX 0 IS THE STARTING LOSS, f(x0), before any
    // step -- the same convention vc::debug::residual_plot already uses for
    // residual histories, so a loss curve goes straight through it.
    //
    // So loss.size() == path.size() == iterations + 1. An implementation that
    // records after the step instead of before loses f(x0) and is off by one;
    // the suite gates the length and loss[0] separately for that reason.
    std::vector<vc::float32> loss;

    // path[0] == x0.
    std::vector<vc_vec2> path;

    // Steps actually taken.
    std::size_t iterations = 0;

    gd_stop stop = gd_stop::iteration_budget;
};

// A PLAIN DATA CARRIER, like vc_eigen_2x2, and deliberately so. An earlier
// version published seed/update/set_diverged/set_converged/set_budget_overflow
// as public members. They were gradient_descent's own implementation steps on
// a value every caller only ever READS, and exposing them let any caller
// rewrite a run's stop reason or desynchronise `iterations` from loss.size() --
// the invariant this header calls load-bearing. The one appending step that
// maintains that invariant now lives once, in the .cpp, where the loop that
// depends on it can be read beside it.
//
// One of those setters was also dead: `stop` already defaults to
// iteration_budget, so set_budget_overflow() wrote the value it found, and its
// name ("overflow") described an outcome gd_stop does not have.

// ---- DIVERGENCE MUST STAY PLOTTABLE, AND THAT IS A CONTRACT TERM ------------
//
// Row 75 wants the divergence shown, not merely survived, and the tool for
// showing it is vc::debug::residual_plot -- WHICH REJECTS A NON-FINITE VALUE
// ANYWHERE IN ANY SERIES, by design (its header: "NaN means the solver
// diverged or divided by zero, and silently skipping the point hides exactly
// the run worth looking at").
//
// So a diverging run that appends inf to `loss` produces a history that cannot
// be drawn, and the demo dies at the plotting call. The rule instead:
//
//   - evaluate the candidate next point and its loss;
//   - if EITHER is not finite, stop with gd_stop::diverged and DO NOT append
//     it. `loss` and `path` stay entirely finite, `x` is the last good point,
//     and the curve shows the climb right up to the edge;
//   - the run that diverges has a loss history that RISES. That is the
//     picture. An implementation that clamps or bails out silently has
//     nothing to draw.
//
// ---- WHAT IT REJECTS, ALL vc_error_code::invalid_argument, NAMING
//      "gradient_descent" ------------------------------------------------------
//
//   - an empty `f` or an empty `grad` (a default-constructed std::function):
//     calling one is undefined behaviour, and the message a user needs is
//     "you passed nothing", not a crash in the first iteration
//   - config.step not finite, or <= 0: a zero step is the "never moves"
//     implementation expressed as a configuration, and a negative step is
//     gradient ASCENT, which will happily run to the budget and report
//     iteration_budget on a divergent climb
//   - config.max_iterations == 0: there is no run to report, and an empty
//     history is indistinguishable from an optimizer that produced nothing
//   - x0 not finite in either component
//   - config.gradient_tolerance not finite, or < 0
//   - f(x0) not finite. A FINITE x0 DOES NOT MAKE f(x0) FINITE -- at 1e20 the
//     value of an ordinary bowl overflows float32 while both components of x0
//     are ordinary numbers. Seeding that unchecked puts a non-finite value in
//     loss[0] on a run that never diverges, which is exactly what the
//     DIVERGENCE MUST STAY PLOTTABLE section above forbids: residual_plot
//     would refuse a history this function had reported as a success. A start
//     the objective cannot evaluate is misuse, not a result
//   - grad(x0) not finite in either component, for the same reason, and
//     because it is also the scale the whole stop test is measured against
//
// It does NOT reject a bowl it cannot solve, or a step that is too large.
// Those are results, not misuse, and gd_stop is how they are reported.
//
// ---- THE STEP-SIZE BOUNDARY IS NOT STATED HERE, DELIBERATELY ----------------
//
// Too large a step diverges; too small a step crawls. There is a largest step
// for which this iteration still converges on a given quadratic bowl, it is
// COMPUTABLE, and it is fixed by the bowl's CURVATURE -- which for a 2-D
// quadratic is a 2x2 symmetric matrix, i.e. exactly what
// vc::math::eigen_symmetric reads.
//
// Deriving that boundary is row 75's homework (X4: "reason about step size").
// Neither this header nor the acceptance suite states the formula. The suite
// finds the boundary EMPIRICALLY, by bisection on the step size, and then asks
// the derivation to agree with the measurement -- two independent routes to one
// number, which is the pattern S2 used against Eigen and the one week 10
// closed on.
[[nodiscard]] gd_result gradient_descent(const objective_fn& f,
                                         const gradient_fn& grad,
                                         const vc_vec2& x0,
                                         const gd_config& config);

} // namespace vc::math
