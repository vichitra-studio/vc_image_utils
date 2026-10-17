// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/debug/vc_residual_plot.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace vc::debug {

// ** S8b: THIS IS A SCAFFOLD STUB. REPLACE THE BODY. **
//
// It exists so the file is registered in CMakeLists.txt and the suite LINKS.
// Without it the only symptom is an undefined symbol at link time, which says
// nothing about what is missing or where the contract is written down.
//
// It throws unconditionally, which means 16_poisson_crossval's groups 9a-9e --
// the rejection checks -- PASS AGAINST IT VACUOUSLY. That is accounted for:
// 9f, 9g and 9h are acceptance checks that this stub fails, and the file's
// header says so. Do not read "9a-9e passed" as progress.
//
// The contract is in include/vc/debug/vc_residual_plot.h and is not repeated
// here. The order of work that the checks imply: validate the five rejections
// first (they are cheap and 9f/9g/9h will still fail), then the axes, then the
// lines, then the legend.
vc::vc_image residual_plot(std::span<const residual_series> series,
                           const residual_plot_config& config) {
    (void)series;
    (void)config;
    throw vc::vc_exception(
        vc::vc_error_code::invalid_argument,
        "residual_plot: not implemented yet (S8b). Contract is in "
        "include/vc/debug/vc_residual_plot.h; checks are in "
        "examples/16_poisson_crossval.cpp group 9");
}

} // namespace vc::debug
