// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/math/vc_optimize.h"

#include <cmath>
#include <string>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace {

[[noreturn]] void reject(const char* who, const char* why) {
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           std::string(who) + ": " + why);
}

} // namespace

namespace vc::math {

gd_result gradient_descent(const objective_fn& f,
                           const gradient_fn& grad,
                           const vc_vec2& x0,
                           const gd_config& config) {
    if (!f) {
        reject("gradient_descent", "the objective function f is empty");
    }
    if (!grad) {
        reject("gradient_descent", "the gradient function grad is empty");
    }
    if (const char* why = config.validation_error(); why != nullptr) {
        reject("gradient_descent", why);
    }
    if (!x0.is_valid()) {
        reject("gradient_descent", "x0 must be finite in both components");
    }

    // f AND grad ARE EVALUATED AT x0 BEFORE ANYTHING IS RECORDED, and both
    // results are checked. x0 being finite does not make f(x0) finite, and
    // seeding an unchecked f(x0) is how a non-finite value reaches loss[0] on
    // a run that never diverges -- which breaks the "entirely finite history"
    // contract at entry 0 and makes residual_plot throw on a successful run.
    // A start the objective cannot evaluate is caller misuse, not a result,
    // so it is rejected rather than reported through gd_stop.
    const vc::float32 loss0 = f(x0);
    const vc_vec2 g0 = grad(x0);
    if (!std::isfinite(loss0)) {
        reject("gradient_descent", "f(x0) is not finite");
    }
    if (!g0.is_valid()) {
        reject("gradient_descent", "grad(x0) is not finite");
    }

    // The starting point is history, not a step: loss[0] is f(x0) -- the value
    // computed above, not a second call -- and iterations stays 0 until the
    // first move.
    gd_result result;
    result.x = x0;
    result.loss.push_back(loss0);
    result.path.push_back(x0);

    // NEITHER VECTOR IS reserve()d, AND THAT IS THE CHEAPER CHOICE. The only
    // size known up front is max_iterations, which is a BUDGET and not a
    // prediction: the suite runs budgets of 200000 for runs that finish in 34
    // steps, and the bisection that measures the boundary makes ~79 such runs
    // per bowl. Reserving the budget would allocate megabytes per call to
    // avoid a handful of amortised-O(1) regrowths.
    //
    // The one place that appends, so `iterations`, `loss`, `path` and `x`
    // cannot drift apart. loss.size() == iterations + 1 is maintained HERE.
    const auto push = [&result](const vc_vec2& next, vc::float32 next_loss) {
        result.x = next;
        result.loss.push_back(next_loss);
        result.path.push_back(next);
        ++result.iterations;
    };

    // The stop test is RELATIVE to the gradient at x0: a quadratic bowl has no
    // natural scale, so an absolute threshold stops early on a shallow bowl
    // and never on a steep one. norm() is a hypot, which is what makes "the
    // gradient is exactly 0" mean what it says at every scale.
    const vc::float32 gradient_x0 = norm(g0);

    // grad(x0) IS EVALUATED TWICE ON PURPOSE -- once above to validate it and
    // scale the test, once on the first pass below. Carrying it into the loop
    // would save ONE gradient call PER RUN (not per step: roughly 0.05% of a
    // 2000-iteration run) in exchange for either a loop that re-evaluates at
    // the bottom or a primed variable that makes the first pass read
    // differently from every other. The loop reading the same way every time
    // round is worth more than that here.
    for (;;) {
        const vc_vec2 g = grad(result.x);

        if (!g.is_valid()) {
            result.stop = gd_stop::diverged;
            return result;
        }

        // TEST WHERE WE ARE, BEFORE STEPPING, AND AFTER THE LAST STEP TOO.
        // Testing only after a step would report the gradient at the point
        // just LEFT, and a caller starting at a stationary point would take a
        // pointless move. Testing only inside a `while (iterations < budget)`
        // condition would skip the final point, so a run that reaches the
        // tolerance on its last allowed step gets reported as iteration_budget
        // with a converged answer in hand. The budget check below sits AFTER
        // this one for that reason.
        if (norm(g) <= (config.gradient_tolerance * gradient_x0)) {
            result.stop = gd_stop::gradient_small;
            return result;
        }

        if (result.iterations >= config.max_iterations) {
            result.stop = gd_stop::iteration_budget;
            return result;
        }

        // TODO: Better to solve for the two curvatures
        // separately/independently/sequentially?
        const vc_vec2 next_x{.x = result.x.x - (config.step * g.x),
                             .y = result.x.y - (config.step * g.y)};

        // next_x IS CHECKED BEFORE f IS CALLED ON IT. Past the boundary
        // step*g overflows and next_x holds inf or NaN; the header only
        // promises the callables are finite-valued "wherever the iteration
        // actually goes", so handing one a non-finite point is this code's
        // bug, not the caller's. An objective that rounds, indexes a table or
        // takes a log would trap there instead of returning the clean
        // gd_stop::diverged the contract promises.
        if (!next_x.is_valid()) {
            result.stop = gd_stop::diverged;
            return result;
        }
        const vc::float32 next_loss = f(next_x);

        // DIVERGENCE MUST STAY PLOTTABLE: stop here and do NOT append, so the
        // history is entirely finite and the climb right up to the edge can
        // still be drawn. residual_plot rejects a non-finite value anywhere
        // in any series.
        if (!std::isfinite(next_loss)) {
            result.stop = gd_stop::diverged;
            return result;
        }

        push(next_x, next_loss);
    }
}

} // namespace vc::math
