// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <span>
#include <vector>

#include "vc/core/vc_image.h"
#include "vc/core/vc_scalar.h"
#include "vc/utils/vc_strings.h"

namespace vc::debug {

// Render convergence histories as a line plot, so they can go through
// vc::debug::dump() like any other image.
//
// IN vc/debug/ RATHER THAN vc/pixelops/, beside vc_spectrum_viz.h, because this
// turns a number into a picture for a human and nothing computes with the
// result. poisson_iter_report::residual is the data; this is one way to look at
// it, and the report must stay usable without it.
//
// ---- WHY A PICTURE AT ALL ----
//
// P2 section D requires convergence to be MEASURED, and a column of numbers
// printed to a terminal is not a measurement anyone checks. The specific thing
// only the picture shows is SHAPE:
//
//   - jacobi FLAT while gauss_seidel falls -- the checkerboard stall, which is
//     a horizontal line and nothing else. notes/poisson.txt section 17c.
//   - red_black tracking gauss_seidel rather than jacobi. If red_black were
//     accidentally double-buffered it would converge to the RIGHT ANSWER at the
//     WRONG RATE, and this plot is the only check that fails.
//   - one channel lagging three orders behind the others, which a
//     worst-channel scalar hides.
//
// ---- LOG SCALE IS THE DEFAULT, AND NOT FOR PRETTINESS ----
//
// A relative residual starts at 1.0 and is driven toward 1e-6. On a linear axis
// everything below about 0.01 is the bottom pixel row, so the entire interesting
// part of the run -- which is all of it after the first few sweeps -- renders as
// a flat line along the floor. On a log axis a constant per-sweep shrink factor
// is a STRAIGHT LINE, and its slope IS the convergence rate, so two methods can
// be compared by eye.
//
// Non-positive values cannot be plotted on a log axis. They occur legitimately:
// an all-zero channel reports exactly 0.0 (poisson_iter's documented exception).
// They are CLAMPED to config.floor and drawn, not dropped -- a dropped series is
// an invisible failure, and a line pinned at the floor is visibly degenerate.
struct residual_series {
    vc::utils::string label; // drawn in the legend; "" is legal and unlabelled
    std::vector<vc::float32> values; // residual[c], one entry per sweep + the
                                     // initial value at index 0
};

struct residual_plot_config {
    vc::image_dim width = 800;
    vc::image_dim height = 500;

    // Everything at or below this is clamped to it. Also the bottom of the
    // y-axis, so a run that converges to 1e-6 is not squeezed into one pixel by
    // a single stray 1e-30.
    vc::float32 floor = 1e-8F;

    // false gives a linear y-axis. Present so the choice above is testable
    // rather than asserted, and for the one honest linear use: showing someone
    // why the log axis is necessary.
    bool log_scale = true;
};

// Returns a 3-channel f32 image, values in [0,1], of config.width x
// config.height.
//
// X is the SWEEP INDEX, shared across all series, running 0..(longest series
// length - 1). Series end where they end -- channels converge at different
// sweeps (poisson_iter_report::residual documents this), and a short series is
// drawn short rather than extended with an invented value.
//
// Y is the residual, log10 by default, spanning config.floor to the largest
// value in any series -- which is 1.0 for any run started from zero.
//
// ---- WHAT IT REJECTS ----
//
//   - an empty `series` span: there is nothing to plot, and a blank image is
//     indistinguishable from a solver that produced nothing
//   - a series whose `values` is empty, for the same reason -- note that a
//     LEGITIMATE run always has at least residual[c][0], so this is a caller bug
//   - width or height below 64: axis labels and the legend do not fit, and a
//     16x16 "plot" is a smear that reads as a bug in the solver
//   - a non-finite value anywhere in any series: NaN means the solver diverged
//     or divided by zero, and silently skipping the point hides exactly the run
//     worth looking at
//   - config.floor <= 0 with log_scale true
//
// All throw vc_error_code::invalid_argument, naming residual_plot.
[[nodiscard]] vc::vc_image
residual_plot(std::span<const residual_series> series,
              const residual_plot_config& config = {});

} // namespace vc::debug
