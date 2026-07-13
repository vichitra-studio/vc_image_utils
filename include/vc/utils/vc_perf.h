// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/utils/vc_strings.h"

namespace vc::utils::perf {

// TODO(you): implement in src/utils/vc_perf.cpp. Independent of
// vc::utils::log::enabled() — you may want normal logs without timing
// data, or timing data without normal logs.
void set_enabled(bool on);
bool enabled() noexcept;

// RAII: starts a clock on construction — skipped entirely if !enabled() at
// that point (checked once, stored in timing_), so a disabled timer has
// zero cost beyond the branch. On destruction, if timing was started,
// reports elapsed time via vc::utils::log::info(tag, label + ": " +
// elapsed_ms + "ms").
//
// No per-instance/per-tag toggle here, unlike log_builder/dump_builder —
// intentionally. A scoped_timer only ever reports once, at destruction,
// using an enabled() answer it already captured at construction; there's
// no repeated, reusable decision to make the way log_builder's is (called
// many times from one persistent instance). If silencing just one tag's
// timers ever becomes a real need, that's the same case grep-filtering
// output already solves for logging — not a reason to add a runtime
// toggle nothing currently needs.
class scoped_timer {
  public:
    explicit scoped_timer(vc::utils::string tag, vc::utils::string label);
    ~scoped_timer();

    scoped_timer(const scoped_timer&) = delete;
    scoped_timer& operator=(const scoped_timer&) = delete;

  private:
    vc::utils::string tag_;
    vc::utils::string label_;
    bool timing_; // true only if the clock was actually started

    // TODO(you): std::chrono::steady_clock::time_point start_; — only
    // meaningful when timing_ is true. #include <chrono>.
};

} // namespace vc::utils::perf
