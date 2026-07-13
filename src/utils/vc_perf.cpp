// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/utils/vc_perf.h"

#include <utility>

// TODO(you): you will need <chrono> for std::chrono::steady_clock,
// <string> for std::to_string when formatting elapsed_ms into the report
// message, and vc/utils/vc_log.h for vc::utils::log::info() (not pulled in
// by vc_perf.h itself — see its comment on why).
// #include <chrono>
// #include <string>
// #include "vc/utils/vc_log.h"

namespace vc::utils::perf {

namespace {

// Process-wide state, owned entirely by this translation unit — never
// exposed directly, only through the setters/getters below. Same
// single-threaded assumption as vc::utils::log.
bool g_enabled = true;

} // namespace

void set_enabled(bool on) {
    // TODO(you): g_enabled = on;
    (void)on;
}

bool enabled() noexcept {
    // TODO(you): return g_enabled;
    return false;
}

scoped_timer::scoped_timer(vc::utils::string tag, vc::utils::string label)
    : tag_(std::move(tag)), label_(std::move(label)), timing_(false) {
    // TODO(you): implement.
    //   1. timing_ = enabled();
    //   2. if (timing_) start_ = std::chrono::steady_clock::now();
    //      Skipping the clock read entirely when disabled is the whole
    //      point — no timer overhead when perf is off.
}

scoped_timer::~scoped_timer() {
    // TODO(you): implement.
    //   1. if (!timing_) return; — nothing was started, nothing to report.
    //   2. auto elapsed = std::chrono::steady_clock::now() - start_;
    //      auto elapsed_ms = std::chrono::duration_cast<
    //          std::chrono::milliseconds>(elapsed).count();
    //   3. vc::utils::log::info(tag_,
    //          label_ + ": " + std::to_string(elapsed_ms) + "ms");
    //      vc::utils::log::info() re-checks its own should_log() —
    //      see vc_log.h's note on why — so no extra gating needed here;
    //      the enabled() check that mattered already happened in the
    //      constructor.
}

} // namespace vc::utils::perf
