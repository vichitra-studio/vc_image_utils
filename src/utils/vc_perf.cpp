// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <string>
#include <utility>

#include "vc/utils/vc_log.h"
#include "vc/utils/vc_perf.h"

namespace vc::utils::perf {

namespace {

// Process-wide state, owned entirely by this translation unit — never
// exposed directly, only through the setters/getters below. Same
// single-threaded assumption as vc::utils::log.
bool g_enabled = true;

} // namespace

void set_enabled(bool on) {
    g_enabled = on;
}

bool enabled() noexcept {
    return g_enabled;
}

scoped_timer::scoped_timer(vc::utils::string tag, vc::utils::string label)
    : tag_(std::move(tag)), label_(std::move(label)) {
    timing_ = enabled();
    if (timing_)
        start_ = std::chrono::steady_clock::now();
}

scoped_timer::~scoped_timer() {
    if (!timing_)
        return;
    auto elapsed = std::chrono::steady_clock::now() - start_;
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    vc::utils::log::info("PERF " + tag_,
                         label_ + ": " + std::to_string(elapsed_ms) + " ms");
}

} // namespace vc::utils::perf
